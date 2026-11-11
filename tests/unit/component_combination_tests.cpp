#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/assignment_coordinator.hpp"
#include "smt/component_dispatcher.hpp"
#include "smt/component_model_coordinator.hpp"
#include "smt/component_solver.hpp"
#include "smt/theory_combination_plan.hpp"
#include "theory/theory_component.hpp"

namespace {

using namespace primo;
using Partition = std::vector<std::size_t>;

// Restricted-growth strings enumerate equivalence relations, not a fixed-size
// universe. Each allowed partition extends to an infinite carrier. This oracle
// does not use the coordinator's spanning edges or any Primo theory kernel.
std::vector<Partition> partitions(std::size_t size) {
  std::vector<Partition> result;
  Partition current(size, 0);
  const std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t position,
                                                                      std::size_t maximum) {
    if (position == size) {
      result.push_back(current);
      return;
    }
    for (std::size_t value = 0; value <= maximum + 1; ++value) {
      current[position] = value;
      enumerate(position + 1, std::max(maximum, value));
    }
  };
  if (size == 0) {
    result.emplace_back();
  } else {
    enumerate(1, 0);
  }
  return result;
}

struct Restriction {
  std::vector<core::TermId> terms;
  std::vector<Partition> permitted;

  bool accepts(const std::map<core::TermId, std::size_t>& values) const {
    return std::any_of(permitted.begin(), permitted.end(), [&](const auto& pattern) {
      for (std::size_t lhs = 0; lhs < terms.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < terms.size(); ++rhs) {
          if ((values.at(terms[lhs]) == values.at(terms[rhs])) != (pattern[lhs] == pattern[rhs])) {
            return false;
          }
        }
      }
      return true;
    });
  }
};

class PartitionComponent final : public theory::TheoryComponent {
 public:
  PartitionComponent(core::TermManager& terms, std::string id, core::TermId marker,
                     std::vector<Restriction> restrictions)
      : terms_(terms), marker_(marker), restrictions_(std::move(restrictions)) {
    properties.id = std::move(id);
    properties.fragment = "independent finite boundary-partition oracle over infinite carriers";
    properties.capabilities.model_projection = true;
    properties.capabilities.model_export = true;
    properties.capabilities.incremental = true;
    properties.capabilities.interface_extension = true;
    for (const auto& restriction : restrictions_) {
      const auto sort = terms_.get(restriction.terms.front()).sort;
      if (std::none_of(properties.shared_sorts.begin(), properties.shared_sorts.end(),
                       [&](const auto& declared) { return declared.sort == sort; })) {
        properties.shared_sorts.push_back({sort, theory::StableInfiniteness::Yes, false});
      }
    }
  }
  const theory::ComponentDescriptor& descriptor() const override { return properties; }
  bool owns_atom(core::TermId atom) const override {
    return atom == marker_ ||
           (owns_equalities && terms_.get(atom).kind == core::TermKind::Equality);
  }
  std::vector<core::TermId> interface_variables(core::TermId atom) const override {
    if (atom != marker_) {
      assert(owns_equalities && terms_.get(atom).kind == core::TermKind::Equality);
      const auto& children = terms_.get(atom).children;
      return {children[0], children[1]};
    }
    std::vector<core::TermId> result;
    for (const auto& restriction : restrictions_) {
      result.insert(result.end(), restriction.terms.begin(), restriction.terms.end());
    }
    return result;
  }
  void register_interface(std::span<const theory::ComponentInterfaceScope> scopes) override {
    scopes_.assign(scopes.begin(), scopes.end());
  }
  void register_atoms(std::span<const theory::ComponentAtom>) override {}
  core::TermId encode_equality(const theory::InterfacePair& pair) override {
    return terms_.mk_eq(pair.lhs, pair.rhs);
  }
  void reset() override {
    trail_.clear();
    levels_.clear();
  }
  void push_level() override { levels_.push_back(trail_.size()); }
  void pop_to_level(std::size_t level) override {
    assert(level <= levels_.size());
    if (level < levels_.size()) {
      trail_.resize(levels_[level]);
      levels_.resize(level);
    }
  }
  void assign(std::span<const theory::AssignedTheoryLiteral> delta) override {
    ++assignment_batches;
    trail_.insert(trail_.end(), delta.begin(), delta.end());
  }
  const std::vector<theory::AssignedTheoryLiteral>& assignments() const { return trail_; }
  std::optional<theory::TheoryCheckResult> propagate() override {
    return std::exchange(pending_propagation, std::nullopt);
  }
  theory::TheoryCheckResult check(theory::TheoryCheckEffort effort, std::uint64_t epoch,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    if (effort == theory::TheoryCheckEffort::FullModel && pending_equalities.has_value()) {
      return std::exchange(pending_equalities, std::nullopt).value();
    }
    ++checks;
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::Consistent;
    result.model.token = {properties.id, epoch, ++revision_};
    for (const auto& scope : scopes_) {
      bool found = false;
      for (const auto& partition : partitions(scope.terms.size())) {
        std::map<core::TermId, std::size_t> values;
        for (std::size_t index = 0; index < scope.terms.size(); ++index) {
          values.emplace(scope.terms[index], partition[index]);
        }
        bool allowed =
            std::all_of(restrictions_.begin(), restrictions_.end(), [&](const auto& rule) {
              return terms_.get(rule.terms.front()).sort != scope.sort || rule.accepts(values);
            });
        for (const auto& relation : arrangement) {
          if (relation.sort == scope.sort) {
            allowed = allowed && ((values.at(relation.lhs) == values.at(relation.rhs)) ==
                                  (relation.value == theory::InterfaceRelationValue::Equal));
          }
        }
        if (!allowed) {
          continue;
        }
        theory::SharedModelPartition projected;
        projected.sort = scope.sort;
        for (std::size_t index = 0; index < partition.size(); ++index) {
          projected.equality_classes.resize(
              std::max(projected.equality_classes.size(), partition[index] + 1));
          projected.equality_classes[partition[index]].terms.push_back(scope.terms[index]);
        }
        result.model.partitions.push_back(std::move(projected));
        found = true;
        break;
      }
      if (!found) {
        result.status = theory::TheoryCheckStatus::LearnedClause;
        for (const auto& assigned : trail_) {
          result.clause.push_back(
              sat::Lit{assigned.value ? -assigned.lit.value : assigned.lit.value});
        }
        for (const auto& relation : arrangement) {
          result.clause.push_back(sat::Lit{relation.value == theory::InterfaceRelationValue::Equal
                                               ? -relation.equality_lit.value
                                               : relation.equality_lit.value});
        }
        return result;
      }
    }
    if (corrupt) {
      corrupt(result);
    }
    last_model_ = result.model;
    return result;
  }
  theory::TheoryMutationResult mutate(const theory::TheoryMutationRequest& request) override {
    ++mutations;
    return mutation ? mutation(request) : theory::TheoryMutationResult{};
  }
  std::vector<theory::InterfacePair> care_pairs(const theory::TheoryModelToken&) const override {
    ++care_calls;
    return suggested_pairs;
  }
  theory::ComponentModelFragment export_model(
      const theory::TheoryModelToken& token) const override {
    assert(token == last_model_.token);
    theory::ComponentModelFragment fragment{token, {}, {}, {}};
    for (const auto& assigned : trail_) fragment.facts.push_back({assigned.atom, assigned.value});
    for (const auto& partition : last_model_.partitions) {
      for (std::size_t index = 0; index < partition.equality_classes.size(); ++index) {
        for (const auto term : partition.equality_classes[index].terms) {
          fragment.bindings.push_back({term, {partition.sort, core::DomainValue{index}}});
        }
      }
    }
    return fragment;
  }

