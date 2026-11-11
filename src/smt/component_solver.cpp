#include "smt/component_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "core/term.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/assignment_coordinator.hpp"
#include "smt/component_dispatcher.hpp"
#include "smt/component_model_assembler.hpp"
#include "smt/component_model_coordinator.hpp"
#include "smt/solver_runtime.hpp"
#include "smt/solver_support.hpp"
#include "smt/theory_combination_plan.hpp"

namespace primo::smt {
namespace {

class CombinationTimer {
 public:
  CombinationTimer(util::Statistics& statistics, std::string key)
      : statistics_(statistics), key_(std::move(key)), start_(std::chrono::steady_clock::now()) {}
  ~CombinationTimer() {
    statistics_.increment(
        key_, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - start_)
                                             .count()));
  }

 private:
  util::Statistics& statistics_;
  std::string key_;
  std::chrono::steady_clock::time_point start_;
};

using PairKey = std::tuple<core::SortId, core::TermId, core::TermId>;

theory::ComponentAtom component_atom(const BooleanClausifier::TheoryAtom& atom) {
  return {atom.term, atom.lit, atom.occurs_positive, atom.occurs_negative,
          atom.theory_relevant_at_both_polarities};
}

void publish(const ComponentModelStatistics& source, util::Statistics& statistics) {
  statistics.set(":combination.component-checks", source.component_checks);
  statistics.set(":combination.learned-clauses", source.learned_clauses);
  statistics.set(":combination.model.component-checks", source.component_checks);
  statistics.set(":combination.model.snapshots", source.snapshots);
  statistics.set(":combination.model.mutations", source.mutations);
  statistics.set(":combination.model.fixed-points", source.fixed_points);
  statistics.set(":combination.model.care-pairs", source.care_pairs);
  statistics.set(":combination.model.care-pair-agreements", source.care_pair_agreements);
  statistics.set(":combination.model.spanning-pair-checks", source.spanning_pair_checks);
  statistics.set(":combination.model.disagreements", source.disagreements);
  statistics.set(":combination.model.learned-clauses", source.learned_clauses);
  statistics.set(":combination.model.decisions", source.model_decisions);
  statistics.set(":combination.model.fresh-decisions", source.fresh_decisions);
  statistics.set(":combination.model.stale-snapshots", source.stale_snapshots);
  statistics.set(":combination.model.rejected-mutations", source.rejected_mutations);
}

}  // namespace

