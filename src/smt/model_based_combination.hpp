#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sat/clause.hpp"
#include "sat/lit.hpp"
#include "theory/theory_solver.hpp"

namespace primo::smt {

struct ModelBasedComponentRound {
  theory::TheorySolver* solver = nullptr;
  std::span<const theory::AssignedTheoryLiteral> assignment;
};

struct SharedInterfaceTerms {
  core::SortId sort = core::kInvalidSortId;
  std::vector<core::TermId> terms;
};

enum class ModelBasedCombinationStatus {
  Accept,
  LearnClause,
  PropagateEqualities,
  DecideEquality,
  FreshDecision,
  IntegerSplit,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

struct ModelBasedCombinationResult {
  ModelBasedCombinationStatus status = ModelBasedCombinationStatus::Unknown;
  sat::Clause clause;
  sat::Lit decision_lit;
  std::vector<sat::Lit> decision_lits;
  theory::TheoryDecisionRequest decision;
  std::string diagnostic;
  // Only for PropagateEqualities. These are proved consequences whose SAT
  // atoms may not exist yet; they must never become assumed model decisions.
  std::vector<theory::ExplainedEquality> equalities = {};
  theory::IntegerSplitRequest integer_split = {};
};

struct ModelBasedCombinationStatistics {
  std::size_t component_checks = 0;
  std::size_t snapshots = 0;
  std::size_t mutations = 0;
  std::size_t fixed_points = 0;
  std::size_t care_pairs = 0;
  std::size_t disagreements = 0;
  std::size_t learned_clauses = 0;
  std::size_t model_decisions = 0;
  std::size_t fresh_decisions = 0;
  std::size_t stale_snapshots = 0;
  std::size_t rejected_mutations = 0;
};

using EqualityLiteralResolver =
    std::function<std::optional<sat::Lit>(const theory::InterfacePair&)>;

// Executes the theory/model transitions that follow one satisfying SAT model.
// The caller owns the SAT loop: it learns the returned clause or assumes the
// returned decision literal before asking CaDiCaL for another model.
class ModelBasedCombinationCoordinator {
 public:
  [[nodiscard]] ModelBasedCombinationResult reconcile(
      std::uint64_t assignment_epoch, std::span<const ModelBasedComponentRound> components,
      std::span<const theory::InterfaceRelation> arrangement,
      std::span<const SharedInterfaceTerms> shared_terms,
      const EqualityLiteralResolver& equality_literal);

  [[nodiscard]] const ModelBasedCombinationStatistics& statistics() const { return statistics_; }

 private:
  ModelBasedCombinationStatistics statistics_;
};

}  // namespace primo::smt