  theory::ComponentDescriptor properties;
  bool owns_equalities = false;
  std::function<void(theory::TheoryCheckResult&)> corrupt;
  std::function<theory::TheoryMutationResult(const theory::TheoryMutationRequest&)> mutation;
  std::size_t checks = 0;
  std::size_t assignment_batches = 0;
  std::size_t mutations = 0;
  mutable std::size_t care_calls = 0;
  std::vector<theory::InterfacePair> suggested_pairs;
  std::optional<theory::TheoryCheckResult> pending_propagation;
  std::optional<theory::TheoryCheckResult> pending_equalities;

 private:
  core::TermManager& terms_;
  core::TermId marker_;
  std::vector<Restriction> restrictions_;
  std::vector<theory::ComponentInterfaceScope> scopes_;
  std::vector<theory::AssignedTheoryLiteral> trail_;
  std::vector<std::size_t> levels_;
  std::uint64_t revision_ = 0;
  theory::TheoryModelProjection last_model_;
};

struct Fixture {
  core::SymbolTable symbols;
  core::TermManager terms{symbols};
  std::vector<std::unique_ptr<PartitionComponent>> owned;
  std::vector<theory::TheoryComponent*> components;
  std::vector<theory::ComponentAtom> atoms;

  core::TermId variable(const std::string& name, core::SortId sort) {
    return terms.mk_constant(symbols.declare_constant(name, sort));
  }
  PartitionComponent& add(std::vector<Restriction> rules) {
    const auto id = "component" + std::to_string(owned.size());
    const auto marker = variable(id, symbols.bool_sort());
    owned.push_back(std::make_unique<PartitionComponent>(terms, id, marker, std::move(rules)));
    components.push_back(owned.back().get());
    atoms.push_back({marker, sat::Lit{static_cast<int>(atoms.size() + 1)}});
    return *owned.back();
  }
};

template <typename Action>
void requires_unsupported(Action action) {
  bool rejected = false;
  try {
    action();
  } catch (const smt::UnsupportedCombination&) {
    rejected = true;
  }
  assert(rejected);
}

