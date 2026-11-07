#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"

namespace primo::theory {

// Properties needed by an SMT-owned combination coordinator.  They are
// declarations about the implemented fragment, not runtime search state.
enum class StableInfiniteness {
  No,
  Yes,
};

enum class Convexity {
  Convex,
  NonConvex,
};

struct TheoryProperties {
  std::string_view id;
  std::vector<core::SortId> shared_sorts;
  StableInfiniteness stably_infinite = StableInfiniteness::No;
  Convexity convexity = Convexity::NonConvex;
  bool produces_model_projection = false;
  bool propagates_equalities = false;
  bool supplies_care_pairs = false;
};

// One SAT assignment projected onto an atom owned by a component theory.
// `lit` is the positive literal naming `atom`; `value` is its assigned truth
// value.  Explanations returned by a component are clauses over these literals.
struct AssignedTheoryLiteral {
  core::TermId atom = core::kInvalidTermId;
  sat::Lit lit;
  bool value = false;
};

enum class InterfaceRelationValue {
  Equal,
  Disequal,
};

// A complete-arrangement policy supplies one relation for every same-sort
// shared pair.  Convex policies may supply only the relations already fixed by
// SAT or explained theory propagation.
struct InterfaceRelation {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
  sat::Lit equality_lit;
  InterfaceRelationValue value = InterfaceRelationValue::Equal;
};

struct SharedModelClass {
  std::vector<core::TermId> terms;
};

// Opaque freshness stamp for a theory-private model.  The SMT coordinator may
// compare and validate this stamp but never dereferences the component model.
struct TheoryModelToken {
  std::string component;
  std::uint64_t assignment_epoch = 0;
  std::uint64_t revision = 0;

  bool operator==(const TheoryModelToken&) const = default;
};

struct SharedModelPartition {
  core::SortId sort = core::kInvalidSortId;
  std::vector<SharedModelClass> equality_classes;
};

struct TheoryModelProjection {
  TheoryModelToken token;
  std::vector<SharedModelPartition> partitions;
};

struct InterfacePair {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;

  bool operator==(const InterfacePair&) const = default;
};

struct TheoryDecisionRequest {
  InterfacePair pair;
  InterfaceRelationValue preferred_value = InterfaceRelationValue::Equal;
  std::vector<sat::Lit> reasons;
};

// A proved shared equality, not a model choice. Reasons are true antecedent
// literals. SMT owns materialization of a fresh equality atom and its clauses.
// Original integration: docs/implementation/uflia-equality-propagation.md.
struct ExplainedEquality {
  InterfacePair pair;
  std::vector<sat::Lit> reasons;
};

enum class TheoryCheckStatus {
  Consistent,
  LearnedClause,
  EntailedLiteral,
  EntailedEqualities,
  FreshDecision,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
  // The component queued lemmas (closed formulas, possibly over new atoms)
  // that the online SMT driver must clausify at its atom-growth boundary
  // before a search result can be trusted; see TheoryComponent::take_lemmas.
  LemmasPending,
};

// A learned clause includes both conflicts and propagations.  Keeping a
// general SAT clause here is what permits non-convex components to return a
// disjunction without pretending that one equality is individually entailed.
struct TheoryCheckResult {
  TheoryCheckStatus status = TheoryCheckStatus::Unknown;
  sat::Clause clause;
  TheoryModelProjection model;
  sat::Lit entailed_lit;
  std::vector<sat::Lit> reasons;
  TheoryDecisionRequest decision;
  std::string diagnostic;
  std::vector<ExplainedEquality> equalities = {};
};

struct TheoryMutationRequest {
  TheoryModelToken source;
  std::vector<InterfaceRelation> required_relations;
};

enum class TheoryMutationStatus {
  Mutated,
  FixedPoint,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

struct TheoryMutationResult {
  TheoryMutationStatus status = TheoryMutationStatus::FixedPoint;
  TheoryModelProjection model;
  std::vector<TheoryDecisionRequest> candidate_decisions;
  std::string diagnostic;
};

// Solve-scoped component interface.  The SMT layer owns purification, SAT,
// interface arrangements and scheduling; an implementation owns only its
// theory-local prepared state and receives no SAT backend handle.
class TheorySolver {
 public:
  virtual ~TheorySolver() = default;

  [[nodiscard]] virtual const TheoryProperties& properties() const = 0;
  [[nodiscard]] virtual TheoryCheckResult check(std::span<const AssignedTheoryLiteral> assignment,
                                                std::span<const InterfaceRelation> arrangement) = 0;
  [[nodiscard]] virtual TheoryMutationResult mutate(const TheoryMutationRequest&) {
    return TheoryMutationResult{};
  }
  [[nodiscard]] virtual std::vector<InterfacePair> care_pairs(const TheoryModelToken&) const {
    return {};
  }
};

// Backend-neutral incremental component lifecycle. The SMT scheduler owns SAT
// variable routing and decision levels; a component owns only its prepared
// theory state and a trail of AssignedTheoryLiteral values. Partial checks may
// return conflicts or consequences, while FullModel additionally requires a
// model projection when the component advertises that capability.
// `push_level` opens the next non-root level; `pop_to_level(0)` removes every
// non-root assignment while preserving root assignments.
//
// This interface deliberately contains no SAT backend handle, so the SMT
// scheduler can change its execution policy without changing theory-local
// ownership.
enum class TheoryCheckEffort {
  Partial,
  FullModel,
};

class IncrementalTheorySolver {
 public:
  virtual ~IncrementalTheorySolver() = default;

  [[nodiscard]] virtual const TheoryProperties& properties() const = 0;
  virtual void reset() = 0;
  virtual void push_level() = 0;
  virtual void pop_to_level(std::size_t level) = 0;
  virtual void assign(std::span<const AssignedTheoryLiteral> assignment_delta) = 0;
  [[nodiscard]] virtual TheoryCheckResult check(TheoryCheckEffort effort) = 0;
};

}  // namespace primo::theory
