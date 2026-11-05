#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "sat/ipasir_up.hpp"
#include "theory/theory_component.hpp"

namespace primo::smt {

struct ComponentDispatchStatistics {
  std::size_t routed_literals = 0;
  std::size_t assignment_batches = 0;
  std::size_t partial_checks = 0;
  std::size_t propagations = 0;
};

// SMT-owned, preindexed literal routing. Components own kernel undo and explanations;
// this solve-scoped dispatcher owns backend callback/streaming state, not kernels.
// See docs/n-way-combination-migration.md. Registration requires disconnection.
class ComponentDispatcher final : public sat::ExternalPropagator {
 public:
  // report_conflict returns true when no further component work is permitted.
  explicit ComponentDispatcher(std::span<theory::TheoryComponent* const> components,
                               std::function<void(sat::Lit)> publish_phase = {},
                               std::function<bool()> report_conflict = {},
                               bool forgettable_clauses = true,
                               std::function<void(const sat::Clause&)> observe_clause = {});
  void register_atoms(std::size_t component, std::span<const theory::ComponentAtom> atoms);
  void bind_assignment_view(sat::TrailView view) override;
  void notify_backtrack_delta(std::size_t level, const std::vector<int>& removed) override;
  void notify_assignment(const std::vector<int>& lits) override;
  void notify_new_decision_level() override;
  void notify_backtrack(std::size_t level) override;
  bool cb_check_found_model(const std::vector<int>& model) override;
  int cb_decide() override;
  int cb_propagate() override;
  int cb_add_reason_clause_lit(int propagated_lit) override;
  bool cb_has_external_clause(bool& is_forgettable) override;
  bool cb_has_external_clause() override;
  int cb_add_external_clause_lit() override;
  [[nodiscard]] const std::optional<theory::TheoryCheckResult>& failure() const { return failure_; }
  // Online index of the component whose check reported LemmasPending, if any.
  [[nodiscard]] const std::optional<std::size_t>& lemmas_pending() const { return lemmas_pending_; }
  [[nodiscard]] const ComponentDispatchStatistics& statistics() const { return statistics_; }

 private:
  struct Route {
    std::size_t component = 0;
    theory::ComponentAtom atom;
  };
  struct Explanation {
    std::size_t component = 0;
    sat::Clause clause;
  };
  bool partial_check(bool complete_assignment = false);
  void accept_result(std::size_t component, theory::TheoryCheckResult result);
  std::vector<theory::TheoryComponent*> components_;
  std::vector<std::optional<Route>> routes_;
  std::vector<std::vector<theory::AssignedTheoryLiteral>> deltas_;
  std::vector<std::size_t> touched_;
  std::vector<bool> dirty_;
  std::unordered_map<int, Explanation> reasons_;
  std::optional<Explanation> external_clause_;
  std::size_t clause_cursor_ = 0;
  int reason_literal_ = 0;
  std::size_t reason_cursor_ = 0;
  std::optional<theory::TheoryCheckResult> failure_;
  std::optional<std::size_t> lemmas_pending_;
  ComponentDispatchStatistics statistics_;
  std::function<void(sat::Lit)> publish_phase_;
  std::function<bool()> report_conflict_;
  bool forgettable_clauses_;
  std::function<void(const sat::Clause&)> observe_clause_;
};

}  // namespace primo::smt