void admission_and_normalization() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  const auto& any_pair = partitions(2);
  f.add({{{x, y}, any_pair}});
  f.add({{{y, z}, any_pair}});
  f.add({{{x, z}, any_pair}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  assert(plan.scopes().size() == 1);
  assert(plan.scopes()[0].terms == std::vector<core::TermId>({x, y, z}));
  for (std::size_t index = 0; index < 3; ++index) {
    assert(plan.component_scopes(index)[0].terms == plan.scopes()[0].terms);
    assert(plan.owner(f.atoms[index].term) == index);
  }
  assert(!plan.convex_complete_propagation());
  assert(plan.scope_for({z, x, sort}) == 0);
  assert(!plan.scope_for({x, y, f.symbols.real_sort()}).has_value());
  f.owned[1]->properties.shared_sorts[0].stable_infiniteness = theory::StableInfiniteness::No;
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
  f.owned[1]->properties.shared_sorts[0].stable_infiniteness = theory::StableInfiniteness::Yes;
  f.owned[1]->properties.capabilities.interface_extension = false;
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
  f.owned[1]->properties.capabilities.interface_extension = true;
  f.owned[1]->properties.capabilities.model_projection = false;
  smt::TheoryCombinationPlan complete(f.terms, f.components, f.atoms, true);
  assert(complete.policy() == smt::CombinationPolicy::CompleteArrangement);
  f.owned[0]->properties.signature_families = {"test-signature"};
  f.owned[1]->properties.signature_families = {"test-signature"};
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
}

void sort_nonparticipants_and_disconnected_groups() {
  Fixture f;
  const auto int_sort = f.symbols.int_sort();
  const auto real_sort = f.symbols.real_sort();
  const auto x = f.variable("x", int_sort);
  const auto y = f.variable("y", int_sort);
  const auto u = f.variable("u", real_sort);
  const auto v = f.variable("v", real_sort);
  f.add({{{x, y}, {{0, 0}}}});
  f.add({{{x, y}, {{0, 0}}}, {{u, v}, {{0, 0}}}});
  f.add({{{u, v}, {{0, 0}}}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  std::size_t decisions = 0;
  const auto result = coordinator.reconcile(plan, 7, f.components, {}, [&](const auto&) {
    ++decisions;
    return sat::Lit{42};
  });
  assert(result.status == smt::ModelBasedCombinationStatus::Accept);
  assert(decisions == 0);
  assert(plan.component_scopes(0).size() == 1);
  assert(plan.component_scopes(1).size() == 2);
  assert(plan.component_scopes(2).size() == 1);
  const auto a = f.variable("a", int_sort);
  const auto b = f.variable("b", int_sort);
  f.add({{{a, b}, {{0, 1}}}});
  f.add({{{a, b}, {{0, 1}}}});
  smt::TheoryCombinationPlan disconnected(f.terms, f.components, f.atoms, true);
  assert(disconnected.scopes().size() == 3);
  assert(!disconnected.scope_for({x, a, int_sort}).has_value());
}

bool solve_with_sat(Fixture& f, bool guided) {
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, guided);
  plan.register_interfaces(f.components);
  sat::CadicalBackend backend;
  for (std::size_t index = 0; index < f.atoms.size(); ++index) {
    assert(backend.new_var() == f.atoms[index].lit.value);
    backend.add_clause({f.atoms[index].lit});
    const std::vector delta{
        theory::AssignedTheoryLiteral{f.atoms[index].term, f.atoms[index].lit, true}};
    f.components[plan.owner(f.atoms[index].term)]->reset();
    f.components[plan.owner(f.atoms[index].term)]->assign(delta);
  }
  std::map<std::pair<core::TermId, core::TermId>, theory::InterfaceRelation> registered;
  const auto materialize = [&](const theory::InterfacePair& pair) {
    const auto key = std::minmax(pair.lhs, pair.rhs);
    const auto found = registered.find(key);
    if (found != registered.end()) {
      return found->second.equality_lit;
    }
    const sat::Lit literal{backend.new_var()};
    backend.add_clause({literal, sat::Lit{-literal.value}});
    registered.emplace(key, theory::InterfaceRelation{pair.lhs, pair.rhs, pair.sort, literal,
                                                      theory::InterfaceRelationValue::Equal});
    return literal;
  };
  if (!guided) {
    for (const auto& scope : plan.scopes()) {
      for (std::size_t lhs = 0; lhs < scope.terms.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < scope.terms.size(); ++rhs) {
          materialize({scope.terms[lhs], scope.terms[rhs], scope.sort});
        }
      }
    }
  }
  smt::ComponentModelCoordinator coordinator;
  for (std::uint64_t epoch = 1; epoch < 100; ++epoch) {
    const auto sat_result = backend.solve();
    if (sat_result == sat::SatResult::Unsat) {
      return false;
    }
    assert(sat_result == sat::SatResult::Sat);
    std::vector<theory::InterfaceRelation> arrangement;
    for (const auto& [key, relation] : registered) {
      static_cast<void>(key);
      auto assigned = relation;
      const auto value = backend.value(relation.equality_lit.value);
      assert(value.has_value());
      assigned.value =
          *value ? theory::InterfaceRelationValue::Equal : theory::InterfaceRelationValue::Disequal;
      arrangement.push_back(assigned);
    }
    const auto result = coordinator.reconcile(plan, epoch, f.components, arrangement, materialize);
    switch (result.status) {
      case smt::ModelBasedCombinationStatus::Accept:
        return true;
      case smt::ModelBasedCombinationStatus::LearnClause:
        backend.add_clause(result.clause);
        break;
      case smt::ModelBasedCombinationStatus::DecideEquality:
        for (const auto literal : result.decision_lits) {
          backend.phase(literal);
        }
        break;
      default:
        assert(false && "unexpected coordinator failure");
    }
  }
  assert(false && "combination did not terminate");
  return false;
}

void exhaustive_three_and_four_component_sat() {
  for (const bool guided : {false, true}) {
    for (const std::size_t count : {3U, 4U}) {
      // Every combination of equality-only, disequality-only and unrestricted
      // binary components on a cycle, in both recipe directions.
      std::size_t cases = 1;
      for (std::size_t index = 0; index < count; ++index) {
        cases *= 3;
      }
      for (std::size_t encoded = 0; encoded < cases; ++encoded) {
        Fixture f;
        const auto boundary_sort = f.symbols.declare_sort("Boundary");
        std::vector<core::TermId> variables;
        for (std::size_t index = 0; index < count; ++index) {
          variables.push_back(f.variable("x" + std::to_string(index), boundary_sort));
        }
        std::vector<Restriction> restrictions;
        auto cursor = encoded;
        for (std::size_t index = 0; index < count; ++index) {
          const auto choice = cursor % 3;
          cursor /= 3;
          Restriction rule{{variables[index], variables[(index + 1) % count]}, {}};
          if (choice != 1) {
            rule.permitted.push_back({0, 0});
          }
          if (choice != 0) {
            rule.permitted.push_back({0, 1});
          }
          restrictions.push_back(rule);
          f.add({rule});
        }
        bool expected = false;
        for (const auto& partition : partitions(count)) {
          std::map<core::TermId, std::size_t> values;
          for (std::size_t index = 0; index < count; ++index) {
            values.emplace(variables[index], partition[index]);
          }
          expected =
              expected || std::all_of(restrictions.begin(), restrictions.end(),
                                      [&](const auto& rule) { return rule.accepts(values); });
        }
        assert(solve_with_sat(f, guided) == expected);
        std::reverse(f.components.begin(), f.components.end());
        assert(solve_with_sat(f, guided) == expected);

        // Exercise the actual collection driver, clausification, sparse growth,
        // assumption blocking and callback dispatcher, not just its coordinator.
        for (const bool online : {false, true}) {
          smt::CombinationRecipe recipe;
          recipe.online = online;
          recipe.model_guided = guided;
          recipe.decision_budget = 100;
          recipe.timing_prefix = ":time.partition-test.";
          std::vector<core::TermId> assertions;
          for (std::size_t index = 0; index < count; ++index) {
            assertions.push_back(f.atoms[index].term);
            recipe.factories.push_back([&, index](bool) {
              auto component = std::make_unique<PartitionComponent>(
                  f.terms, "component" + std::to_string(index), f.atoms[index].term,
                  std::vector{restrictions[index]});
              component->owns_equalities = index == 0;
              return component;
            });
            recipe.online_order.push_back(count - index - 1);
          }
          smt::CheckSatOptions options;
          options.produce_model = true;
          util::Statistics statistics;
          const auto outcome =
              smt::solve_component_recipe(f.terms, assertions, {}, options, statistics, recipe);
          assert(outcome.result ==
                 (expected ? smt::CheckSatResult::Sat : smt::CheckSatResult::Unsat));
          assert(statistics.get(":combination.components") == count);
          assert(!expected || outcome.typed_model != nullptr);
        }
      }
    }
  }
}

void malformed_models_are_not_accepted_or_redecided() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  f.add({{{x, y}, {{0, 0}}}});
  f.add({{{x, y}, {{0, 0}}}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  const auto resolver = [](const auto&) { return sat::Lit{42}; };
  for (const int fault : {0, 1, 2}) {
    f.owned[1]->corrupt = [fault](auto& checked) {
      if (fault == 0) {
        --checked.model.token.assignment_epoch;
      } else if (fault == 1) {
        checked.model.partitions[0].equality_classes[0].terms.pop_back();
      } else {
        checked.model.token.component = "wrong-component";
      }
    };
    assert(coordinator.reconcile(plan, 7, f.components, {}, resolver).status ==
           smt::ModelBasedCombinationStatus::Unknown);
  }
  f.owned[1]->corrupt = [](auto& checked) {
    checked.status = theory::TheoryCheckStatus::FreshDecision;
    const auto& projection = checked.model.partitions[0];
    const auto& terms = projection.equality_classes[0].terms;
    checked.decision.pair = {terms[0], terms[1], projection.sort};
  };
  const std::vector arrangement{
      theory::InterfaceRelation{x, y, sort, sat::Lit{42}, theory::InterfaceRelationValue::Equal}};
  assert(coordinator.reconcile(plan, 7, f.components, arrangement, resolver).status ==
         smt::ModelBasedCombinationStatus::Unknown);
  assert(f.owned[0]->mutations == 0 && f.owned[1]->mutations == 0);
  assert(f.owned[0]->care_calls == 0 && f.owned[1]->care_calls == 0);
}

void incremental_routing_backtracking_and_explanation_lifetime() {
  Fixture f;
  const auto x = f.variable("x", f.symbols.int_sort());
  const auto y = f.variable("y", f.symbols.int_sort());
  for (std::size_t index = 0; index < 4; ++index) {
    f.add({{{x, y}, partitions(2)}});
  }
  smt::ComponentDispatcher dispatcher(f.components);
  for (std::size_t index = 0; index < f.atoms.size(); ++index) {
    dispatcher.register_atoms(index, std::span(&f.atoms[index], 1));
  }
  smt::AssignmentCoordinator owner(dispatcher);
  bool missing_notifications_rejected = false;
  try {
    static_cast<void>(owner.cb_check_found_model({1, 2, 3, 4}));
  } catch (const std::logic_error&) {
    missing_notifications_rejected = true;
  }
  assert(missing_notifications_rejected);
  assert(owner.view().size() == 0);
  owner.notify_assignment({1, 3, 99, 1});
  assert(f.owned[0]->assignments().size() == 1);
  assert(f.owned[1]->assignments().empty());
  assert(f.owned[2]->assignments().size() == 1);
  assert(f.owned[3]->assignments().empty());
  assert(dispatcher.statistics().routed_literals == 2);
  assert(dispatcher.statistics().assignment_batches == 2);
  assert(!dispatcher.cb_has_external_clause());
  assert(f.owned[0]->checks == 1 && f.owned[2]->checks == 1);
  assert(!dispatcher.cb_has_external_clause());
  assert(f.owned[0]->checks == 1 && f.owned[2]->checks == 1);

  theory::TheoryCheckResult consequence;
  consequence.status = theory::TheoryCheckStatus::EntailedLiteral;
  consequence.entailed_lit = sat::Lit{2};
  consequence.reasons = {sat::Lit{1}};
  f.owned[1]->pending_propagation = consequence;
  assert(dispatcher.cb_propagate() == 2);
  owner.notify_assignment({2});
  owner.notify_new_decision_level();
  owner.notify_assignment({-4});
  owner.notify_new_decision_level();
  owner.notify_backtrack(0);
  assert(f.owned[3]->assignments().empty());
  assert(f.owned[1]->assignments().size() == 1);
  // The root propagation's explanation survives unrelated non-root undo.
  assert(dispatcher.cb_add_reason_clause_lit(2) == 2);
  assert(dispatcher.cb_add_reason_clause_lit(2) == -1);
  assert(dispatcher.cb_add_reason_clause_lit(2) == 0);
  owner.notify_new_decision_level();
  owner.notify_assignment({4});
  assert(f.owned[3]->assignments().size() == 1);
  assert(f.owned[3]->assignments()[0].value);
  owner.notify_backtrack(0);

  f.owned[0]->corrupt = [](auto& result) {
    result.status = theory::TheoryCheckStatus::Cancelled;
    result.diagnostic = "test cancellation";
  };
  assert(!dispatcher.cb_has_external_clause());
  assert(dispatcher.failure()->status == theory::TheoryCheckStatus::Cancelled);
  assert(dispatcher.cb_propagate() == 0);
}

void external_clause_retention_follows_recipe_policy() {
  assert(smt::CombinationRecipe{}.forgettable_clauses);
  for (const bool forgettable : {false, true}) {
    Fixture f;
    const auto x = f.variable("x", f.symbols.int_sort());
    const auto y = f.variable("y", f.symbols.int_sort());
    f.add({{{x, y}, partitions(2)}});
    smt::ComponentDispatcher dispatcher(f.components, {}, {}, forgettable);
    dispatcher.register_atoms(0, std::span(&f.atoms[0], 1));
    smt::AssignmentCoordinator owner(dispatcher);
    owner.notify_assignment({1});
    f.owned[0]->corrupt = [](auto& result) {
      result.status = theory::TheoryCheckStatus::LearnedClause;
      result.clause = {sat::Lit{-1}};
    };
    bool observed = !forgettable;
    assert(dispatcher.cb_has_external_clause(observed));
    assert(observed == forgettable);
    assert(dispatcher.cb_add_external_clause_lit() == -1);
    assert(dispatcher.cb_add_external_clause_lit() == 0);
  }
}

void conflict_reports_are_counted_once_and_propagations_are_excluded() {
  for (const bool conflicting : {false, true}) {
    Fixture f;
    const auto x = f.variable("x", f.symbols.int_sort());
    const auto y = f.variable("y", f.symbols.int_sort());
    f.add({{{x, y}, partitions(2)}});
    std::size_t conflicts = 0;
    smt::ComponentDispatcher dispatcher(f.components, {}, [&] {
      ++conflicts;
      return true;
    });
    dispatcher.register_atoms(0, std::span(&f.atoms[0], 1));
    smt::AssignmentCoordinator owner(dispatcher);
    owner.notify_assignment({1});
    f.owned[0]->corrupt = [conflicting](auto& result) {
      result.status = theory::TheoryCheckStatus::LearnedClause;
      result.clause = {sat::Lit{conflicting ? -1 : 2}};
    };
    assert(dispatcher.cb_has_external_clause());
    assert(dispatcher.cb_has_external_clause());
    assert(conflicts == (conflicting ? 1 : 0));
    assert(dispatcher.cb_add_external_clause_lit() == (conflicting ? -1 : 2));
    assert(dispatcher.cb_add_external_clause_lit() == 0);
    assert(conflicts == (conflicting ? 1 : 0));
    if (conflicting) {
      assert(dispatcher.failure()->status == theory::TheoryCheckStatus::ResourceLimit);
      assert(!dispatcher.cb_has_external_clause());
      assert(dispatcher.cb_propagate() == 0);
      assert(conflicts == 1);
    }
  }
}

void nonconvex_disjunction_and_mutation_side_effects() {
  for (const bool guided : {false, true}) {
    for (const bool contradiction : {false, true}) {
      Fixture f;
      const auto sort = f.symbols.int_sort();
      const auto x = f.variable("x", sort);
      const auto y = f.variable("y", sort);
      const auto z = f.variable("z", sort);
      // Exactly (x=y or y=z) and x!=z: neither equality alone is entailed.
      f.add({{{x, y, z}, {{0, 0, 1}, {0, 1, 1}}}});
      f.add({{{x, y}, {{0, 1}}}});
      f.add({{{y, z}, contradiction ? std::vector<Partition>{{0, 1}} : partitions(2)}});
      assert(solve_with_sat(f, guided) == !contradiction);
    }
  }
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  f.add({{{x, y, z}, {{0, 1, 2}}}});
  auto& changing = f.add({{{x, y, z}, partitions(3)}});
  changing.properties.capabilities.model_mutation = true;
  changing.mutation = [&](const auto& request) {
    theory::TheoryMutationResult result;
    result.status = theory::TheoryMutationStatus::Mutated;
    result.model.token = request.source;
    ++result.model.token.revision;
    // Satisfy x!=y and x!=z but introduce y=z, absent from the old spanning
    // edges. Acceptance must compare the newly produced partitions too.
    result.model.partitions = {{sort, {{{x}}, {{y, z}}}}};
    return result;
  };
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  std::vector<theory::InterfacePair> decisions;
  auto result = coordinator.reconcile(plan, 1, f.components, {}, [&](const auto& pair) {
    decisions.push_back(pair);
    return sat::Lit{static_cast<int>(decisions.size() + 10)};
  });
  assert(result.status == smt::ModelBasedCombinationStatus::DecideEquality);
  assert(decisions.size() == 1);
  assert(std::find(decisions.begin(), decisions.end(), theory::InterfacePair{y, z, sort}) !=
         decisions.end());
  const std::vector arrangement{theory::InterfaceRelation{
      y, z, sort, sat::Lit{42}, theory::InterfaceRelationValue::Disequal}};
  result = coordinator.reconcile(plan, 2, f.components, arrangement,
                                 [](const auto&) { return sat::Lit{42}; });
  assert(result.status == smt::ModelBasedCombinationStatus::Unknown);
  assert(coordinator.statistics().rejected_mutations == 1);
  changing.mutation = [](const auto& request) {
    theory::TheoryMutationResult cancelled;
    cancelled.status = theory::TheoryMutationStatus::Cancelled;
    cancelled.model.token = request.source;
    return cancelled;
  };
  assert(coordinator.reconcile(plan, 3, f.components, {}, [](const auto&) { return sat::Lit{42}; })
             .status == smt::ModelBasedCombinationStatus::Cancelled);
}

void care_selection_is_linear_and_validates_late_entries() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  const auto foreign = f.variable("foreign", sort);
  auto& first = f.add({{{x, y, z}, {{0, 0, 0}}}});
  f.add({{{x, y, z}, {{0, 0, 1}}}});
  f.add({{{x, y, z}, {{0, 0, 0}}}});
  first.properties.capabilities.care_pairs = true;
  first.suggested_pairs.assign(50000, {y, x, sort});
  first.suggested_pairs.push_back({z, y, sort});
  first.suggested_pairs.push_back({y, z, sort});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  std::vector<theory::InterfacePair> choices;
  const auto resolve = [&](const auto& pair) {
    choices.push_back(pair);
    return sat::Lit{42};
  };
  auto result = coordinator.reconcile(plan, 1, f.components, {}, resolve);
  assert(result.status == smt::ModelBasedCombinationStatus::DecideEquality);
  assert(choices == std::vector<theory::InterfacePair>({{y, z, sort}}));
  assert(coordinator.statistics().care_pairs == 50002);
  assert(coordinator.statistics().care_pair_agreements == 50000);
  // Care priority skips even the lexicographically smaller x=z spanning edge.
  assert(coordinator.statistics().spanning_pair_checks == 0);

  first.suggested_pairs.push_back({z, x, sort});
  choices.clear();
  result = coordinator.reconcile(plan, 2, f.components, {}, resolve);
  assert(result.status == smt::ModelBasedCombinationStatus::DecideEquality);
  assert(choices == std::vector<theory::InterfacePair>({{x, z, sort}}));

  first.suggested_pairs = {{y, x, sort}};
  choices.clear();
  result = coordinator.reconcile(plan, 3, f.components, {}, resolve);
  assert(result.status == smt::ModelBasedCombinationStatus::DecideEquality);
  assert(choices == std::vector<theory::InterfacePair>({{x, z, sort}}));
  assert(coordinator.statistics().spanning_pair_checks > 0);

  // Finding a good candidate must not hide an invalid later care entry.
  first.suggested_pairs = {{x, z, sort}, {x, foreign, sort}};
  choices.clear();
  result = coordinator.reconcile(plan, 4, f.components, {}, resolve);
  assert(result.status == smt::ModelBasedCombinationStatus::Unknown);
  assert(choices.empty());
}

void exhaustive_ordinary_choice_order() {
  const auto patterns = partitions(3);
  using Edge = std::pair<std::size_t, std::size_t>;
  const std::vector<std::vector<Edge>> suggestions{{}, {{1, 2}}, {{1, 2}, {0, 2}, {0, 1}}};
  for (const std::size_t count : {2U, 3U, 4U}) {
    std::size_t combinations = 1;
    for (std::size_t index = 0; index < count; ++index) combinations *= patterns.size();
    for (std::size_t code = 0; code < combinations; ++code) {
      for (const auto& care : suggestions) {
        Fixture f;
        const auto sort = f.symbols.int_sort();
        const std::vector terms{f.variable("x", sort), f.variable("y", sort),
                                f.variable("z", sort)};
        std::vector<Partition> models;
        std::vector<Edge> ordered = care;
        auto remaining = code;
        for (std::size_t index = 0; index < count; ++index) {
          models.push_back(patterns[remaining % patterns.size()]);
          remaining /= patterns.size();
          f.add({{terms, {models.back()}}});
          // Test-only materialization of the previous sorted-union policy.
          std::map<std::size_t, std::size_t> representatives;
          for (std::size_t term = 0; term < terms.size(); ++term) {
            const auto [entry, inserted] = representatives.emplace(models.back()[term], term);
            if (!inserted) ordered.emplace_back(entry->second, term);
          }
        }
        f.owned.back()->properties.capabilities.care_pairs = true;
        for (const auto& [lhs, rhs] : care) {
          f.owned.back()->suggested_pairs.push_back({terms[rhs], terms[lhs], sort});
          f.owned.back()->suggested_pairs.push_back({terms[lhs], terms[rhs], sort});
        }
        std::sort(ordered.begin(), ordered.end());
        ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
        std::stable_partition(ordered.begin(), ordered.end(), [&](const auto& pair) {
          return std::find(care.begin(), care.end(), pair) != care.end();
        });
        std::optional<theory::InterfacePair> expected;
        for (const auto& [lhs, rhs] : ordered) {
          const bool equal = models.front()[lhs] == models.front()[rhs];
          if (std::any_of(models.begin(), models.end(),
                          [&](const auto& model) { return (model[lhs] == model[rhs]) != equal; })) {
            expected = {terms[lhs], terms[rhs], sort};
            break;
          }
        }
        smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
        plan.register_interfaces(f.components);
        smt::ComponentModelCoordinator coordinator;
        std::optional<theory::InterfacePair> actual;
        const auto result = coordinator.reconcile(plan, 1, f.components, {}, [&](const auto& pair) {
          assert(!actual.has_value());
          actual = pair;
          return sat::Lit{42};
        });
        assert(actual == expected);
        assert(result.status == (expected.has_value()
                                     ? smt::ModelBasedCombinationStatus::DecideEquality
                                     : smt::ModelBasedCombinationStatus::Accept));
      }
    }
  }
}

// A scripted source of lemma batches standing in for an instantiation module:
// it owns at most one marker atom and does no theory reasoning of its own.
class LemmaComponent final : public theory::TheoryComponent {
 public:
  LemmaComponent(core::TermId marker, std::vector<std::vector<core::TermId>> batches,
                 bool check_on_full_model, std::size_t& checks)
      : marker_(marker), batches_(std::move(batches)), checks_(checks) {
    properties_.id = "lemmas";
    properties_.fragment = "scripted lemma source";
    properties_.capabilities.incremental = true;
    properties_.capabilities.check_on_full_model = check_on_full_model;
  }
  const theory::ComponentDescriptor& descriptor() const override { return properties_; }
  bool owns_atom(core::TermId atom) const override { return atom == marker_; }
  std::vector<core::TermId> interface_variables(core::TermId) const override { return {}; }
  void register_interface(std::span<const theory::ComponentInterfaceScope>) override {}
  void register_atoms(std::span<const theory::ComponentAtom>) override {}
  core::TermId encode_equality(const theory::InterfacePair&) override {
    return core::kInvalidTermId;
  }
  void reset() override {}
  void push_level() override {}
  void pop_to_level(std::size_t) override {}
  void assign(std::span<const theory::AssignedTheoryLiteral>) override {}
  theory::TheoryCheckResult check(theory::TheoryCheckEffort, std::uint64_t epoch,
                                  std::span<const theory::InterfaceRelation>) override {
    ++checks_;
    theory::TheoryCheckResult result;
    result.model.token = {properties_.id, epoch, ++revision_};
    if (queue_.empty() && next_ < batches_.size()) {
      queue_ = batches_[next_++];
    }
    result.status = queue_.empty() ? theory::TheoryCheckStatus::Consistent
                                   : theory::TheoryCheckStatus::LemmasPending;
    return result;
  }
  std::vector<core::TermId> take_lemmas(std::size_t limit) override {
    const auto count = static_cast<std::ptrdiff_t>(std::min(limit, queue_.size()));
    std::vector<core::TermId> taken(queue_.begin(), queue_.begin() + count);
    queue_.erase(queue_.begin(), queue_.begin() + count);
    return taken;
  }
  theory::ComponentModelFragment export_model(
      const theory::TheoryModelToken& token) const override {
    return {token, {}, {}, {}};
  }

 private:
  theory::ComponentDescriptor properties_;
  core::TermId marker_;
  std::vector<std::vector<core::TermId>> batches_;
  std::vector<core::TermId> queue_;
  std::size_t next_ = 0;
  std::uint64_t revision_ = 0;
  std::size_t& checks_;
};

void queued_lemmas_are_drained_at_the_growth_boundary() {
  enum class Scenario {
    RefuteNewAtom,
    KeepNewAtom,
    NeverConsulted,
    ConsultedOnFullModel,
    NoHook,
    BatchedDrain,
    NotConsultedMidSearch,
  };
  for (const Scenario scenario :
       {Scenario::RefuteNewAtom, Scenario::KeepNewAtom, Scenario::NeverConsulted,
        Scenario::ConsultedOnFullModel, Scenario::NoHook, Scenario::BatchedDrain,
        Scenario::NotConsultedMidSearch}) {
    Fixture f;
    const auto sort = f.symbols.declare_sort("S");
    const core::TermId a = f.variable("a", sort);
    const core::TermId c = f.variable("c", sort);
    const core::TermId ground_marker = f.variable("ground", f.symbols.bool_sort());
    const core::TermId lemma_marker = f.variable("lemma", f.symbols.bool_sort());
    // Not in any assertion: only a lemma introduces this atom.
    const core::TermId equal = f.terms.mk_eq(a, c);

    std::vector<std::vector<core::TermId>> batches{{equal}};
    std::vector<core::TermId> assertions{ground_marker, lemma_marker};
    bool full_model_checks = true;
    smt::CheckSatResult expected = smt::CheckSatResult::Sat;
    switch (scenario) {
      case Scenario::RefuteNewAtom:
        batches.push_back({f.terms.mk_not(equal)});
        expected = smt::CheckSatResult::Unsat;
        break;
      case Scenario::KeepNewAtom:
        break;
      case Scenario::NeverConsulted:
        // Owning no assigned atom, the component is never dirty. Without the
        // capability only the final reconciliation sees its queue, and that
        // is Unsupported rather than silently accepted.
        batches = {{f.terms.mk_bool(false)}};
        assertions = {ground_marker};
        full_model_checks = false;
        expected = smt::CheckSatResult::Unsupported;
        break;
      case Scenario::ConsultedOnFullModel:
        batches = {{f.terms.mk_bool(false)}};
        assertions = {ground_marker};
        expected = smt::CheckSatResult::Unsat;
        break;
      case Scenario::NoHook:
        expected = smt::CheckSatResult::Unsupported;
        break;
      case Scenario::BatchedDrain:
        batches = {{equal, f.terms.mk_not(equal)}};
        expected = smt::CheckSatResult::Unsat;
        break;
      case Scenario::NotConsultedMidSearch:
        // The ground component rejects every assignment of its marker, a
        // conflict only the theory finds. A full-model component consulted
        // first mid-search would stop the solve with queued lemmas, mute the
        // ground component, and let SAT accept a propositional model.
        batches = {{lemma_marker}, {lemma_marker}};
        expected = smt::CheckSatResult::Unsat;
        break;
    }

    bool owner_saw_new_atom = false;
    std::size_t lemma_checks = 0;
    smt::CombinationRecipe recipe;
    recipe.online = true;
    recipe.model_guided = false;
    recipe.online_order = {0, 1};
    if (scenario == Scenario::NotConsultedMidSearch) {
      recipe.online_order = {1, 0};
    }
    recipe.timing_prefix = ":time.lemma-test.";
    bool prepared_new_atom = false;
    recipe.prepare_boolean = [&](smt::BooleanClausifier& clausifier) {
      for (const auto& atom : clausifier.theory_atoms()) {
        prepared_new_atom = prepared_new_atom || atom.term == equal;
      }
    };
    if (scenario != Scenario::NoHook) {
      recipe.prepare_lemmas = [](std::vector<core::TermId> formulas) { return formulas; };
    }
    if (scenario == Scenario::BatchedDrain) {
      recipe.lemma_batch = 1;
    }
    recipe.factories.push_back([&](bool) {
      auto component = std::make_unique<PartitionComponent>(f.terms, "ground", ground_marker,
                                                            std::vector<Restriction>{});
      component->owns_equalities = true;
      component->corrupt = [&owner_saw_new_atom, owner = component.get(), equal, ground_marker,
                            scenario](theory::TheoryCheckResult& result) {
        for (const auto& assigned : owner->assignments()) {
          owner_saw_new_atom = owner_saw_new_atom || (assigned.atom == equal && assigned.value);
          // A conflict only this component can report: SAT alone satisfies
          // the assertions, and nothing else refutes the marker.
          if (scenario == Scenario::NotConsultedMidSearch && assigned.atom == ground_marker &&
              assigned.value) {
            result.status = theory::TheoryCheckStatus::LearnedClause;
            result.clause = {sat::Lit{-assigned.lit.value}};
          }
        }
      };
      return component;
    });
    recipe.factories.push_back([&](bool) {
      return std::make_unique<LemmaComponent>(lemma_marker, batches, full_model_checks,
                                              lemma_checks);
    });
    smt::CheckSatOptions options;
    util::Statistics statistics;
    const auto outcome =
        smt::solve_component_recipe(f.terms, assertions, {}, options, statistics, recipe);
    assert(outcome.result == expected);
    const auto drained = statistics.get(":combination.lemmas.batches");
    switch (scenario) {
      case Scenario::RefuteNewAtom:
        assert(drained == 2);
        assert(statistics.get(":combination.components.incremental-growths") == 2);
        break;
      case Scenario::KeepNewAtom:
        assert(drained == 1);
        assert(prepared_new_atom);
        // The lemma's new equality atom reached its owner at the growth boundary.
        assert(owner_saw_new_atom);
        break;
      case Scenario::NeverConsulted:
      case Scenario::NoHook:
        assert(drained == 0);
        break;
      case Scenario::ConsultedOnFullModel:
        assert(drained == 1 && lemma_checks >= 1);
        break;
      case Scenario::BatchedDrain:
        assert(drained == 2);
        assert(statistics.get(":combination.lemmas.formulas") == 2);
        break;
      case Scenario::NotConsultedMidSearch:
        // Refuted before any complete assignment: the lemma source was never
        // asked, so nothing was drained.
        assert(drained == 0 && lemma_checks == 0);
        break;
    }
  }
}

void driver_owned_booleans_are_observed_without_component_routing() {
  for (const bool value : {false, true}) {
    Fixture f;
    const auto marker = f.variable("marker", f.symbols.bool_sort());
    const auto boolean = f.variable("driver_boolean", f.symbols.bool_sort());
    sat::Lit boolean_lit;
    bool observed = false;
    smt::CombinationRecipe recipe;
    recipe.online = true;
    recipe.online_order = {0};
    recipe.model_guided = false;
    recipe.prepare_boolean = [&](smt::BooleanClausifier& clausifier) {
      boolean_lit = clausifier.ensure_theory_atom(boolean).lit;
    };
    recipe.factories = {[&](bool) {
      auto component = std::make_unique<PartitionComponent>(f.terms, "marker", marker,
                                                            std::vector<Restriction>{});
      auto* observer = component.get();
      component->corrupt = [&, observer](theory::TheoryCheckResult&) {
        const int literal = observer->assignment_view().literal(boolean_lit.value);
        if (literal != 0) {
          assert(literal == (value ? boolean_lit.value : -boolean_lit.value));
          observed = true;
        }
        for (const auto& assignment : observer->assignments()) assert(assignment.atom == marker);
      };
      return component;
    }};
    smt::CheckSatOptions options;
    util::Statistics statistics;
    const std::vector assertions{marker, value ? boolean : f.terms.mk_not(boolean)};
    const auto result =
        smt::solve_component_recipe(f.terms, assertions, {}, options, statistics, recipe);
    assert(result.result == smt::CheckSatResult::Sat);
    assert(observed);
  }
}

void empty_component_conflicts_respect_the_aggregate_budget() {
  for (const bool online : {false, true}) {
    for (const bool limited : {false, true}) {
      Fixture f;
      const auto marker = f.variable("marker", f.symbols.bool_sort());
      smt::CombinationRecipe recipe;
      recipe.online = online;
      recipe.online_order = {0};
      recipe.model_guided = false;
      recipe.timing_prefix = ":time.empty-conflict-test.";
      recipe.factories = {[&](bool) {
        auto component = std::make_unique<PartitionComponent>(f.terms, "empty-conflict", marker,
                                                              std::vector<Restriction>{});
        component->corrupt = [](theory::TheoryCheckResult& result) {
          result.status = theory::TheoryCheckStatus::LearnedClause;
          result.clause.clear();
        };
        return component;
      }};
      smt::CheckSatOptions options;
      if (limited) options.max_theory_conflicts = 1;
      util::Statistics statistics;
      const std::vector assertions{marker};
      const auto result =
          smt::solve_component_recipe(f.terms, assertions, {}, options, statistics, recipe);
      assert(result.result ==
             (limited ? smt::CheckSatResult::Unknown : smt::CheckSatResult::Unsat));
      assert(!limited || result.reason == smt::InconclusiveReason::ResourceLimit);
      assert(result.statistics.get(util::StatKey::TheoryConflictCount) == 1);
    }
  }
}

void explained_batches_validate_scope_reasons_and_progress() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  const auto foreign = f.variable("foreign", sort);
  for (int index = 0; index < 2; ++index) f.add({{{x, y, z}, partitions(3)}});
  f.owned[0]->properties.capabilities.equality_propagation =
      theory::EqualityPropagationStrength::Opportunistic;
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  for (std::size_t index = 0; index < f.components.size(); ++index)
    f.components[index]->register_interface(plan.component_scopes(index));
  smt::ComponentModelCoordinator coordinator;
  const auto resolver = [](const theory::InterfacePair&) { return std::optional<sat::Lit>{}; };
  for (const int fault : {-1, 0, 1, 2, 3, 4}) {
    f.owned[0]->corrupt = [=](auto& checked) {
      checked.status = theory::TheoryCheckStatus::EntailedEqualities;
      checked.equalities = {{{x, y, sort}, {sat::Lit{1}}}, {{y, z, sort}, {sat::Lit{-2}}}};
      if (fault == 0) checked.equalities.clear();
      if (fault == 1) checked.equalities[0].reasons = {sat::Lit{0}};
      if (fault == 2) checked.equalities[0].pair.rhs = foreign;
      if (fault == 3) checked.equalities.push_back(checked.equalities.front());
      if (fault == 4) checked.equalities[0].pair.rhs = x;
    };
    const auto result = coordinator.reconcile(plan, 7, f.components, {}, resolver);
    assert(result.status == (fault == -1 ? smt::ModelBasedCombinationStatus::PropagateEqualities
                                         : smt::ModelBasedCombinationStatus::Unknown));
    if (fault == -1) {
      assert(result.equalities.size() == 2);
      assert(result.equalities[1].reasons[0].value == -2);
      assert(coordinator.model_tokens()[0].component.empty());
    }
  }
  f.owned[0]->corrupt = [=](auto& checked) {
    checked.status = theory::TheoryCheckStatus::EntailedEqualities;
    checked.equalities = {{{x, y, sort}, {sat::Lit{1}}}};
  };
  for (const bool already_true : {false, true}) {
    const std::vector arrangement{
        theory::InterfaceRelation{x, y, sort, sat::Lit{42},
                                  already_true ? theory::InterfaceRelationValue::Equal
                                               : theory::InterfaceRelationValue::Disequal}};
    const auto result = coordinator.reconcile(plan, 7, f.components, arrangement, resolver);
    assert(result.status == (already_true ? smt::ModelBasedCombinationStatus::Unknown
                                          : smt::ModelBasedCombinationStatus::PropagateEqualities));
  }
}