CheckSatOutcome solve_component_recipe(core::TermManager& terms,
                                       std::span<const core::TermId> assertions,
                                       const ModelConversionChain& conversions,
                                       const CheckSatOptions& options, util::Statistics& statistics,
                                       const CombinationRecipe& recipe) try {
  sat::CadicalBackend backend(options.cadical_options);
  SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
  const ScopedTerminatorConnection terminator_connection(backend, terminator);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const auto assertion : assertions) clausifier.assert_formula(assertion);
  if (recipe.prepare_boolean) recipe.prepare_boolean(clausifier);

  sat::Trail completed_assignment;
  std::vector<std::unique_ptr<theory::TheoryComponent>> owned;
  std::vector<theory::TheoryComponent*> components;
  for (const auto& factory : recipe.factories) {
    owned.push_back(factory(recipe.model_guided));
    components.push_back(owned.back().get());
  }
  const auto boolean_constant = [&](core::TermId term) {
    const auto& node = terms.get(term);
    return node.kind == core::TermKind::Constant && node.sort == terms.symbols().bool_sort();
  };
  std::vector<theory::ComponentAtom> initial_atoms;
  for (const auto& atom : clausifier.theory_atoms()) {
    if (atom.term == core::kInvalidTermId) continue;
    // Keep Boolean arguments with EUF when it owns them. Only otherwise
    // unowned Boolean constants are purely propositional driver assignments.
    if (boolean_constant(atom.term) &&
        std::none_of(components.begin(), components.end(),
                     [&](const auto* component) { return component->owns_atom(atom.term); }))
      continue;
    initial_atoms.push_back(component_atom(atom));
  }
  std::optional<TheoryCombinationPlan> planned;
  try {
    planned.emplace(terms, components, initial_atoms, recipe.model_guided);
    planned->register_interfaces(components);
  } catch (const UnsupportedCombination& error) {
    return {CheckSatResult::Unsupported, error.what(), statistics, "",
            InconclusiveReason::Unsupported};
  }
  const auto& plan = *planned;
  if (recipe.online) {
    for (const auto& descriptor : plan.descriptors()) {
      if (!descriptor.capabilities.incremental) {
        throw UnsupportedCombination("online recipe requires incremental components");
      }
    }
  }
  if (options.produce_model) {
    for (const auto& descriptor : plan.descriptors()) {
      if (!descriptor.capabilities.model_export) {
        throw UnsupportedCombination("component does not support model export");
      }
    }
  }
  const bool guided = plan.policy() == CombinationPolicy::ModelGuided;
  std::size_t total_pairs = 0;
  std::size_t shared_terms = 0;
  for (const auto& scope : plan.scopes()) {
    shared_terms += scope.terms.size();
    total_pairs += scope.terms.size() * (scope.terms.size() - 1) / 2;
  }
  std::map<PairKey, theory::InterfaceRelation> registered;
  std::size_t bridge_clauses = 0;
  std::unique_ptr<ComponentDispatcher> dispatcher;
  std::unique_ptr<AssignmentConnection> assignment_connection;
  const auto detach_dispatcher = [&]() {
    assignment_connection.reset();
    dispatcher.reset();
  };
  const auto materialize = [&](const theory::InterfacePair& requested) -> std::optional<sat::Lit> {
    const theory::InterfacePair pair{std::min(requested.lhs, requested.rhs),
                                     std::max(requested.lhs, requested.rhs), requested.sort};
    const auto scope_index = plan.scope_for(pair);
    if (!scope_index.has_value()) return std::nullopt;
    const PairKey key{pair.sort, pair.lhs, pair.rhs};
    if (const auto found = registered.find(key); found != registered.end())
      return found->second.equality_lit;
    detach_dispatcher();
    const auto equality = terms.mk_eq(pair.lhs, pair.rhs);
    for (const auto participant : plan.scopes()[*scope_index].participants) {
      const auto encoding = components[participant]->encode_equality(pair);
      if (encoding == equality) continue;
      // Boolean syntax optimization, independent of the component's theory:
      // e iff conjunction needs the existing k+1 direct implication clauses.
      const auto kind = terms.get(encoding).kind;
      const auto children = terms.get(encoding).children;
      if (kind == core::TermKind::And) {
        for (const auto child : children) {
          clausifier.assert_formula(terms.mk_implies(equality, child));
          ++bridge_clauses;
        }
        clausifier.assert_formula(terms.mk_implies(encoding, equality));
        ++bridge_clauses;
      } else {
        clausifier.assert_formula(terms.mk_implies(equality, encoding));
        clausifier.assert_formula(terms.mk_implies(encoding, equality));
        bridge_clauses += 2;
      }
    }
    const auto literal = clausifier.ensure_theory_atom(equality).lit;
    registered.emplace(key, theory::InterfaceRelation{pair.lhs, pair.rhs, pair.sort, literal,
                                                      theory::InterfaceRelationValue::Equal});
    return literal;
  };
  if (guided) {
    for (const auto& atom : initial_atoms) {
      const auto kind = terms.get(atom.term).kind;
      const auto children = terms.get(atom.term).children;
      if (kind == core::TermKind::Equality && children.size() == 2) {
        const auto sort = terms.get(children[0]).sort;
        static_cast<void>(materialize({children[0], children[1], sort}));
      }
    }
  } else {
    for (const auto& scope : plan.scopes()) {
      for (std::size_t lhs = 0; lhs < scope.terms.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < scope.terms.size(); ++rhs) {
          static_cast<void>(materialize({scope.terms[lhs], scope.terms[rhs], scope.sort}));
        }
      }
    }
  }

  statistics.set(":combination.components", components.size());
  statistics.set(":combination.shared-terms", shared_terms);
  statistics.set(":combination.arrangement-pairs", total_pairs);
  statistics.set(":combination.model.enabled", guided ? 1 : 0);
  statistics.set(":combination.model.total-pairs", total_pairs);
  for (const auto* key :
       {":combination.model.complete-fallbacks", ":combination.model.fallback-budget",
        ":combination.model.fallback-unsupported", ":combination.model.fallback-unknown",
        ":combination.model.fallback-resource-limit", ":combination.component-checks",
        ":combination.online-components.integer-completions",
        ":combination.online-components.rebuilds", ":combination.components.full-builds",
        ":combination.components.incremental-growths"}) {
    statistics.set(key, 0);
  }
  std::vector<std::vector<theory::ComponentAtom>> atoms_by_component(components.size());
  std::vector<theory::TheoryComponent*> online_components;
  std::vector<std::size_t> online_order = recipe.online_order;
  if (recipe.online) {
    auto sorted = online_order;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() != components.size())
      throw std::logic_error("incomplete online recipe order");
    for (std::size_t index = 0; index < sorted.size(); ++index) {
      if (sorted[index] != index)
        throw std::logic_error("online recipe order is not a permutation");
    }
    for (const auto index : online_order) online_components.push_back(components[index]);
  }
  std::vector<theory::ComponentAtom> propositional_atoms;
  std::size_t installed_atoms = 0;
  std::size_t installed_pairs = 0;
  const auto install_atoms = [&]() {
    detach_dispatcher();
    // Every producer uses this boundary, including generated interface
    // encodings. Complete the registry before taking any view of its storage.
    if (recipe.prepare_boolean) recipe.prepare_boolean(clausifier);
    const bool first = statistics.get(":combination.components.full-builds") == 0;
    CombinationTimer timer(statistics, recipe.timing_prefix + (first ? "component-rebuild-us"
                                                                     : "component-growth-us"));
    std::vector<std::vector<theory::ComponentAtom>> additions(components.size());
    const auto& all = clausifier.theory_atoms();
    for (std::size_t atom_index = installed_atoms; atom_index < all.size(); ++atom_index) {
      if (all[atom_index].term == core::kInvalidTermId) continue;
      std::optional<std::size_t> owner;
      for (std::size_t index = 0; index < components.size(); ++index) {
        if (components[index]->owns_atom(all[atom_index].term)) {
          if (owner.has_value()) throw UnsupportedCombination("ambiguous generated atom ownership");
          owner = index;
        }
      }
      if (!owner.has_value()) {
        if (!boolean_constant(all[atom_index].term))
          throw UnsupportedCombination("generated atom has no owner");
        propositional_atoms.push_back(component_atom(all[atom_index]));
        continue;
      }
      const auto atom = component_atom(all[atom_index]);
      additions[*owner].push_back(atom);
      atoms_by_component[*owner].push_back(atom);
    }
    for (std::size_t index = 0; index < components.size(); ++index) {
      components[index]->register_atoms(additions[index]);
      const auto& id = plan.descriptors()[index].id;
      statistics.set(":no.mixed." + id + "-atoms", atoms_by_component[index].size());
      if (!first)
        statistics.increment(":combination.components.incremental-" + id + "-atoms",
                             additions[index].size());
    }
    if (recipe.online) {
      dispatcher = std::make_unique<ComponentDispatcher>(
          online_components, [&](sat::Lit phase) { backend.phase(phase); },
          [&] {
            record_theory_conflict(statistics);
            return terminator.conflict_limit_reached();
          },
          recipe.forgettable_clauses);
      for (std::size_t index = 0; index < online_order.size(); ++index) {
        dispatcher->register_atoms(index, atoms_by_component[online_order[index]]);
        for (const auto phase : online_components[index]->phase_literals()) backend.phase(phase);
      }
      assignment_connection = std::make_unique<AssignmentConnection>(backend, *dispatcher);
      if (propositional_atoms.empty()) {
        for (const auto& atoms : atoms_by_component) {
          for (const auto& atom : atoms)
            backend.add_observed_var(atom.lit.value < 0 ? -atom.lit.value : atom.lit.value);
        }
      } else {
        // Driver-owned Boolean atoms need the same SAT observation contract as
        // component atoms. Preserve registry order for the standalone recipe;
        // dropping these observations changes SAT elimination and search.
        for (const auto& atom : all) {
          if (atom.term != core::kInvalidTermId)
            backend.add_observed_var(atom.lit.value < 0 ? -atom.lit.value : atom.lit.value);
        }
      }
      statistics.increment(":combination.online-components.rebuilds");
    } else {
      for (auto* component : components)
        component->bind_assignment_view(sat::TrailView(completed_assignment));
    }
    statistics.increment(first ? ":combination.components.full-builds"
                               : ":combination.components.incremental-growths");
    installed_atoms = all.size();
    installed_pairs = registered.size();
    statistics.set(":combination.arrangement-bridge-clauses", bridge_clauses);
    statistics.set(":combination.model.materialized-pairs", installed_pairs);
    statistics.set(util::StatKey::TheoryAtoms, all.size());
  };
  install_atoms();
  statistics.set(":combination.online-components.enabled", recipe.online ? 1 : 0);
  std::vector<sat::Lit> active_choices;
  std::size_t decision_batches = 0;
  std::uint64_t epoch = 0;
  ComponentModelCoordinator coordinator;
  const auto fallback = [&](const char* reason) {
    auto complete = recipe;
    complete.model_guided = false;
    CheckSatOutcome outcome;
    {
      CombinationTimer timer(statistics, recipe.timing_prefix + "complete-fallback-us");
      outcome =
          solve_component_recipe(terms, assertions, conversions, options, statistics, complete);
    }
    statistics.set(":combination.model.enabled", 1);
    statistics.increment(":combination.model.complete-fallbacks");
    statistics.increment(reason);
    outcome.statistics = statistics;
    return outcome;
  };
  while (true) {
    if (auto stop = terminator.stop_outcome()) return *stop;
    if (guided && decision_batches >= recipe.decision_budget)
      return fallback(":combination.model.fallback-budget");
    if (installed_pairs != registered.size()) install_atoms();
    for (const auto choice : active_choices) backend.assume(choice);
    statistics.increment(util::StatKey::SatSolveCalls);
    sat::SatResult sat_result;
    {
      CombinationTimer timer(statistics, recipe.timing_prefix + "sat-us");
      sat_result = backend.solve();
    }
    publish_sat_stats(backend.statistics(), statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    // Lemma queue: an online component that must add consequences over new
    // atoms stops the search; its queue is drained here, at the established
    // growth boundary, and the problem is solved again. An UNSAT result with a
    // stop pending is still UNSAT, since lemmas only strengthen the formula.
    if (dispatcher && dispatcher->lemmas_pending().has_value() &&
        sat_result == sat::SatResult::Sat) {
      if (!recipe.prepare_lemmas) {
        return {CheckSatResult::Unsupported,
                "a component queued lemmas but the recipe cannot prepare them", statistics, "",
                InconclusiveReason::Unsupported};
      }
      const std::size_t owner = online_order.at(*dispatcher->lemmas_pending());
      std::vector<core::TermId> lemmas = components[owner]->take_lemmas(recipe.lemma_batch);
      if (lemmas.empty()) {
        throw std::logic_error("a component reported queued lemmas but supplied none");
      }
      detach_dispatcher();
      statistics.increment(":combination.lemmas.batches");
      statistics.increment(":combination.lemmas.formulas", lemmas.size());
      {
        CombinationTimer timer(statistics, recipe.timing_prefix + "lemma-us");
        for (const auto formula : recipe.prepare_lemmas(std::move(lemmas))) {
          clausifier.assert_formula(formula);
        }
      }
      install_atoms();
      continue;
    }
    if (dispatcher && dispatcher->failure().has_value()) {
      const auto& failure = *dispatcher->failure();
      const bool unsupported = failure.status == theory::TheoryCheckStatus::Unsupported;
      const auto reason = unsupported ? InconclusiveReason::Unsupported
                          : failure.status == theory::TheoryCheckStatus::Cancelled
                              ? InconclusiveReason::Cancelled
                          : failure.status == theory::TheoryCheckStatus::ResourceLimit
                              ? InconclusiveReason::ResourceLimit
                              : InconclusiveReason::Unknown;
      return {unsupported ? CheckSatResult::Unsupported : CheckSatResult::Unknown,
              failure.diagnostic, statistics, "", reason};
    }
    if (sat_result == sat::SatResult::Unsat && !active_choices.empty()) {
      sat::Clause clause;
      std::vector<sat::Lit> surviving;
      for (const auto choice : active_choices) {
        if (backend.failed(choice))
          clause.push_back(sat::Lit{-choice.value});
        else
          surviving.push_back(choice);
      }
      if (clause.empty()) {
        for (const auto choice : active_choices) clause.push_back(sat::Lit{-choice.value});
        surviving.clear();
      }
      backend.add_clause(clause);
      active_choices = std::move(surviving);
      statistics.increment(":no.interface.model-equality-blocking-clauses");
      continue;
    }
    if (sat_result != sat::SatResult::Sat) return {map_sat_result(sat_result), "", statistics, ""};
    ++epoch;
    statistics.increment(":combination.rounds");
    std::vector<theory::InterfaceRelation> arrangement;
    for (const auto& [key, relation] : registered) {
      static_cast<void>(key);
      auto assigned = relation;
      const auto value =
          backend.value(relation.equality_lit.value < 0 ? -relation.equality_lit.value
                                                        : relation.equality_lit.value);
      if (!value.has_value()) throw std::logic_error("SAT model omitted an interface equality");
      assigned.value = (*value == (relation.equality_lit.value > 0))
                           ? theory::InterfaceRelationValue::Equal
                           : theory::InterfaceRelationValue::Disequal;
      arrangement.push_back(assigned);
    }
    {
      CombinationTimer timer(statistics, recipe.timing_prefix + "assignment-extraction-us");
      if (!recipe.online) {
        completed_assignment.clear();
        for (const auto& atoms : atoms_by_component) {
          for (const auto& atom : atoms) {
            const int var = std::abs(atom.lit.value);
            const auto value = backend.value(var);
            if (!value) throw std::logic_error("SAT model omitted a component atom");
            completed_assignment.assign(*value ? var : -var);
          }
        }
        for (std::size_t index = 0; index < components.size(); ++index) {
          std::vector<theory::AssignedTheoryLiteral> assignment;
          for (const auto& atom : atoms_by_component[index]) {
            const auto literal = completed_assignment.literal(std::abs(atom.lit.value));
            assignment.push_back({atom.term, atom.lit, literal == atom.lit.value});
          }
          components[index]->reset();
          components[index]->assign(assignment);
        }
      }
    }
    ModelBasedCombinationResult combined;
    {
      CombinationTimer timer(statistics, recipe.timing_prefix + "model-reconciliation-us");
      combined = coordinator.reconcile(plan, epoch, components, arrangement, materialize);
    }
    publish(coordinator.statistics(), statistics);
    switch (combined.status) {
      case ModelBasedCombinationStatus::Accept:
        break;
      case ModelBasedCombinationStatus::LearnClause:
        statistics.increment(util::StatKey::TheoryConflicts);
        if (std::all_of(combined.clause.begin(), combined.clause.end(), [&](sat::Lit literal) {
              const auto value = backend.value(std::abs(literal.value));
              return value && *value != (literal.value > 0);
            }))
          record_theory_conflict(statistics);
        backend.add_clause(combined.clause);
        continue;
      case ModelBasedCombinationStatus::PropagateEqualities: {
        // Snapshot every model-dependent fact before materialize() can add
        // clauses/variables and invalidate CaDiCaL's last-model query state.
        std::vector<bool> conflicts;
        conflicts.reserve(combined.equalities.size());
        for (const auto& equality : combined.equalities) {
          for (const auto reason : equality.reasons) {
            const int var = std::abs(reason.value);
            const int actual = dispatcher ? dispatcher->assignment_view().literal(var)
                                          : completed_assignment.literal(var);
            if (actual != reason.value) {
              return {CheckSatResult::Unknown, "equality antecedent is not currently true",
                      statistics, "", InconclusiveReason::Unknown};
            }
          }
          const PairKey key{equality.pair.sort, std::min(equality.pair.lhs, equality.pair.rhs),
                            std::max(equality.pair.lhs, equality.pair.rhs)};
          const auto known = registered.find(key);
          if (!recipe.learn_exported_equalities && known != registered.end()) {
            throw std::logic_error("naming-only equality batch contains an existing pair");
          }
          const int literal = known == registered.end() ? 0 : known->second.equality_lit.value;
          const int actual = literal == 0 ? 0
                             : dispatcher ? dispatcher->assignment_view().literal(std::abs(literal))
                                          : completed_assignment.literal(std::abs(literal));
          conflicts.push_back(literal != 0 && actual == -literal);
        }
        detach_dispatcher();
        statistics.increment(":combination.equality.batches");
        for (std::size_t index = 0; index < combined.equalities.size(); ++index) {
          const auto& equality = combined.equalities[index];
          const auto before = registered.size();
          const auto literal = materialize(equality.pair);
          if (!literal.has_value()) {
            throw std::logic_error("validated equality could not be materialized");
          }
          statistics.increment(":combination.equality.fresh-pairs", registered.size() - before);
          if (!recipe.learn_exported_equalities) {
            statistics.increment(":combination.equality.named-without-proof");
            continue;
          }
          sat::Clause clause{*literal};
          for (const auto reason : equality.reasons) clause.push_back(sat::Lit{-reason.value});
          backend.add_clause(clause);
          statistics.increment(":combination.equality.clauses");
          if (conflicts[index]) {
            statistics.increment(":combination.equality.conflicts");
            statistics.increment(util::StatKey::TheoryConflicts);
            record_theory_conflict(statistics);
          }
        }
        install_atoms();
        continue;
      }
      case ModelBasedCombinationStatus::DecideEquality:
        ++decision_batches;
        active_choices.insert(active_choices.end(), combined.decision_lits.begin(),
                              combined.decision_lits.end());
        continue;
      case ModelBasedCombinationStatus::FreshDecision:
        if (const auto literal = materialize(combined.decision.pair); literal.has_value()) {
          active_choices.push_back(combined.decision.preferred_value ==
                                           theory::InterfaceRelationValue::Equal
                                       ? *literal
                                       : sat::Lit{-literal->value});
          ++decision_batches;
          continue;
        }
        [[fallthrough]];
      case ModelBasedCombinationStatus::IntegerSplit:
      case ModelBasedCombinationStatus::Unsupported:
        if (guided) return fallback(":combination.model.fallback-unsupported");
        return {CheckSatResult::Unsupported, combined.diagnostic, statistics, "",
                InconclusiveReason::Unsupported};
      case ModelBasedCombinationStatus::Unknown:
        if (guided) return fallback(":combination.model.fallback-unknown");
        return {CheckSatResult::Unknown, combined.diagnostic, statistics, "",
                InconclusiveReason::Unknown};
      case ModelBasedCombinationStatus::Cancelled:
        return {CheckSatResult::Unknown, "cancelled", statistics, "",
                InconclusiveReason::Cancelled};
      case ModelBasedCombinationStatus::ResourceLimit:
        if (guided) return fallback(":combination.model.fallback-resource-limit");
        return {CheckSatResult::Unknown, combined.diagnostic, statistics, "",
                InconclusiveReason::ResourceLimit};
    }
    CheckSatOutcome outcome{CheckSatResult::Sat, "", statistics, ""};
    if (options.produce_model) {
      std::vector<theory::ComponentModelFragment> fragments;
      for (std::size_t index = 0; index < components.size(); ++index) {
        auto fragment = components[index]->export_model(coordinator.model_tokens()[index]);
        validate_component_fragment(terms, plan.descriptors()[index], plan.component_scopes(index),
                                    coordinator.model_tokens()[index],
                                    guided ? &coordinator.model_projections()[index] : nullptr,
                                    arrangement, fragment);
        fragments.push_back(std::move(fragment));
      }
      std::size_t completed = 0;
      std::vector<theory::ComponentModelFact> propositional_facts;
      propositional_facts.reserve(propositional_atoms.size());
      for (const auto& atom : propositional_atoms) {
        const auto value = backend.value(std::abs(atom.lit.value));
        propositional_facts.push_back({atom.term, value.value_or(false) == (atom.lit.value > 0)});
      }
      outcome.typed_model = assemble_component_model(terms, assertions, fragments, conversions,
                                                     &completed, &statistics, propositional_facts);
      statistics.set(util::StatKey::ModelInventedIntValues, completed);
      outcome.statistics = statistics;
    }
    return outcome;
  }
} catch (const UnsupportedCombination& error) {
  return {CheckSatResult::Unsupported, error.what(), statistics, "",
          InconclusiveReason::Unsupported};
}

}  // namespace primo::smt
