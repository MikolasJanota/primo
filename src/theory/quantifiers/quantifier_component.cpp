#include "theory/quantifiers/quantifier_component.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/binders.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/statistics.hpp"

namespace primo::theory {

namespace {

bool is_quantifier(const core::Term& node) {
  return node.kind == core::TermKind::Forall || node.kind == core::TermKind::Exists;
}

}  // namespace

QuantifierComponent::QuantifierComponent(core::TermManager& terms,
                                         std::span<const core::TermId> ground_roots,
                                         FreshConstantFactory fresh_constant,
                                         QuantifierInstantiationOptions options,
                                         util::Statistics& statistics)
    : terms_(terms),
      fresh_constant_(std::move(fresh_constant)),
      options_(options),
      statistics_(statistics),
      simplifier_(terms) {
  descriptor_.id = "quantifiers";
  descriptor_.fragment = "enumerative instantiation of quantified formulas";
  descriptor_.convexity = Convexity::Convex;
  descriptor_.capabilities.incremental = true;
  descriptor_.capabilities.check_on_full_model = true;
  // Bool's domain is exactly {true, false}; no other Bool term is needed.
  auto& booleans = pools_[terms_.symbols().bool_sort()];
  booleans.push_back(terms_.mk_bool(false));
  booleans.push_back(terms_.mk_bool(true));
  for (const core::TermId root : ground_roots) {
    add_pool_terms(root);
  }
}

bool QuantifierComponent::owns_atom(core::TermId atom) const {
  return is_quantifier(terms_.get(atom));
}

std::vector<core::TermId> QuantifierComponent::interface_variables(core::TermId) const {
  return {};
}

void QuantifierComponent::register_interface(std::span<const ComponentInterfaceScope> scopes) {
  if (!scopes.empty()) {
    throw std::logic_error("the quantifier component shares no interface terms");
  }
}

void QuantifierComponent::register_atoms(std::span<const ComponentAtom> atoms) {
  // Growth happens at a detached boundary; the next dispatcher replays the
  // assignment from scratch, as for every other component.
  reset();
  assignment_.register_atoms(atoms);
}

core::TermId QuantifierComponent::encode_equality(const InterfacePair&) {
  throw std::logic_error("the quantifier component shares no interface terms");
}

// Enumeration and lemma caches survive SAT backtracking; truth lives in the view.
void QuantifierComponent::reset() {}
void QuantifierComponent::push_level() {}
void QuantifierComponent::pop_to_level(std::size_t) {}
void QuantifierComponent::assign(std::span<const AssignedTheoryLiteral> delta) {
  assignment_.check_delta(delta);
}

TheoryCheckResult QuantifierComponent::check(TheoryCheckEffort effort,
                                             std::uint64_t assignment_epoch,
                                             std::span<const InterfaceRelation>) {
  statistics_.increment(util::StatKey::QuantChecks);
  TheoryCheckResult result;
  result.model.token = {descriptor_.id, assignment_epoch, ++revision_};
  if (!queue_.empty()) {
    result.status = TheoryCheckStatus::LemmasPending;
    return result;
  }
  const bool full = effort == TheoryCheckEffort::FullModel;
  // Instances are chosen once every quantifier atom is assigned (section 2's loop).
  if (!assignment_.complete()) {
    result.status = full ? TheoryCheckStatus::Unknown : TheoryCheckStatus::Consistent;
    if (full) result.diagnostic = "a full model left quantifier atoms unassigned";
    return result;
  }

  std::vector<std::pair<core::TermId, bool>> universal;
  bool uncovered = false;
  for (const AssignedTheoryLiteral& assigned : assignment_.full_assignment(true)) {
    const bool exists = terms_.get(assigned.atom).kind == core::TermKind::Exists;
    if (exists != assigned.value) {
      universal.emplace_back(assigned.atom, assigned.value);
    } else if (full) {
      uncovered = uncovered || !skolemized_.contains({assigned.atom, assigned.value});
    } else {
      skolemize(assigned.atom, assigned.value);
    }
  }

  if (full) {
    // The verdict on an accepted model; never new lemmas. `sat` needs every
    // assigned atom covered by a Skolem lemma, which SAT has satisfied.
    if (universal.empty() && !uncovered) {
      result.status = TheoryCheckStatus::Consistent;
    } else if (instances_ >= options_.max_instances) {
      result.status = TheoryCheckStatus::ResourceLimit;
      result.diagnostic = "quantifier instantiation budget reached";
    } else {
      statistics_.increment(util::StatKey::QuantExhausted);
      result.status = TheoryCheckStatus::Unknown;
      result.diagnostic = "enumerative instantiation found no new instance";
    }
    return result;
  }

  // Skolem lemmas already give SAT new work. Do not hold them while searching
  // a universal's tuple product for another useful instance.
  if (!queue_.empty()) {
    statistics_.increment(util::StatKey::QuantRounds);
    result.status = TheoryCheckStatus::LemmasPending;
    return result;
  }

  if (!universal.empty()) {
    // Fairness across quantifiers: a rotating start and an even share first.
    const std::size_t share =
        std::max<std::size_t>(1, options_.instances_per_round / universal.size());
    std::size_t remaining = options_.instances_per_round;
    for (std::size_t step = 0; step < universal.size() && remaining > 0; ++step) {
      const auto& [atom, value] = universal[(next_obligation_ + step) % universal.size()];
      remaining -= enumerate(atom, value, std::min(share, remaining));
    }
    next_obligation_ = (next_obligation_ + 1) % universal.size();
  }
  if (!queue_.empty()) {
    statistics_.increment(util::StatKey::QuantRounds);
    result.status = TheoryCheckStatus::LemmasPending;
    return result;
  }
  // Nothing new for this assignment. A Partial check may run before SAT has
  // finished, so failing here could stop the search short of a ground conflict
  // or of a Skolem-covered assignment; the FullModel check gives the verdict.
  result.status = TheoryCheckStatus::Consistent;
  return result;
}

std::vector<core::TermId> QuantifierComponent::take_lemmas(std::size_t limit) {
  std::vector<core::TermId> taken;
  while (!queue_.empty() && taken.size() < limit) {
    taken.push_back(queue_.front());
    queue_.pop_front();
  }
  // Handed-over lemmas become assertions; their ground terms join the pools.
  for (const core::TermId lemma : taken) {
    add_pool_terms(lemma);
  }
  statistics_.increment(util::StatKey::QuantLemmas, taken.size());
  return taken;
}

ComponentModelFragment QuantifierComponent::export_model(const TheoryModelToken&) const {
  throw std::logic_error("quantified formulas have no model export");
}

void QuantifierComponent::add_pool_terms(core::TermId root) {
  const core::SortId boolean = terms_.symbols().bool_sort();
  std::vector<core::TermId> pending{root};
  while (!pending.empty()) {
    const core::TermId current = pending.back();
    pending.pop_back();
    if (!walked_.insert(current).second) {
      continue;
    }
    const core::Term& node = terms_.get(current);
    if (is_quantifier(node) || node.kind == core::TermKind::Pattern) {
      continue;
    }
    if ((node.kind == core::TermKind::Constant || node.kind == core::TermKind::Application) &&
        node.sort != boolean && terms_.loose_index(current) == 0) {
      pools_[node.sort].push_back(current);
      statistics_.increment(util::StatKey::QuantPoolTerms);
    }
    pending.insert(pending.end(), node.children.begin(), node.children.end());
  }
}

const std::vector<core::TermId>& QuantifierComponent::pool(core::SortId sort) {
  auto& terms = pools_[sort];
  if (terms.empty()) {
    // Theorem 1 assumes a term of each sort; supply one for an empty sort.
    terms.push_back(fresh_constant_(sort, "herbrand"));
    if (options_.export_sink != nullptr) options_.export_sink->constants.push_back(terms.back());
  }
  return terms;
}

core::TermId QuantifierComponent::canonical(core::TermId term) {
  return options_.simplify_instances ? simplifier_.simplify(term) : term;
}

bool QuantifierComponent::add_instance(core::TermId atom, bool value, core::TermId instance) {
  // Different tuples can yield the same instance, for example when a variable
  // does not occur in the body.
  if (!raw_instances_.insert({atom, value, instance}).second) {
    statistics_.increment(util::StatKey::QuantInstancesRawDuplicates);
    return false;
  }
  if (options_.simplify_instances) {
    const core::TermId simplified = simplifier_.simplify(instance);
    if (simplified != instance) {
      statistics_.increment(util::StatKey::QuantInstancesSimplified);
    }
    // Distinct as instantiated, equal after normalization: generated before.
    if (!canonical_instances_.insert({atom, value, simplified}).second) {
      statistics_.increment(util::StatKey::QuantInstancesSimplifiedDuplicates);
      return false;
    }
    instance = simplified;
  }
  // The budget counts distinct instances. Counting every tuple let duplicates
  // exhaust it: with normalization on, sledgehammer/TwoSquares files spent
  // 100,000 tuples on about 400 lemmas and gave up where 700 instances refute.
  ++instances_;
  // A true atom's lemma is `not Q or inst` and a false one's `Q or not inst`;
  // either is valid outright when the instance has that value.
  if (const core::Term& node = terms_.get(instance);
      node.kind == core::TermKind::BoolValue && node.bool_value == value) {
    statistics_.increment(util::StatKey::QuantInstancesTrivial);
    return false;
  }
  if (!enqueue(canonical(lemma_for(atom, value, instance)))) {
    statistics_.increment(util::StatKey::QuantInstancesDuplicateLemmas);
    return false;
  }
  return true;
}

bool QuantifierComponent::enqueue(core::TermId lemma) {
  const core::Term& node = terms_.get(lemma);
  if (node.kind == core::TermKind::BoolValue && node.bool_value) {
    return false;
  }
  if (!lemmas_.insert(lemma).second) {
    return false;
  }
  queue_.push_back(lemma);
  return true;
}

core::TermId QuantifierComponent::lemma_for(core::TermId atom, bool value, core::TermId instance) {
  // A true atom implies its instance; a false one is implied by it.
  return value ? terms_.mk_or({terms_.mk_not(atom), instance})
               : terms_.mk_or({atom, terms_.mk_not(instance)});
}

void QuantifierComponent::skolemize(core::TermId atom, bool value) {
  if (!skolemized_.emplace(atom, value).second) {
    return;
  }
  std::vector<core::TermId> constants;
  for (const core::SortId sort : core::quantifier_sorts(terms_, atom)) {
    constants.push_back(fresh_constant_(sort, "skolem"));
  }
  const core::TermId instance = canonical(core::instantiate(terms_, atom, constants));
  const core::TermId lemma = canonical(lemma_for(atom, value, instance));
  const bool queued = enqueue(lemma);
  if (queued) {
    statistics_.increment(util::StatKey::QuantSkolemLemmas);
  }
  if (options_.export_sink != nullptr) {
    auto& sink = *options_.export_sink;
    sink.constants.insert(sink.constants.end(), constants.begin(), constants.end());
    sink.skolemizations.push_back({atom, value, constants, queued ? lemma : core::kInvalidTermId});
  }
}

std::size_t QuantifierComponent::enumerate(core::TermId atom, bool value, std::size_t budget) {
  Enumeration& state = enumerations_[{atom, value}];
  if (state.sorts.empty()) {
    state.sorts = core::quantifier_sorts(terms_, atom);
  }
  const std::size_t arity = state.sorts.size();
  // Snapshot the pools: instantiating does not change them, but pool() may
  // create a term for an empty sort, which would reallocate the map's vectors.
  std::vector<std::vector<core::TermId>> pools;
  std::vector<std::size_t> sizes;
  pools.reserve(arity);
  sizes.reserve(arity);
  for (const core::SortId sort : state.sorts) {
    pools.push_back(pool(sort));
    sizes.push_back(pools.back().size());
  }

  std::size_t made = 0;
  // Tuples visited since this obligation last queued a lemma.
  std::size_t idle = 0;
  std::vector<core::TermId> tuple(arity);
  // Instantiates one index tuple. False once this round's share or the
  // instance budget is spent, or once the scan stalls with lemmas queued.
  const TupleVisitor visit = [&](const IndexTuple& indices) {
    for (std::size_t position = 0; position < arity; ++position) {
      tuple[position] = pools[position][indices[position]];
    }
    if (instances_ < options_.max_instances && state.tried.insert(tuple).second) {
      statistics_.increment(util::StatKey::QuantInstances);
      if (add_instance(atom, value, core::instantiate(terms_, atom, tuple))) {
        idle = 0;
        if (options_.export_sink != nullptr)
          options_.export_sink->tuples.emplace_back(state.sorts, tuple);
        return ++made < budget && instances_ < options_.max_instances;
      }
    }
    // A long run of tuples that add nothing would hold back lemmas SAT can
    // already use: 26 unused Boolean variables give 67,108,864 tuples and one
    // instance. The queue stays empty until this round queues a lemma, so a
    // round still delivers one or enumerates to the end.
    if (options_.stall_tuples != 0 && !queue_.empty() && ++idle >= options_.stall_tuples) {
      statistics_.increment(util::StatKey::QuantStalls);
      return false;
    }
    return instances_ < options_.max_instances;
  };

  const TupleOrder order = options_.tuple_order;
  if (is_staged(order)) {
    const std::size_t stages = stage_count(order, sizes);
    for (std::size_t stage = state.first_open_level; stage < stages; ++stage) {
      bool clipped = false;
      if (!visit_stage(order, stage, sizes, visit, clipped)) {
        return made;
      }
      if (!clipped && stage == state.first_open_level) {
        ++state.first_open_level;
      }
    }
    return made;
  }

  IndexTuple indices;
  if (order == TupleOrder::IterativeDeepening) {
    if (!state.deepening) {
      state.deepening = std::make_unique<IterativeDeepening>(options_.deepening_increment);
    }
    while (state.deepening->next(sizes, indices)) {
      if (!visit(indices)) {
        return made;
      }
    }
    return made;
  }
  if (!state.random) {
    // Each quantifier and polarity gets its own reproducible stream.
    const std::uint64_t seed = options_.random_seed ^
                               (static_cast<std::uint64_t>(atom) * 0x9e3779b97f4a7c15ULL) ^
                               (value ? 1U : 0U);
    state.random = std::make_unique<RandomWalk>(seed);
  }
  while (state.random->next(sizes, indices)) {
    if (!visit(indices)) {
      return made;
    }
  }
  return made;
}

}  // namespace primo::theory