void explained_batches_materialize_before_learning_without_model_queries() {
  for (const bool online : {false, true}) {
    for (const bool conflicting : {false, true}) {
      for (const bool learn : {false, true}) {
        Fixture f;
        const auto sort = f.symbols.int_sort();
        const auto x = f.variable("x", sort);
        const auto y = f.variable("y", sort);
        const auto z = f.variable("z", sort);
        const auto first = f.variable("first", f.symbols.bool_sort());
        const auto second = f.variable("second", f.symbols.bool_sort());
        const auto equality = f.terms.mk_eq(x, y);
        const std::vector<core::TermId> all{x, y, z};
        smt::CombinationRecipe recipe;
        recipe.online = online;
        recipe.online_order = {0, 1};
        recipe.model_guided = true;
        recipe.learn_exported_equalities = learn;
        recipe.decision_budget = 8;
        recipe.factories = {
            [&](bool) {
              auto component = std::make_unique<PartitionComponent>(
                  f.terms, "producer", first,
                  std::vector<Restriction>{{all, {Partition{0, 0, 0}}}});
              component->properties.capabilities.equality_propagation =
                  theory::EqualityPropagationStrength::Opportunistic;
              theory::TheoryCheckResult batch;
              batch.status = theory::TheoryCheckStatus::EntailedEqualities;
              // First is the first encoded SAT atom and is asserted false. This
              // checks that negative TRUE antecedents survive both translations.
              batch.equalities = {{{x, y, sort}, {sat::Lit{-1}}}, {{y, z, sort}, {sat::Lit{-1}}}};
              component->pending_equalities = std::move(batch);
              return component;
            },
            [&](bool) {
              auto component = std::make_unique<PartitionComponent>(
                  f.terms, "observer", second, std::vector<Restriction>{{all, partitions(3)}});
              component->owns_equalities = true;
              return component;
            }};
        std::vector assertions{f.terms.mk_not(first), second};
        if (conflicting) assertions.push_back(f.terms.mk_not(equality));
        util::Statistics statistics;
        if (!learn && conflicting) {
          bool rejected = false;
          try {
            static_cast<void>(
                smt::solve_component_recipe(f.terms, assertions, {}, {}, statistics, recipe));
          } catch (const std::logic_error&) {
            rejected = true;
          }
          assert(rejected);  // Naming-only batches must never revisit an existing pair.
          continue;
        }
        const auto result =
            smt::solve_component_recipe(f.terms, assertions, {}, {}, statistics, recipe);
        assert(result.result ==
               (conflicting ? smt::CheckSatResult::Unsat : smt::CheckSatResult::Sat));
        assert(result.statistics.get(":combination.equality.batches") == 1);
        assert(result.statistics.get(":combination.equality.clauses") == (learn ? 2 : 0));
        assert(result.statistics.get(":combination.equality.named-without-proof") ==
               (learn ? 0 : 2));
        assert(result.statistics.get(":combination.equality.fresh-pairs") == (conflicting ? 1 : 2));
        assert(result.statistics.get(":combination.equality.conflicts") == (conflicting ? 1 : 0));
        if (learn) assert(result.statistics.get(":combination.model.decisions") == 0);
      }
    }
  }
}

void integer_split_is_forwarded_without_model_acceptance() {
  Fixture f;
  const auto x = f.variable("private_int", f.symbols.int_sort());
  auto& producer = f.add({});
  const smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, false);
  theory::TheoryCheckResult request;
  request.status = theory::TheoryCheckStatus::IntegerSplit;
  request.integer_split = {x, util::Rational(-4)};
  producer.pending_equalities = request;
  smt::ComponentModelCoordinator coordinator;
  const auto result =
      coordinator.reconcile(plan, 1, f.components, {},
                            [](const theory::InterfacePair&) { return std::optional<sat::Lit>{}; });
  assert(result.status == smt::ModelBasedCombinationStatus::IntegerSplit);
  assert(result.integer_split.term == x);
  assert(result.integer_split.floor == util::Rational(-4));
  assert(result.clause.empty() && result.equalities.empty());
  assert(coordinator.model_tokens()[0].component.empty());
}

}  // namespace

int main() {
  integer_split_is_forwarded_without_model_acceptance();
  explained_batches_validate_scope_reasons_and_progress();
  explained_batches_materialize_before_learning_without_model_queries();

  driver_owned_booleans_are_observed_without_component_routing();
  external_clause_retention_follows_recipe_policy();
  empty_component_conflicts_respect_the_aggregate_budget();
  conflict_reports_are_counted_once_and_propagations_are_excluded();
  admission_and_normalization();
  sort_nonparticipants_and_disconnected_groups();
  exhaustive_three_and_four_component_sat();
  malformed_models_are_not_accepted_or_redecided();
  incremental_routing_backtracking_and_explanation_lifetime();
  nonconvex_disjunction_and_mutation_side_effects();
  care_selection_is_linear_and_validates_late_entries();
  exhaustive_ordinary_choice_order();
  queued_lemmas_are_drained_at_the_growth_boundary();
}
