#include "smt/solver_engines.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "sat/trail.hpp"
#include "smt/assignment_coordinator.hpp"
#include "smt/boolean_solver.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/component_solver.hpp"
#include "smt/formula_analysis.hpp"
#include "smt/frontend.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/incremental_engine.hpp"
#include "smt/lia_settings.hpp"
#include "smt/model_based_combination.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_runtime.hpp"
#include "smt/solver_support.hpp"
#include "smt/tactic.hpp"
#include "smt/theory_combination.hpp"
#include "smt/theory_combination_plan.hpp"
#include "smt/typed_model_builder.hpp"
#include "theory/arithmetic/lia_completion.hpp"
#include "theory/arithmetic/lia_cube.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lia_explanation.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/linear_polynomial.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "theory/component_assignment.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/explained_congruence_closure.hpp"
#include "theory/equality/flat_congruence_closure.hpp"
#include "theory/equality/flat_terms.hpp"
#include "theory/theory_component.hpp"
#include "theory/theory_solver.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

namespace {

constexpr util::StatKey kStatFiniteDomainDomains = util::StatKey::FiniteDomainDomains;
constexpr util::StatKey kStatUfSymmetryConstraints = util::StatKey::UfSymmetryConstraints;
constexpr util::StatKey kStatAutoBasicSingle = util::StatKey::AutoBasicSingle;
constexpr util::StatKey kStatAutoBasicMulti = util::StatKey::AutoBasicMulti;
constexpr util::StatKey kStatSatTheoryNegativePhase = util::StatKey::SatTheoryNegativePhase;
constexpr util::StatKey kStatSatPhaseAutoPolarity = util::StatKey::SatPhaseAutoPolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicSinglePredicatePolarity =
    util::StatKey::SatPhaseAutoBasicSinglePredicatePolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicMultiPolarity =
    util::StatKey::SatPhaseAutoBasicMultiPolarity;
constexpr util::StatKey kStatSolverQfUf = util::StatKey::SolverQfUf;
constexpr util::StatKey kStatSolverQfLra = util::StatKey::SolverQfLra;
constexpr util::StatKey kStatLraTheoryChecks = util::StatKey::LraTheoryChecks;
constexpr util::StatKey kStatLraTheoryConflicts = util::StatKey::LraTheoryConflicts;
constexpr util::StatKey kStatLraTheoryPropagations = util::StatKey::LraTheoryPropagations;
constexpr util::StatKey kStatLraBlandFallbacks = util::StatKey::LraBlandFallbacks;
constexpr util::StatKey kStatLraSoiMultiRowConflicts = util::StatKey::LraSoiMultiRowConflicts;
constexpr util::StatKey kStatLraSoiMinimizeCalls = util::StatKey::LraSoiMinimizeCalls;
constexpr util::StatKey kStatLraSoiMinimizeShrunk = util::StatKey::LraSoiMinimizeShrunk;
constexpr util::StatKey kStatLraSoiConflictLits = util::StatKey::LraSoiConflictLits;
constexpr util::StatKey kStatLraPivots = util::StatKey::LraPivots;
// Item H3's cost accounting: what the pivots actually cost, not just how
// many there were -- a sparsity rule's whole risk is trading one for the
// other, so both have to be visible.
constexpr util::StatKey kStatLraPivotRowUpdates = util::StatKey::LraPivotRowUpdates;
constexpr util::StatKey kStatLraPivotTermOps = util::StatKey::LraPivotTermOps;
// Distinct from kStatLraTheoryChecks: that one counts iterations of
// solve_qf_lra's outer backend.solve() loop (always 1 under the IPASIR
// online-propagator path, since the whole search happens inside a single
// solve() call) -- not useful for measuring how often the eager
// per-literal tableau_.check() in notify_assignment_lit actually runs.
// This one counts exactly that.
constexpr util::StatKey kStatLraOnlineCheckCalls = util::StatKey::LraOnlineCheckCalls;
constexpr util::StatKey kStatLraRowPropagations = util::StatKey::LraRowPropagations;
constexpr util::StatKey kStatLraBidirectionalRowPropagations =
    util::StatKey::LraBidirectionalRowPropagations;
constexpr util::StatKey kStatLraTheoryDecisions = util::StatKey::LraTheoryDecisions;
constexpr util::StatKey kStatLraModelPhaseRefreshes = util::StatKey::LraModelPhaseRefreshes;
constexpr util::StatKey kStatLraModelPhaseHints = util::StatKey::LraModelPhaseHints;
constexpr util::StatKey kStatSatSolveCalls = util::StatKey::SatSolveCalls;
constexpr util::StatKey kStatTheoryAtoms = util::StatKey::TheoryAtoms;
constexpr util::StatKey kStatEufPredicateAtoms = util::StatKey::EufPredicateAtoms;
constexpr util::StatKey kStatPureTheoryFilterEnabled = util::StatKey::PureTheoryFilterEnabled;
constexpr util::StatKey kStatPureTheoryAtomsNegative = util::StatKey::PureTheoryAtomsNegative;
constexpr util::StatKey kStatPureTheoryLiteralsFiltered = util::StatKey::PureTheoryLiteralsFiltered;
constexpr util::StatKey kStatEufAssignments = util::StatKey::EufAssignments;
constexpr util::StatKey kStatEufEqAssignments = util::StatKey::EufEqAssignments;
constexpr util::StatKey kStatEufDiseqAssignments = util::StatKey::EufDiseqAssignments;
constexpr util::StatKey kStatTheoryChecks = util::StatKey::TheoryChecks;
constexpr util::StatKey kStatTheoryConflicts = util::StatKey::TheoryConflicts;
constexpr util::StatKey kStatEufPropagationRounds = util::StatKey::EufPropagationRounds;
constexpr util::StatKey kStatEufPropagationCandidates = util::StatKey::EufPropagationCandidates;
constexpr util::StatKey kStatEufPropagationDelivered = util::StatKey::EufPropagationDelivered;
constexpr util::StatKey kStatEufPropagationReasonLits = util::StatKey::EufPropagationReasonLits;
constexpr util::StatKey kStatEufPropagationBudget = util::StatKey::EufPropagationBudget;
constexpr util::StatKey kStatEufPropagationDeliveryBudget =
    util::StatKey::EufPropagationDeliveryBudget;
constexpr util::StatKey kStatEufPropagationBudgetCutoffs =
    util::StatKey::EufPropagationBudgetCutoffs;
constexpr util::StatKey kStatEufPropagationHitRateCutoffs =
    util::StatKey::EufPropagationHitRateCutoffs;
constexpr util::StatKey kStatEufPropagationAutoOnline = util::StatKey::EufPropagationAutoOnline;
constexpr util::StatKey kStatEufPropagationAutoConflict = util::StatKey::EufPropagationAutoConflict;
constexpr std::string_view kStatEufDiseqPropAssignedAppDisequalities =
    ":euf.diseq-prop.assigned-app-disequalities";
constexpr std::string_view kStatEufDiseqPropSourceUnaryCandidates =
    ":euf.diseq-prop.source-unary-candidates";
constexpr std::string_view kStatEufDiseqPropSourceUnaryExistingArgAtom =
    ":euf.diseq-prop.source-unary-existing-arg-atom";
constexpr std::string_view kStatEufDiseqPropSourceUnaryArgAtomTrue =
    ":euf.diseq-prop.source-unary-arg-atom-true";
constexpr std::string_view kStatEufDiseqPropSourceUnaryArgAtomFalse =
    ":euf.diseq-prop.source-unary-arg-atom-false";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionCandidates =
    ":euf.diseq-prop.flat-same-function-candidates";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionExistingArgAtom =
    ":euf.diseq-prop.flat-same-function-existing-arg-atom";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionArgAtomTrue =
    ":euf.diseq-prop.flat-same-function-arg-atom-true";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionArgAtomFalse =
    ":euf.diseq-prop.flat-same-function-arg-atom-false";
constexpr std::string_view kStatEufDiseqPropGuardedFunctionGuardCandidates =
    ":euf.diseq-prop.guarded-function-guard-candidates";
constexpr std::string_view kStatEufDiseqPropGuardedArgumentGuardCandidates =
    ":euf.diseq-prop.guarded-argument-guard-candidates";
constexpr std::string_view kStatEufAckermannApplications = ":euf.ackermann.applications";
constexpr std::string_view kStatEufAckermannFunctionSymbols = ":euf.ackermann.function-symbols";
constexpr std::string_view kStatEufAckermannEligibleSymbols = ":euf.ackermann.eligible-symbols";
constexpr std::string_view kStatEufAckermannUnarySymbols = ":euf.ackermann.unary-symbols";
constexpr std::string_view kStatEufAckermannNarySymbols = ":euf.ackermann.nary-symbols";
constexpr std::string_view kStatEufAckermannEstimatedConstraints =
    ":euf.ackermann.estimated-constraints";
constexpr std::string_view kStatEufAckermannEstimatedEqualities =
    ":euf.ackermann.estimated-equalities";
constexpr std::string_view kStatEufAckermannEstimatedClauseLits =
    ":euf.ackermann.estimated-clause-lits";
constexpr std::string_view kStatEufAckermannMaxSymbolApplications =
    ":euf.ackermann.max-symbol-applications";
constexpr std::string_view kStatEufAckermannMaxSymbolArity = ":euf.ackermann.max-symbol-arity";
constexpr std::string_view kStatEufAckermannSymbolsCostLe100 = ":euf.ackermann.symbols-cost-le-100";
constexpr std::string_view kStatEufAckermannSymbolsCostLe1000 =
    ":euf.ackermann.symbols-cost-le-1000";
constexpr std::string_view kStatEufAckermannSymbolsCostLe10000 =
    ":euf.ackermann.symbols-cost-le-10000";
constexpr std::string_view kStatEufDynamicAckShadowInvocations =
    ":euf.dynamic-ack.shadow.invocations";
constexpr std::string_view kStatEufDynamicAckShadowResultPairs =
    ":euf.dynamic-ack.shadow.result-pairs";
constexpr std::string_view kStatEufDynamicAckShadowArgAtomsMissing =
    ":euf.dynamic-ack.shadow.arg-atoms-missing";
constexpr std::string_view kStatEufDynamicAckShadowSatVisibleClauses =
    ":euf.dynamic-ack.shadow.sat-visible-clauses";
constexpr std::string_view kStatEufDynamicAckShadowSatisfied = ":euf.dynamic-ack.shadow.satisfied";
constexpr std::string_view kStatEufDynamicAckShadowUnit = ":euf.dynamic-ack.shadow.unit";
constexpr std::string_view kStatEufDynamicAckShadowConflicting =
    ":euf.dynamic-ack.shadow.conflicting";
constexpr std::string_view kStatEufDynamicAckShadowResultDiseqAssigned =
    ":euf.dynamic-ack.shadow.result-diseq-assigned";
constexpr std::string_view kStatEufDynamicAckShadowAllArgsEqAssigned =
    ":euf.dynamic-ack.shadow.all-args-eq-assigned";
constexpr std::string_view kStatEufDynamicAckShadowWidthTotal =
    ":euf.dynamic-ack.shadow.width-total";
constexpr std::string_view kStatEufDynamicAckShadowMaxWidth = ":euf.dynamic-ack.shadow.max-width";
constexpr std::string_view kStatEufDynamicAckShadowWidthLe3 = ":euf.dynamic-ack.shadow.width-le-3";
constexpr std::string_view kStatEufDynamicAckShadowWidthGt3 = ":euf.dynamic-ack.shadow.width-gt-3";
constexpr std::string_view kStatEufDiseqAckCandidates = ":euf.diseq-ack.candidates";
constexpr std::string_view kStatEufDiseqAckQueuePushes = ":euf.diseq-ack.queue-pushes";
constexpr std::string_view kStatEufDiseqAckQueuePops = ":euf.diseq-ack.queue-pops";
constexpr std::string_view kStatEufDiseqAckWidthSkips = ":euf.diseq-ack.width-skips";
constexpr std::string_view kStatEufDiseqAckMissingArgAtom = ":euf.diseq-ack.missing-arg-atom";
constexpr std::string_view kStatEufDiseqAckSatisfiedSkips = ":euf.diseq-ack.satisfied-skips";
constexpr std::string_view kStatEufDiseqAckUnit = ":euf.diseq-ack.unit";
constexpr std::string_view kStatEufDiseqAckConflict = ":euf.diseq-ack.conflict";
constexpr std::string_view kStatEufDiseqAckPropagated = ":euf.diseq-ack.propagated";
constexpr std::string_view kStatEufDiseqAckReturned = ":euf.diseq-ack.returned";
constexpr std::string_view kStatEufDiseqAckPendingCleared = ":euf.diseq-ack.pending-cleared";
constexpr std::string_view kStatEufDiseqAckReasonLits = ":euf.diseq-ack.reason-lits";
constexpr std::string_view kStatEufDiseqAckQueueBudgetCutoffs =
    ":euf.diseq-ack.queue-budget-cutoffs";
constexpr std::string_view kStatEufDiseqAckPerSymbolCutoffs = ":euf.diseq-ack.per-symbol-cutoffs";
constexpr util::StatKey kStatEufIncrementalTerms = util::StatKey::EufIncrementalTerms;
constexpr util::StatKey kStatEufIncrementalFlatDefinitions =
    util::StatKey::EufIncrementalFlatDefinitions;
constexpr util::StatKey kStatEufIncrementalMerges = util::StatKey::EufIncrementalMerges;
constexpr util::StatKey kStatEufIncrementalPendingPops = util::StatKey::EufIncrementalPendingPops;
constexpr util::StatKey kStatEufIncrementalLookupProbes = util::StatKey::EufIncrementalLookupProbes;
constexpr util::StatKey kStatEufIncrementalLookupHits = util::StatKey::EufIncrementalLookupHits;
constexpr util::StatKey kStatEufIncrementalCongruenceMerges =
    util::StatKey::EufIncrementalCongruenceMerges;
constexpr util::StatKey kStatEufIncrementalDisequalityChecks =
    util::StatKey::EufIncrementalDisequalityChecks;
constexpr util::StatKey kStatEufIncrementalConflicts = util::StatKey::EufIncrementalConflicts;
constexpr util::StatKey kStatEufIncrementalExplainCalls = util::StatKey::EufIncrementalExplainCalls;
constexpr util::StatKey kStatEufIncrementalExplainCacheHits =
    util::StatKey::EufIncrementalExplainCacheHits;
constexpr util::StatKey kStatEufIncrementalExplainCacheBoundMisses =
    util::StatKey::EufIncrementalExplainCacheBoundMisses;
constexpr util::StatKey kStatEufIncrementalExplainLength =
    util::StatKey::EufIncrementalExplainLength;
constexpr util::StatKey kStatEufIncrementalExplainTopLevelQueries =
    util::StatKey::EufIncrementalExplainTopLevelQueries;
constexpr util::StatKey kStatEufIncrementalExplainRecursiveQueries =
    util::StatKey::EufIncrementalExplainRecursiveQueries;
constexpr util::StatKey kStatEufIncrementalExplainRepeatedPairQueries =
    util::StatKey::EufIncrementalExplainRepeatedPairQueries;
constexpr util::StatKey kStatEufIncrementalExplainMaxRecursionDepth =
    util::StatKey::EufIncrementalExplainMaxRecursionDepth;
constexpr util::StatKey kStatEufIncrementalExplainBfsSearches =
    util::StatKey::EufIncrementalExplainBfsSearches;
constexpr util::StatKey kStatEufIncrementalExplainBfsVisited =
    util::StatKey::EufIncrementalExplainBfsVisited;
constexpr util::StatKey kStatEufIncrementalExplainBfsPathEdges =
    util::StatKey::EufIncrementalExplainBfsPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainBfsMaxQueue =
    util::StatKey::EufIncrementalExplainBfsMaxQueue;
constexpr util::StatKey kStatEufIncrementalExplainProofForestQueries =
    util::StatKey::EufIncrementalExplainProofForestQueries;
constexpr util::StatKey kStatEufIncrementalExplainProofForestPathEdges =
    util::StatKey::EufIncrementalExplainProofForestPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainProofForestMaxPathEdges =
    util::StatKey::EufIncrementalExplainProofForestMaxPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainProofForestMissingLca =
    util::StatKey::EufIncrementalExplainProofForestMissingLca;
constexpr util::StatKey kStatEufIncrementalExplainProofForestShorterThanBfs =
    util::StatKey::EufIncrementalExplainProofForestShorterThanBfs;
constexpr util::StatKey kStatEufIncrementalExplainProofForestEqualToBfs =
    util::StatKey::EufIncrementalExplainProofForestEqualToBfs;
constexpr util::StatKey kStatEufIncrementalExplainProofForestLongerThanBfs =
    util::StatKey::EufIncrementalExplainProofForestLongerThanBfs;
constexpr util::StatKey kStatEufIncrementalExplainCausalEdgeSkips =
    util::StatKey::EufIncrementalExplainCausalEdgeSkips;
constexpr util::StatKey kStatEufIncrementalExplainDirectEdges =
    util::StatKey::EufIncrementalExplainDirectEdges;
constexpr util::StatKey kStatEufIncrementalExplainCongruenceEdges =
    util::StatKey::EufIncrementalExplainCongruenceEdges;
constexpr util::StatKey kStatEufIncrementalExplainReasonDuplicateLiterals =
    util::StatKey::EufIncrementalExplainReasonDuplicateLiterals;
constexpr util::StatKey kStatEufIncrementalConflictReasonMaxLength =
    util::StatKey::EufIncrementalConflictReasonMaxLength;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe4 =
    util::StatKey::EufIncrementalConflictReasonLengthLe4;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe8 =
    util::StatKey::EufIncrementalConflictReasonLengthLe8;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe16 =
    util::StatKey::EufIncrementalConflictReasonLengthLe16;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe32 =
    util::StatKey::EufIncrementalConflictReasonLengthLe32;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe64 =
    util::StatKey::EufIncrementalConflictReasonLengthLe64;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthGt64 =
    util::StatKey::EufIncrementalConflictReasonLengthGt64;
constexpr util::StatKey kStatEufIncrementalBacktracks = util::StatKey::EufIncrementalBacktracks;
constexpr util::StatKey kStatEufIncrementalTrailEntries = util::StatKey::EufIncrementalTrailEntries;
constexpr util::StatKey kStatEufIncrementalClassMemberTrailEntriesSaved =
    util::StatKey::EufIncrementalClassMemberTrailEntriesSaved;
constexpr util::StatKey kStatEufIncrementalUseListAppends =
    util::StatKey::EufIncrementalUseListAppends;
constexpr util::StatKey kStatEufIncrementalUseListDuplicateSkips =
    util::StatKey::EufIncrementalUseListDuplicateSkips;
constexpr util::StatKey kStatEufIncrementalAffectedUseListDedup =
    util::StatKey::EufIncrementalAffectedUseListDedup;
constexpr util::StatKey kStatEufIncrementalMaxUseListSize =
    util::StatKey::EufIncrementalMaxUseListSize;
constexpr util::StatKey kStatEufIncrementalChangedTermDrains =
    util::StatKey::EufIncrementalChangedTermDrains;
constexpr util::StatKey kStatEufIncrementalChangedTermsDelivered =
    util::StatKey::EufIncrementalChangedTermsDelivered;
constexpr util::StatKey kStatEufIncrementalChangedTermGenerationRebases =
    util::StatKey::EufIncrementalChangedTermGenerationRebases;
constexpr util::StatKey kStatEufIncrementalMaxCheckpointDepth =
    util::StatKey::EufIncrementalMaxCheckpointDepth;

theory::equality::FlatExplanationSearchMode flat_explanation_search_mode(
    EufExplainSearchMode mode) {
  switch (mode) {
    case EufExplainSearchMode::BidirectionalBfs:
      return theory::equality::FlatExplanationSearchMode::BidirectionalBfs;
    case EufExplainSearchMode::ProofForest:
      return theory::equality::FlatExplanationSearchMode::ProofForest;
    case EufExplainSearchMode::ReorientedProofForest:
      return theory::equality::FlatExplanationSearchMode::ReorientedProofForest;
    case EufExplainSearchMode::StructuralProofForest:
      return theory::equality::FlatExplanationSearchMode::StructuralProofForest;
    case EufExplainSearchMode::OneSidedBfs:
      return theory::equality::FlatExplanationSearchMode::OneSidedBfs;
  }
  return theory::equality::FlatExplanationSearchMode::OneSidedBfs;
}

theory::arithmetic::LraTableau::PivotingRule lra_pivoting_rule_from_options(LraPivotingRule rule) {
  switch (rule) {
    case LraPivotingRule::Dantzig:
      return theory::arithmetic::LraTableau::PivotingRule::Dantzig;
    case LraPivotingRule::Bland:
      return theory::arithmetic::LraTableau::PivotingRule::Bland;
    case LraPivotingRule::Soi:
      return theory::arithmetic::LraTableau::PivotingRule::Soi;
    case LraPivotingRule::Sparse:
      return theory::arithmetic::LraTableau::PivotingRule::Sparse;
  }
  return theory::arithmetic::LraTableau::PivotingRule::Bland;
}

// Same wiring-point translation as above, for the numeric Check/Pivot knobs
// (LraTuning mirrors LraTableau::Tuning field for field).
theory::arithmetic::LraTableau::SoiMinimization lra_soi_minimization_from_options(
    LraSoiMinimization strategy) {
  switch (strategy) {
    case LraSoiMinimization::Deletion:
      return theory::arithmetic::LraTableau::SoiMinimization::Deletion;
    case LraSoiMinimization::None:
      return theory::arithmetic::LraTableau::SoiMinimization::None;
    case LraSoiMinimization::QuickXplain:
      break;
  }
  return theory::arithmetic::LraTableau::SoiMinimization::QuickXplain;
}

theory::arithmetic::LraTableau::SoiMinimizationOrder lra_soi_minimization_order_from_options(
    LraSoiMinimizationOrder order) {
  switch (order) {
    case LraSoiMinimizationOrder::RowSupport:
      return theory::arithmetic::LraTableau::SoiMinimizationOrder::RowSupport;
    case LraSoiMinimizationOrder::Violation:
      return theory::arithmetic::LraTableau::SoiMinimizationOrder::Violation;
    case LraSoiMinimizationOrder::Id:
      break;
  }
  return theory::arithmetic::LraTableau::SoiMinimizationOrder::Id;
}

theory::arithmetic::LraTableau::Tuning lra_tuning_from_options(const LraTuning& tuning) {
  theory::arithmetic::LraTableau::Tuning result;
  result.sparse_pricing_candidates = tuning.sparse_pricing_candidates;
  result.bland_fallback_factor = tuning.bland_fallback_factor;
  result.bland_fallback_offset = tuning.bland_fallback_offset;
  result.soi_minimization = lra_soi_minimization_from_options(tuning.soi_minimization);
  result.soi_minimization_order =
      lra_soi_minimization_order_from_options(tuning.soi_minimization_order);
  return result;
}

constexpr std::size_t kLiveStatsFlushInterval = 1024;
using PhaseClock = std::chrono::steady_clock;

util::Statistics::Value elapsed_microseconds(PhaseClock::time_point start,
                                             PhaseClock::time_point end) {
  return static_cast<util::Statistics::Value>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

class ScopedStatisticsTimer {
 public:
  ScopedStatisticsTimer(util::Statistics& statistics, std::string_view key)
      : statistics_(statistics),
        key_(key),
        start_(key.empty() ? PhaseClock::time_point{} : PhaseClock::now()) {}

  ~ScopedStatisticsTimer() {
    if (!key_.empty()) statistics_.increment(key_, elapsed_microseconds(start_, PhaseClock::now()));
  }

  ScopedStatisticsTimer(const ScopedStatisticsTimer&) = delete;
  ScopedStatisticsTimer& operator=(const ScopedStatisticsTimer&) = delete;

 private:
  util::Statistics& statistics_;
  std::string_view key_;
  PhaseClock::time_point start_;
};

void record_ackermann_cost_diagnostics(const core::TermManager& terms,
                                       const std::vector<core::TermId>& roots,
                                       util::Statistics& statistics) {
  std::vector<unsigned char> seen(terms.size(), 0);
  std::vector<core::TermId> work = roots;
  std::unordered_map<core::SymbolId, std::vector<core::TermId>> applications_by_symbol;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (term_id >= seen.size()) {
      seen.resize(term_id + 1, 0);
    }
    if (seen[term_id] != 0) {
      continue;
    }
    seen[term_id] = 1;
    const core::Term& term = terms.get(term_id);
    if (term.kind == core::TermKind::Application) {
      applications_by_symbol[term.symbol].push_back(term_id);
    }
    work.insert(work.end(), term.children.begin(), term.children.end());
  }

  std::size_t applications = 0;
  std::size_t eligible_symbols = 0;
  std::size_t unary_symbols = 0;
  std::size_t nary_symbols = 0;
  std::size_t estimated_constraints = 0;
  std::size_t estimated_equalities = 0;
  std::size_t max_symbol_applications = 0;
  std::size_t max_symbol_arity = 0;
  std::size_t symbols_cost_le_100 = 0;
  std::size_t symbols_cost_le_1000 = 0;
  std::size_t symbols_cost_le_10000 = 0;

  for (const auto& [symbol_id, symbol_applications] : applications_by_symbol) {
    const core::Symbol& symbol = terms.symbols().get_symbol(symbol_id);
    const std::size_t arity = symbol.arg_sorts.size();
    const std::size_t count = symbol_applications.size();
    applications += count;
    max_symbol_applications = std::max(max_symbol_applications, count);
    max_symbol_arity = std::max(max_symbol_arity, arity);
    if (arity == 1) {
      ++unary_symbols;
    } else if (arity > 1) {
      ++nary_symbols;
    }
    if (count < 2) {
      continue;
    }
    ++eligible_symbols;
    const std::size_t pairs = count * (count - 1U) / 2U;
    if (pairs <= 100) {
      ++symbols_cost_le_100;
    }
    if (pairs <= 1000) {
      ++symbols_cost_le_1000;
    }
    if (pairs <= 10000) {
      ++symbols_cost_le_10000;
    }
    estimated_constraints += pairs;
    estimated_equalities += pairs * (arity + 1U);
  }

  statistics.set(kStatEufAckermannApplications, applications);
  statistics.set(kStatEufAckermannFunctionSymbols, applications_by_symbol.size());
  statistics.set(kStatEufAckermannEligibleSymbols, eligible_symbols);
  statistics.set(kStatEufAckermannUnarySymbols, unary_symbols);
  statistics.set(kStatEufAckermannNarySymbols, nary_symbols);
  statistics.set(kStatEufAckermannEstimatedConstraints, estimated_constraints);
  statistics.set(kStatEufAckermannEstimatedEqualities, estimated_equalities);
  statistics.set(kStatEufAckermannEstimatedClauseLits, estimated_equalities);
  statistics.set(kStatEufAckermannMaxSymbolApplications, max_symbol_applications);
  statistics.set(kStatEufAckermannMaxSymbolArity, max_symbol_arity);
  statistics.set(kStatEufAckermannSymbolsCostLe100, symbols_cost_le_100);
  statistics.set(kStatEufAckermannSymbolsCostLe1000, symbols_cost_le_1000);
  statistics.set(kStatEufAckermannSymbolsCostLe10000, symbols_cost_le_10000);
}

EufPropagationMode resolve_euf_propagation_mode(const CheckSatOptions& options,
                                                std::size_t theory_atom_count,
                                                util::Statistics& statistics) {
  if (options.euf_propagation != EufPropagationMode::Auto) {
    return options.euf_propagation;
  }
  constexpr std::size_t kAutoOnlineTheoryAtomLimit = 5000;
  if (theory_atom_count <= kAutoOnlineTheoryAtomLimit) {
    statistics.set(kStatEufPropagationAutoOnline, 1);
    return EufPropagationMode::Online;
  }
  constexpr std::size_t kAutoOnlineFiniteDomainDomainLimit = 500;
  const std::size_t finite_domain_domains = statistics.get(kStatFiniteDomainDomains);
  const std::size_t pure_negative_atoms = statistics.get(kStatPureTheoryAtomsNegative);
  if (finite_domain_domains >= kAutoOnlineFiniteDomainDomainLimit &&
      pure_negative_atoms <= finite_domain_domains) {
    statistics.set(kStatEufPropagationAutoOnline, 1);
    return EufPropagationMode::Online;
  }
  statistics.set(kStatEufPropagationAutoConflict, 1);
  return EufPropagationMode::Conflict;
}

struct AssignedTheoryAtom {
  int lit = 0;
  bool value = false;
  core::TermId term = 0;
  bool filtered = false;
};

sat::Lit blocking_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{-atom.lit} : sat::Lit{atom.lit};
}

sat::Lit assigned_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{atom.lit} : sat::Lit{-atom.lit};
}

bool is_pure_positive(const BooleanClausifier::TheoryAtom& atom) {
  return atom.occurs_positive && !atom.occurs_negative;
}

bool is_pure_negative(const BooleanClausifier::TheoryAtom& atom) {
  return atom.occurs_negative && !atom.occurs_positive;
}

bool pure_theory_filter_enabled(PureTheoryFilterMode mode) {
  return mode != PureTheoryFilterMode::Off;
}

bool should_filter_assignment(PureTheoryFilterMode mode, const BooleanClausifier::TheoryAtom& atom,
                              bool value) {
  if (!pure_theory_filter_enabled(mode)) {
    return false;
  }
  // The filter's argument is that a purely-positive atom cannot matter to the
  // theory when assigned false. That holds only when the atom's meaning IS its
  // propositional occurrence, which is untrue for a term the theory reads
  // through some other route.
  if (atom.theory_relevant_at_both_polarities) {
    return false;
  }
  return (is_pure_positive(atom) && !value) || (is_pure_negative(atom) && value);
}

AssignedTheoryAtom assigned_theory_atom(PureTheoryFilterMode mode,
                                        const BooleanClausifier::TheoryAtom& atom, bool value) {
  return AssignedTheoryAtom{atom.lit.value, value, atom.term,
                            should_filter_assignment(mode, atom, value)};
}

TheoryPhaseMode resolve_theory_phase_mode(TheoryPhaseMode mode,
                                          const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                          const util::Statistics& statistics) {
  if (mode != TheoryPhaseMode::Auto) {
    return mode;
  }
  const bool has_finite_domain_structure = statistics.get(kStatFiniteDomainDomains) != 0 ||
                                           statistics.get(kStatUfSymmetryConstraints) != 0;
  if (!has_finite_domain_structure) {
    return TheoryPhaseMode::Negative;
  }
  std::size_t pure_positive = 0;
  std::size_t pure_negative = 0;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    if (is_pure_positive(atom)) {
      ++pure_positive;
    } else if (is_pure_negative(atom)) {
      ++pure_negative;
    }
  }
  if (pure_negative == 0) {
    return TheoryPhaseMode::Negative;
  }
  constexpr std::size_t kLowNegativePolarityLimit = 25;
  constexpr std::size_t kLowNegativeMinimumPositiveAtoms = 100;
  if (pure_negative <= kLowNegativePolarityLimit &&
      pure_positive >= kLowNegativeMinimumPositiveAtoms) {
    return TheoryPhaseMode::Polarity;
  }
  return TheoryPhaseMode::Negative;
}

void count_filtered_assignment(const AssignedTheoryAtom& atom, util::Statistics& statistics) {
  if (atom.filtered) {
    statistics.increment(kStatPureTheoryLiteralsFiltered);
  }
}

void increment_stat_if_nonzero(util::Statistics& statistics, util::StatKey key, std::size_t value) {
  if (value != 0) {
    statistics.increment(key, value);
  }
}

void add_lia_patching_stats(util::Statistics& statistics,
                            const theory::arithmetic::LiaPatchingStats& stats) {
  statistics.increment(util::StatKey::LiaPatchOccurrenceEntries, stats.occurrence_entries);
  statistics.increment(util::StatKey::LiaPatchCandidatesScored, stats.candidates_scored);
  statistics.increment(util::StatKey::LiaPatchAffectedRowsInspected, stats.affected_rows_inspected);
  statistics.increment(util::StatKey::LiaPatchFullScanEquivalent, stats.full_scan_equivalent);
  statistics.increment(util::StatKey::LiaPatchFractionalNonbasicMoves,
                       stats.fractional_nonbasic_moves);
  statistics.increment(util::StatKey::LiaPatchFractionalCandidatesRejected,
                       stats.fractional_candidates_rejected);
  statistics.increment(util::StatKey::LiaPatchLatticeCandidatesScored,
                       stats.lattice_candidates_scored);
  statistics.increment(util::StatKey::LiaPatchLatticeMovesAccepted, stats.lattice_moves_accepted);
  statistics.increment(util::StatKey::LiaPatchIntervalRequests, stats.interval_requests);
  statistics.increment(util::StatKey::LiaPatchIntervalComputations, stats.interval_computations);
  statistics.increment(util::StatKey::LiaPatchIntervalCacheHits, stats.interval_cache_hits);
}

bool add_explained_theory_equality(theory::equality::ExplainedCongruenceClosure& closure,
                                   const core::TermManager& terms, const AssignedTheoryAtom& atom,
                                   core::TermId true_term, core::TermId false_term) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality && atom.value) {
    closure.assert_equal(term.children.at(0), term.children.at(1), {blocking_lit(atom)});
    return true;
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    closure.assert_equal(atom.term, atom.value ? true_term : false_term, {blocking_lit(atom)});
    return true;
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    closure.assert_equal(atom.term, atom.value ? true_term : false_term, {blocking_lit(atom)});
    return true;
  }
  return false;
}

void add_incremental_stats(util::Statistics& statistics,
                           const theory::equality::IncrementalCongruenceClosureStats& stats) {
  increment_stat_if_nonzero(statistics, kStatEufIncrementalTerms, stats.terms);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalFlatDefinitions, stats.flat_definitions);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalMerges, stats.merges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalPendingPops, stats.pending_pops);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalLookupProbes, stats.lookup_probes);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalLookupHits, stats.lookup_hits);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalCongruenceMerges,
                            stats.congruence_merges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalDisequalityChecks,
                            stats.disequality_checks);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflicts, stats.conflicts);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCalls, stats.explain_calls);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCacheHits,
                            stats.explain_cache_hits);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCacheBoundMisses,
                            stats.explain_cache_bound_misses);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainLength, stats.explain_length);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainTopLevelQueries,
                            stats.explain_top_level_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainRecursiveQueries,
                            stats.explain_recursive_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainRepeatedPairQueries,
                            stats.explain_repeated_pair_queries);
  if (stats.explain_max_recursion_depth >
      statistics.get(kStatEufIncrementalExplainMaxRecursionDepth)) {
    statistics.set(kStatEufIncrementalExplainMaxRecursionDepth, stats.explain_max_recursion_depth);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsSearches,
                            stats.explain_bfs_searches);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsVisited,
                            stats.explain_bfs_visited);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsPathEdges,
                            stats.explain_bfs_path_edges);
  if (stats.explain_bfs_max_queue > statistics.get(kStatEufIncrementalExplainBfsMaxQueue)) {
    statistics.set(kStatEufIncrementalExplainBfsMaxQueue, stats.explain_bfs_max_queue);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestQueries,
                            stats.explain_proof_forest_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestPathEdges,
                            stats.explain_proof_forest_path_edges);
  if (stats.explain_proof_forest_max_path_edges >
      statistics.get(kStatEufIncrementalExplainProofForestMaxPathEdges)) {
    statistics.set(kStatEufIncrementalExplainProofForestMaxPathEdges,
                   stats.explain_proof_forest_max_path_edges);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestMissingLca,
                            stats.explain_proof_forest_missing_lca);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestShorterThanBfs,
                            stats.explain_proof_forest_shorter_than_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestEqualToBfs,
                            stats.explain_proof_forest_equal_to_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestLongerThanBfs,
                            stats.explain_proof_forest_longer_than_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCausalEdgeSkips,
                            stats.explain_causal_edge_skips);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainDirectEdges,
                            stats.explain_direct_edges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCongruenceEdges,
                            stats.explain_congruence_edges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainReasonDuplicateLiterals,
                            stats.explain_reason_duplicate_literals);
  if (stats.conflict_reason_max_length >
      statistics.get(kStatEufIncrementalConflictReasonMaxLength)) {
    statistics.set(kStatEufIncrementalConflictReasonMaxLength, stats.conflict_reason_max_length);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe4,
                            stats.conflict_reason_length_le_4);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe8,
                            stats.conflict_reason_length_le_8);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe16,
                            stats.conflict_reason_length_le_16);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe32,
                            stats.conflict_reason_length_le_32);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe64,
                            stats.conflict_reason_length_le_64);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthGt64,
                            stats.conflict_reason_length_gt_64);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalBacktracks, stats.backtracks);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalTrailEntries, stats.trail_entries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalClassMemberTrailEntriesSaved,
                            stats.class_member_trail_entries_saved);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalUseListAppends, stats.use_list_appends);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalUseListDuplicateSkips,
                            stats.use_list_duplicate_skips);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalAffectedUseListDedup,
                            stats.affected_use_list_dedup);
  if (stats.max_use_list_size > statistics.get(kStatEufIncrementalMaxUseListSize)) {
    statistics.set(kStatEufIncrementalMaxUseListSize, stats.max_use_list_size);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalChangedTermDrains,
                            stats.changed_term_drains);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalChangedTermsDelivered,
                            stats.changed_terms_delivered);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalChangedTermGenerationRebases,
                            stats.changed_term_generation_rebases);
  if (stats.max_checkpoint_depth > statistics.get(kStatEufIncrementalMaxCheckpointDepth)) {
    statistics.set(kStatEufIncrementalMaxCheckpointDepth, stats.max_checkpoint_depth);
  }
}

std::size_t positive_delta(std::size_t current, std::size_t previous) {
  return current >= previous ? current - previous : 0;
}

theory::equality::IncrementalCongruenceClosureStats stats_delta(
    const theory::equality::IncrementalCongruenceClosureStats& lhs,
    const theory::equality::IncrementalCongruenceClosureStats& rhs) {
  return theory::equality::IncrementalCongruenceClosureStats{
      positive_delta(lhs.add_term_requests, rhs.add_term_requests),
      positive_delta(lhs.add_term_seen_hits, rhs.add_term_seen_hits),
      positive_delta(lhs.terms, rhs.terms),
      positive_delta(lhs.flat_definitions, rhs.flat_definitions),
      positive_delta(lhs.merges, rhs.merges),
      positive_delta(lhs.pending_pops, rhs.pending_pops),
      positive_delta(lhs.lookup_probes, rhs.lookup_probes),
      positive_delta(lhs.lookup_hits, rhs.lookup_hits),
      positive_delta(lhs.congruence_merges, rhs.congruence_merges),
      positive_delta(lhs.disequality_checks, rhs.disequality_checks),
      positive_delta(lhs.conflicts, rhs.conflicts),
      positive_delta(lhs.explain_calls, rhs.explain_calls),
      positive_delta(lhs.explain_cache_hits, rhs.explain_cache_hits),
      positive_delta(lhs.explain_cache_bound_misses, rhs.explain_cache_bound_misses),
      positive_delta(lhs.explain_length, rhs.explain_length),
      positive_delta(lhs.explain_top_level_queries, rhs.explain_top_level_queries),
      positive_delta(lhs.explain_recursive_queries, rhs.explain_recursive_queries),
      positive_delta(lhs.explain_repeated_pair_queries, rhs.explain_repeated_pair_queries),
      lhs.explain_max_recursion_depth,
      positive_delta(lhs.explain_bfs_searches, rhs.explain_bfs_searches),
      positive_delta(lhs.explain_bfs_visited, rhs.explain_bfs_visited),
      positive_delta(lhs.explain_bfs_path_edges, rhs.explain_bfs_path_edges),
      lhs.explain_bfs_max_queue,
      positive_delta(lhs.explain_proof_forest_queries, rhs.explain_proof_forest_queries),
      positive_delta(lhs.explain_proof_forest_path_edges, rhs.explain_proof_forest_path_edges),
      lhs.explain_proof_forest_max_path_edges,
      positive_delta(lhs.explain_proof_forest_missing_lca, rhs.explain_proof_forest_missing_lca),
      positive_delta(lhs.explain_proof_forest_shorter_than_bfs,
                     rhs.explain_proof_forest_shorter_than_bfs),
      positive_delta(lhs.explain_proof_forest_equal_to_bfs, rhs.explain_proof_forest_equal_to_bfs),
      positive_delta(lhs.explain_proof_forest_longer_than_bfs,
                     rhs.explain_proof_forest_longer_than_bfs),
      positive_delta(lhs.explain_causal_edge_skips, rhs.explain_causal_edge_skips),
      positive_delta(lhs.explain_direct_edges, rhs.explain_direct_edges),
      positive_delta(lhs.explain_congruence_edges, rhs.explain_congruence_edges),
      positive_delta(lhs.explain_reason_duplicate_literals, rhs.explain_reason_duplicate_literals),
      lhs.conflict_reason_max_length,
      positive_delta(lhs.conflict_reason_length_le_4, rhs.conflict_reason_length_le_4),
      positive_delta(lhs.conflict_reason_length_le_8, rhs.conflict_reason_length_le_8),
      positive_delta(lhs.conflict_reason_length_le_16, rhs.conflict_reason_length_le_16),
      positive_delta(lhs.conflict_reason_length_le_32, rhs.conflict_reason_length_le_32),
      positive_delta(lhs.conflict_reason_length_le_64, rhs.conflict_reason_length_le_64),
      positive_delta(lhs.conflict_reason_length_gt_64, rhs.conflict_reason_length_gt_64),
      positive_delta(lhs.backtracks, rhs.backtracks),
      positive_delta(lhs.trail_entries, rhs.trail_entries),
      positive_delta(lhs.class_member_trail_entries_saved, rhs.class_member_trail_entries_saved),
      positive_delta(lhs.use_list_appends, rhs.use_list_appends),
      positive_delta(lhs.use_list_duplicate_skips, rhs.use_list_duplicate_skips),
      positive_delta(lhs.affected_use_list_dedup, rhs.affected_use_list_dedup),
      lhs.max_use_list_size,
      positive_delta(lhs.changed_term_drains, rhs.changed_term_drains),
      positive_delta(lhs.changed_terms_delivered, rhs.changed_terms_delivered),
      positive_delta(lhs.changed_term_generation_rebases, rhs.changed_term_generation_rebases),
      lhs.max_checkpoint_depth,
  };
}

std::optional<std::size_t> resolve_budget(AutoBudget budget, std::size_t theory_atoms) {
  switch (budget.kind) {
    case AutoBudget::Kind::Auto:
      return theory_atoms;
    case AutoBudget::Kind::Unlimited:
      return std::nullopt;
    case AutoBudget::Kind::Fixed:
      return budget.value;
  }
  return theory_atoms;
}

std::optional<std::size_t> resolve_delivery_budget(AutoBudget budget, std::size_t candidate_budget,
                                                   std::size_t theory_atoms) {
  switch (budget.kind) {
    case AutoBudget::Kind::Auto:
      return std::max<std::size_t>(1, std::min(theory_atoms, candidate_budget / 2 + 1));
    case AutoBudget::Kind::Unlimited:
      return std::nullopt;
    case AutoBudget::Kind::Fixed:
      if (budget.value == 0) {
        return std::nullopt;
      }
      return budget.value;
  }
  return std::max<std::size_t>(1, std::min(theory_atoms, candidate_budget / 2 + 1));
}

std::string clause_key(const sat::Clause& clause) {
  std::string key;
  for (const sat::Lit lit : clause) {
    key += std::to_string(lit.value);
    key += ',';
  }
  return key;
}

std::optional<std::vector<sat::Lit>> candidate_explanation(
    theory::equality::ExplainedCongruenceClosure& closure, const core::TermManager& terms,
    const AssignedTheoryAtom& atom, core::TermId true_term, core::TermId false_term) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality) {
    if (!atom.value) {
      return std::nullopt;
    }
    return closure.explain_equal(term.children.at(0), term.children.at(1));
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    return closure.explain_equal(atom.term, atom.value ? true_term : false_term);
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    return closure.explain_equal(atom.term, atom.value ? true_term : false_term);
  }
  throw UnsupportedInput("unsupported theory atom");
}

std::optional<sat::Clause> model_propagation_clause_for(
    const core::TermManager& terms, const std::vector<AssignedTheoryAtom>& atoms,
    std::size_t candidate_index, core::TermId true_term, core::TermId false_term) {
  theory::equality::ExplainedCongruenceClosure explained(terms);
  explained.add_term(true_term);
  explained.add_term(false_term);

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (i == candidate_index) {
      continue;
    }
    const AssignedTheoryAtom& atom = atoms[i];
    if (atom.filtered) {
      continue;
    }
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      continue;
    }
    if (!add_explained_theory_equality(explained, terms, atom, true_term, false_term)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  const AssignedTheoryAtom& candidate = atoms[candidate_index];
  if (candidate.filtered) {
    return std::nullopt;
  }
  std::optional<std::vector<sat::Lit>> reason =
      candidate_explanation(explained, terms, candidate, true_term, false_term);
  if (!reason.has_value()) {
    return std::nullopt;
  }
  reason->push_back(assigned_lit(candidate));
  return reason;
}

struct FlatAssignedTheoryAtom {
  AssignedTheoryAtom source;
  bool equality = false;
  theory::equality::FlatTermId lhs = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId rhs = theory::equality::kInvalidFlatTermId;
};

struct PreparedFlatTheory {
  PreparedFlatTheory(const core::TermManager& terms,
                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                     core::TermId true_term, core::TermId false_term,
                     EufExplainSearchMode explain_search_mode, bool proof_forest_diagnostics);

  // Extend the structural preparation without adding round-specific facts.
  std::size_t ensure_theory_atoms(const core::TermManager& terms,
                                  std::span<const BooleanClausifier::TheoryAtom> theory_atoms);
  std::vector<AssignedTheoryAtom> align_assignments(
      const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
      const std::vector<AssignedTheoryAtom>& assignments) const;

  theory::equality::FlatTermIndex flat;
  theory::equality::FlatCongruenceClosure base_closure;
  theory::equality::FlatTermId true_flat = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
  std::unordered_map<std::uint64_t, std::size_t> atom_index_by_key;
  std::size_t installed_definition_count = 0;
};

struct FlatEufModelView {
  const theory::equality::FlatTermIndex& flat;
  theory::equality::FlatCongruenceClosure& closure;
};

struct TermPairKey {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;

  bool operator==(const TermPairKey& other) const = default;
};

struct TermPairKeyHash {
  std::size_t operator()(const TermPairKey& key) const {
    std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
    seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

TermPairKey canonical_term_pair(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return TermPairKey{lhs, rhs};
}

struct FlatPairKey {
  theory::equality::FlatTermId lhs = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId rhs = theory::equality::kInvalidFlatTermId;

  bool operator==(const FlatPairKey& other) const = default;
};

struct FlatPairKeyHash {
  std::size_t operator()(const FlatPairKey& key) const {
    std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
    seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

FlatPairKey canonical_flat_pair(theory::equality::FlatTermId lhs,
                                theory::equality::FlatTermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return FlatPairKey{lhs, rhs};
}

struct DynamicAckermannCandidate {
  std::size_t result_atom_index = 0;
  core::SymbolId symbol = core::kInvalidSymbolId;
  std::vector<std::size_t> argument_atom_indices;
  sat::Clause clause;
  std::size_t width = 0;
};

struct DynamicAckermannCandidateIndex {
  std::vector<DynamicAckermannCandidate> candidates;
  std::size_t result_pairs = 0;
  std::size_t missing_argument_atoms = 0;
};

bool clause_contains_lit(const sat::Clause& clause, sat::Lit lit) {
  return std::find_if(clause.begin(), clause.end(), [lit](sat::Lit existing) {
           return existing.value == lit.value;
         }) != clause.end();
}

void append_lit_once(sat::Clause& clause, sat::Lit lit) {
  if (!clause_contains_lit(clause, lit)) {
    clause.push_back(lit);
  }
}

DynamicAckermannCandidateIndex build_dynamic_ackermann_candidate_index(
    const core::TermManager& terms,
    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms) {
  DynamicAckermannCandidateIndex index;
  std::unordered_map<TermPairKey, std::size_t, TermPairKeyHash> equality_atom_by_pair;
  equality_atom_by_pair.reserve(theory_atoms.size());
  for (std::size_t atom_index = 0; atom_index < theory_atoms.size(); ++atom_index) {
    const core::Term& term = terms.get(theory_atoms[atom_index].term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
      continue;
    }
    equality_atom_by_pair.emplace(canonical_term_pair(term.children[0], term.children[1]),
                                  atom_index);
  }

  for (std::size_t atom_index = 0; atom_index < theory_atoms.size(); ++atom_index) {
    const core::Term& result_equality = terms.get(theory_atoms[atom_index].term);
    if (result_equality.kind != core::TermKind::Equality || result_equality.children.size() != 2) {
      continue;
    }
    const core::Term& lhs = terms.get(result_equality.children[0]);
    const core::Term& rhs = terms.get(result_equality.children[1]);
    if (lhs.kind != core::TermKind::Application || rhs.kind != core::TermKind::Application ||
        lhs.symbol != rhs.symbol || lhs.children.size() != rhs.children.size()) {
      continue;
    }

    ++index.result_pairs;
    DynamicAckermannCandidate candidate;
    candidate.result_atom_index = atom_index;
    candidate.symbol = lhs.symbol;
    candidate.argument_atom_indices.reserve(lhs.children.size());
    candidate.clause.reserve(lhs.children.size() + 1U);
    candidate.clause.push_back(theory_atoms[atom_index].lit);
    std::size_t missing_argument_atoms = 0;
    for (std::size_t child_index = 0; child_index < lhs.children.size(); ++child_index) {
      const auto found = equality_atom_by_pair.find(
          canonical_term_pair(lhs.children[child_index], rhs.children[child_index]));
      if (found == equality_atom_by_pair.end()) {
        ++missing_argument_atoms;
        continue;
      }
      const sat::Lit argument_disequality_lit{-theory_atoms[found->second].lit.value};
      if (clause_contains_lit(candidate.clause, argument_disequality_lit)) {
        continue;
      }
      candidate.argument_atom_indices.push_back(found->second);
      candidate.clause.push_back(argument_disequality_lit);
    }
    if (missing_argument_atoms != 0) {
      index.missing_argument_atoms += missing_argument_atoms;
      continue;
    }
    candidate.width = candidate.clause.size();
    index.candidates.push_back(std::move(candidate));
  }
  return index;
}

void record_dynamic_ackermann_shadow(const DynamicAckermannCandidateIndex& candidate_index,
                                     const std::vector<std::optional<bool>>& atom_values,
                                     util::Statistics& statistics) {
  statistics.increment(kStatEufDynamicAckShadowInvocations);
  statistics.increment(kStatEufDynamicAckShadowResultPairs, candidate_index.result_pairs);
  statistics.increment(kStatEufDynamicAckShadowArgAtomsMissing,
                       candidate_index.missing_argument_atoms);

  for (const DynamicAckermannCandidate& candidate : candidate_index.candidates) {
    const std::size_t width = candidate.width;
    statistics.increment(kStatEufDynamicAckShadowSatVisibleClauses);
    statistics.increment(kStatEufDynamicAckShadowWidthTotal, width);
    if (width > statistics.get(kStatEufDynamicAckShadowMaxWidth)) {
      statistics.set(kStatEufDynamicAckShadowMaxWidth, width);
    }
    statistics.increment(width <= 3 ? kStatEufDynamicAckShadowWidthLe3
                                    : kStatEufDynamicAckShadowWidthGt3);

    std::size_t true_literals = 0;
    std::size_t unassigned_literals = 0;
    const std::optional<bool> result_value = atom_values.at(candidate.result_atom_index);
    if (!result_value.has_value()) {
      ++unassigned_literals;
    } else if (result_value.value()) {
      ++true_literals;
    } else {
      statistics.increment(kStatEufDynamicAckShadowResultDiseqAssigned);
    }

    bool all_arguments_equal_assigned = !candidate.argument_atom_indices.empty();
    for (const std::size_t argument_index : candidate.argument_atom_indices) {
      const std::optional<bool> argument_value = atom_values.at(argument_index);
      if (!argument_value.has_value()) {
        ++unassigned_literals;
        all_arguments_equal_assigned = false;
      } else if (argument_value.value()) {
        // The Ackermann clause contains the argument disequality literal, so an
        // assigned argument equality makes this clause literal false.
      } else {
        ++true_literals;
        all_arguments_equal_assigned = false;
      }
    }
    if (all_arguments_equal_assigned) {
      statistics.increment(kStatEufDynamicAckShadowAllArgsEqAssigned);
    }
    if (true_literals != 0) {
      statistics.increment(kStatEufDynamicAckShadowSatisfied);
    } else if (unassigned_literals == 1) {
      statistics.increment(kStatEufDynamicAckShadowUnit);
    } else if (unassigned_literals == 0) {
      statistics.increment(kStatEufDynamicAckShadowConflicting);
    }
  }
}

void record_dynamic_ackermann_shadow(const core::TermManager& terms,
                                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                     const std::vector<std::optional<bool>>& atom_values,
                                     util::Statistics& statistics) {
  if (atom_values.size() != theory_atoms.size()) {
    throw UnsupportedInput("internal dynamic Ackermann shadow assignment mismatch");
  }
  const DynamicAckermannCandidateIndex candidate_index =
      build_dynamic_ackermann_candidate_index(terms, theory_atoms);
  record_dynamic_ackermann_shadow(candidate_index, atom_values, statistics);
}

FlatAssignedTheoryAtom flatten_assigned_atom(theory::equality::FlatTermIndex& flat,
                                             const core::TermManager& terms,
                                             const AssignedTheoryAtom& atom,
                                             theory::equality::FlatTermId false_flat) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality) {
    return FlatAssignedTheoryAtom{atom, true, flat.flatten(term.children.at(0)),
                                  flat.flatten(term.children.at(1))};
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    return FlatAssignedTheoryAtom{atom, false, flat.flatten(atom.term), false_flat};
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    return FlatAssignedTheoryAtom{atom, false, flat.flatten(atom.term), false_flat};
  }
  throw UnsupportedInput("unsupported theory atom");
}

PreparedFlatTheory::PreparedFlatTheory(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    core::TermId true_term, core::TermId false_term, EufExplainSearchMode explain_search_mode,
    bool proof_forest_diagnostics)
    : flat(terms),
      base_closure(0, flat_explanation_search_mode(explain_search_mode), proof_forest_diagnostics) {
  true_flat = flat.flatten(true_term);
  false_flat = flat.flatten(false_term);
  flat_atoms.reserve(theory_atoms.size());
  base_closure.assert_distinct(true_flat, false_flat);
  ensure_theory_atoms(terms, theory_atoms);
}

std::size_t PreparedFlatTheory::ensure_theory_atoms(
    const core::TermManager& terms, std::span<const BooleanClausifier::TheoryAtom> theory_atoms) {
  std::size_t appended = 0;
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    const std::uint64_t key =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(atom.lit.value)) << 32U) ^
        static_cast<std::uint32_t>(atom.term);
    const bool inserted = atom_index_by_key.emplace(key, flat_atoms.size()).second;
    if (!inserted) {
      continue;
    }
    flat_atoms.push_back(flatten_assigned_atom(
        flat, terms, AssignedTheoryAtom{atom.lit.value, false, atom.term}, false_flat));
    ++appended;
  }

  base_closure.ensure_node_count(flat.size());
  const std::vector<theory::equality::FlatDefinition>& definitions = flat.definitions();
  for (; installed_definition_count < definitions.size(); ++installed_definition_count) {
    base_closure.add_definition(definitions[installed_definition_count]);
  }
  return appended;
}

std::vector<AssignedTheoryAtom> PreparedFlatTheory::align_assignments(
    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    const std::vector<AssignedTheoryAtom>& assignments) const {
  if (theory_atoms.size() != assignments.size()) {
    throw UnsupportedInput("internal EUF theory atom assignment mismatch");
  }

  std::vector<AssignedTheoryAtom> aligned(flat_atoms.size());
  std::vector<bool> assigned(flat_atoms.size(), false);
  for (std::size_t index = 0; index < theory_atoms.size(); ++index) {
    const BooleanClausifier::TheoryAtom& atom = theory_atoms[index];
    const std::uint64_t key =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(atom.lit.value)) << 32U) ^
        static_cast<std::uint32_t>(atom.term);
    const auto prepared_index = atom_index_by_key.find(key);
    if (prepared_index == atom_index_by_key.end()) {
      throw UnsupportedInput("internal EUF theory atom was not prepared");
    }
    aligned[prepared_index->second] = assignments[index];
    assigned[prepared_index->second] = true;
  }
  if (std::find(assigned.begin(), assigned.end(), false) != assigned.end()) {
    throw UnsupportedInput("internal prepared EUF atom has no current assignment");
  }
  return aligned;
}

void assert_flat_assigned_atom(theory::equality::FlatCongruenceClosure& closure,
                               const FlatAssignedTheoryAtom& atom,
                               theory::equality::FlatTermId true_flat,
                               theory::equality::FlatTermId false_flat) {
  if (atom.equality) {
    if (atom.source.value) {
      closure.assert_equal(atom.lhs, atom.rhs, {blocking_lit(atom.source)});
    } else {
      closure.assert_distinct(atom.lhs, atom.rhs, {blocking_lit(atom.source)});
    }
    return;
  }
  closure.assert_equal(atom.lhs, atom.source.value ? true_flat : false_flat,
                       {blocking_lit(atom.source)});
}

std::optional<std::vector<sat::Lit>> flat_candidate_explanation(
    theory::equality::FlatCongruenceClosure& closure, const FlatAssignedTheoryAtom& atom,
    theory::equality::FlatTermId true_flat, theory::equality::FlatTermId false_flat) {
  if (atom.equality) {
    if (!atom.source.value) {
      return std::nullopt;
    }
    return closure.explain_equal(atom.lhs, atom.rhs);
  }
  return closure.explain_equal(atom.lhs, atom.source.value ? true_flat : false_flat);
}

std::optional<sat::Clause> flat_model_propagation_clause_for(
    const core::TermManager& terms, const std::vector<AssignedTheoryAtom>& atoms,
    std::size_t candidate_index, core::TermId true_term, core::TermId false_term,
    EufExplainSearchMode explain_search_mode, bool proof_forest_diagnostics,
    util::Statistics* statistics) {
  theory::equality::FlatTermIndex flat(terms);
  const theory::equality::FlatTermId true_flat = flat.flatten(true_term);
  const theory::equality::FlatTermId false_flat = flat.flatten(false_term);
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
  flat_atoms.reserve(atoms.size());
  for (const AssignedTheoryAtom& atom : atoms) {
    flat_atoms.push_back(flatten_assigned_atom(flat, terms, atom, false_flat));
  }

  theory::equality::FlatCongruenceClosure closure(
      flat.size(), flat_explanation_search_mode(explain_search_mode), proof_forest_diagnostics);
  for (const theory::equality::FlatDefinition& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_distinct(true_flat, false_flat);
  for (std::size_t i = 0; i < flat_atoms.size(); ++i) {
    if (i == candidate_index) {
      continue;
    }
    const FlatAssignedTheoryAtom& atom = flat_atoms[i];
    if (atom.source.filtered) {
      continue;
    }
    if (atom.equality && !atom.source.value) {
      continue;
    }
    assert_flat_assigned_atom(closure, atom, true_flat, false_flat);
  }

  if (flat_atoms[candidate_index].source.filtered) {
    if (statistics != nullptr) {
      add_incremental_stats(*statistics, closure.stats());
    }
    return std::nullopt;
  }
  std::optional<std::vector<sat::Lit>> reason =
      flat_candidate_explanation(closure, flat_atoms[candidate_index], true_flat, false_flat);
  if (statistics != nullptr) {
    add_incremental_stats(*statistics, closure.stats());
  }
  if (!reason.has_value()) {
    return std::nullopt;
  }
  reason->push_back(assigned_lit(flat_atoms[candidate_index].source));
  return reason;
}

class IncrementalFlatAssignmentState {
 public:
  class ModelScope {
   public:
    ModelScope(IncrementalFlatAssignmentState& state, std::size_t checkpoint)
        : state_(&state), checkpoint_(checkpoint) {}

    ModelScope(const ModelScope&) = delete;
    ModelScope& operator=(const ModelScope&) = delete;

    ModelScope(ModelScope&& other) noexcept
        : state_(std::exchange(other.state_, nullptr)), checkpoint_(other.checkpoint_) {}

    ModelScope& operator=(ModelScope&& other) noexcept {
      if (this != &other) {
        reset();
        state_ = std::exchange(other.state_, nullptr);
        checkpoint_ = other.checkpoint_;
      }
      return *this;
    }

    ~ModelScope() { reset(); }

    FlatEufModelView view() const { return FlatEufModelView{state_->flat_, state_->closure_}; }

   private:
    void reset() {
      if (state_ != nullptr) {
        state_->end_model_scope(checkpoint_);
        state_ = nullptr;
      }
    }

    IncrementalFlatAssignmentState* state_ = nullptr;
    std::size_t checkpoint_ = 0;
  };

  IncrementalFlatAssignmentState(const core::TermManager& terms,
                                 const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                 core::TermId true_term, core::TermId false_term,
                                 PureTheoryFilterMode pure_theory_filter,
                                 EufExplainSearchMode explain_search_mode,
                                 bool proof_forest_diagnostics, util::Statistics& statistics,
                                 bool flush_live_stats,
                                 const PreparedFlatTheory* prepared = nullptr)
      : terms_(terms),
        pure_theory_filter_(pure_theory_filter),
        flat_(prepared ? prepared->flat : theory::equality::FlatTermIndex(terms)),
        closure_(prepared ? prepared->base_closure
                          : theory::equality::FlatCongruenceClosure(
                                0, flat_explanation_search_mode(explain_search_mode),
                                proof_forest_diagnostics)),
        statistics_(statistics),
        flush_live_stats_(flush_live_stats) {
    // Original session design: docs/assumption-incremental-uf.md. Copy only
    // structural definitions; no preceding query's assignments or reasons.
    if (prepared != nullptr) {
      true_flat_ = prepared->true_flat;
      false_flat_ = prepared->false_flat;
      flat_atoms_ = prepared->flat_atoms;
      if (flat_atoms_.size() != theory_atoms.size())
        throw std::logic_error("prepared EUF atom count mismatch");
      for (std::size_t i = 0; i < flat_atoms_.size(); ++i) {
        if (flat_atoms_[i].source.term != theory_atoms[i].term ||
            flat_atoms_[i].source.lit != theory_atoms[i].lit.value)
          throw std::logic_error("prepared EUF atom identity mismatch");
        flat_atom_by_var_.emplace(std::abs(theory_atoms[i].lit.value), i);
      }
      publish_stats();
      return;
    }
    true_flat_ = flat_.flatten(true_term);
    false_flat_ = flat_.flatten(false_term);
    flat_atoms_.reserve(theory_atoms.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      flat_atom_by_var_.emplace(var, flat_atoms_.size());
      flat_atoms_.push_back(flatten_assigned_atom(
          flat_, terms_, AssignedTheoryAtom{atom.lit.value, false, atom.term}, false_flat_));
    }

    initialize_base_closure();
    publish_stats();
  }

  void push_level() { checkpoints_.push_back(Checkpoint{std::nullopt}); }

  void pop_to_level(std::size_t new_level) {
    if (checkpoints_.size() <= new_level) {
      return;
    }
    std::optional<std::size_t> closure_checkpoint;
    for (std::size_t index = new_level; index < checkpoints_.size(); ++index) {
      if (checkpoints_[index].closure_checkpoint.has_value()) {
        closure_checkpoint = checkpoints_[index].closure_checkpoint;
        break;
      }
    }
    checkpoints_.resize(new_level);
    if (closure_checkpoint.has_value()) {
      publish_stats();
      closure_.pop(closure_checkpoint.value());
      last_published_stats_ = closure_.stats();
      ++accumulated_stats_.backtracks;
      ++unflushed_publications_;
      if (flush_live_stats_ && unflushed_publications_ >= kLiveStatsFlushInterval) {
        flush_stats();
      }
    }
  }

  void assert_assignment(const AssignedTheoryAtom& assignment) {
    if (assignment.filtered) {
      return;
    }
    std::optional<FlatAssignedTheoryAtom> flat_atom = flat_atom_for_lit(assignment.lit);
    if (!flat_atom.has_value()) {
      return;
    }
    ensure_current_level_checkpoint();
    flat_atom->source.value = assignment.value;
    flat_atom->source.filtered = assignment.filtered;
    assert_flat_assigned_atom(closure_, flat_atom.value(), true_flat_, false_flat_);
    ++assignments_since_publication_;
    if (flush_live_stats_ && assignments_since_publication_ >= kLiveStatsFlushInterval) {
      publish_stats();
      flush_stats();
    }
  }

  std::optional<std::size_t> register_existing_equality_atom(
      const BooleanClausifier::TheoryAtom& atom) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    if (flat_atom_by_var_.contains(var)) {
      return flat_atom_by_var_.at(var);
    }
    const core::Term& term = terms_.get(atom.term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
      return std::nullopt;
    }
    const std::optional<theory::equality::FlatTermId> lhs =
        flat_.existing_flat_term(term.children.at(0));
    const std::optional<theory::equality::FlatTermId> rhs =
        flat_.existing_flat_term(term.children.at(1));
    if (!lhs.has_value() || !rhs.has_value()) {
      return std::nullopt;
    }
    const std::size_t index = flat_atoms_.size();
    flat_atom_by_var_.emplace(var, index);
    flat_atoms_.push_back(FlatAssignedTheoryAtom{
        AssignedTheoryAtom{atom.lit.value, false, atom.term}, true, lhs.value(), rhs.value()});
    return index;
  }

  std::optional<sat::Clause> explain_candidate(const BooleanClausifier::TheoryAtom& atom,
                                               bool value) {
    if (should_filter_assignment(pure_theory_filter_, atom, value)) {
      return std::nullopt;
    }
    std::optional<FlatAssignedTheoryAtom> flat_atom = flat_atom_for_lit(atom.lit.value);
    if (!flat_atom.has_value()) {
      return std::nullopt;
    }
    flat_atom->source.value = value;
    if (flat_atom->equality && !value) {
      return std::nullopt;
    }
    std::optional<std::vector<sat::Lit>> reason =
        flat_candidate_explanation(closure_, flat_atom.value(), true_flat_, false_flat_);
    if (!reason.has_value()) {
      return std::nullopt;
    }
    statistics_.increment(kStatEufIncrementalExplainLength, reason->size());
    reason->push_back(assigned_lit(flat_atom->source));
    return reason;
  }

  void drain_changed_terms(std::vector<theory::equality::FlatTermId>& out) {
    closure_.drain_changed_terms(out);
  }

  const std::vector<FlatAssignedTheoryAtom>& flat_atoms() const { return flat_atoms_; }

  std::size_t flat_term_count() const { return flat_.size(); }

  std::optional<sat::Clause> conflict() const { return closure_.conflict(); }

  bool model_scope_active() const { return model_scope_active_; }

  ModelScope begin_model_scope() {
    if (model_scope_active_) {
      throw std::logic_error("EUF model scope is already active");
    }
    publish_stats();
    const std::size_t checkpoint = closure_.push();
    closure_.protect_checkpoint(checkpoint);
    model_scope_active_ = true;
    return ModelScope(*this, checkpoint);
  }

  void flush_stats() {
    publish_stats();
    add_incremental_stats(statistics_, accumulated_stats_);
    accumulated_stats_ = {};
    unflushed_publications_ = 0;
  }

 private:
  void end_model_scope(std::size_t checkpoint) {
    if (closure_.checkpoint_depth() <= checkpoint) {
      throw std::logic_error("EUF model scope popped a persistent SAT-trail checkpoint");
    }
    closure_.release_protected_checkpoint(checkpoint);
    closure_.pop(checkpoint);
    // Model-reconciliation probes are deliberately excluded from the SAT-trail
    // incremental counters. Advancing the watermark prevents their cumulative
    // closure statistics from being published on the next assignment.
    last_published_stats_ = closure_.stats();
    model_scope_active_ = false;
  }

  void initialize_base_closure() {
    closure_.ensure_node_count(flat_.size());
    for (const theory::equality::FlatDefinition& definition : flat_.definitions()) {
      closure_.add_definition(definition);
    }
    closure_.assert_distinct(true_flat_, false_flat_);
    std::vector<theory::equality::FlatTermId> ignored_changed_terms;
    closure_.drain_changed_terms(ignored_changed_terms);
  }

  std::optional<FlatAssignedTheoryAtom> flat_atom_for_lit(int lit) const {
    const int var = lit > 0 ? lit : -lit;
    const auto found = flat_atom_by_var_.find(var);
    if (found == flat_atom_by_var_.end()) {
      return std::nullopt;
    }
    return flat_atoms_[found->second];
  }

  void publish_stats() {
    const theory::equality::IncrementalCongruenceClosureStats current = closure_.stats();
    accumulate_stats(stats_delta(current, last_published_stats_));
    last_published_stats_ = current;
    ++unflushed_publications_;
    assignments_since_publication_ = 0;
  }

  void accumulate_stats(const theory::equality::IncrementalCongruenceClosureStats& stats) {
    accumulated_stats_.terms += stats.terms;
    accumulated_stats_.flat_definitions += stats.flat_definitions;
    accumulated_stats_.merges += stats.merges;
    accumulated_stats_.pending_pops += stats.pending_pops;
    accumulated_stats_.lookup_probes += stats.lookup_probes;
    accumulated_stats_.lookup_hits += stats.lookup_hits;
    accumulated_stats_.congruence_merges += stats.congruence_merges;
    accumulated_stats_.disequality_checks += stats.disequality_checks;
    accumulated_stats_.conflicts += stats.conflicts;
    accumulated_stats_.explain_calls += stats.explain_calls;
    accumulated_stats_.explain_cache_hits += stats.explain_cache_hits;
    accumulated_stats_.explain_cache_bound_misses += stats.explain_cache_bound_misses;
    accumulated_stats_.explain_length += stats.explain_length;
    accumulated_stats_.explain_top_level_queries += stats.explain_top_level_queries;
    accumulated_stats_.explain_recursive_queries += stats.explain_recursive_queries;
    accumulated_stats_.explain_repeated_pair_queries += stats.explain_repeated_pair_queries;
    accumulated_stats_.explain_max_recursion_depth =
        std::max(accumulated_stats_.explain_max_recursion_depth, stats.explain_max_recursion_depth);
    accumulated_stats_.explain_bfs_searches += stats.explain_bfs_searches;
    accumulated_stats_.explain_bfs_visited += stats.explain_bfs_visited;
    accumulated_stats_.explain_bfs_path_edges += stats.explain_bfs_path_edges;
    accumulated_stats_.explain_bfs_max_queue =
        std::max(accumulated_stats_.explain_bfs_max_queue, stats.explain_bfs_max_queue);
    accumulated_stats_.explain_proof_forest_queries += stats.explain_proof_forest_queries;
    accumulated_stats_.explain_proof_forest_path_edges += stats.explain_proof_forest_path_edges;
    accumulated_stats_.explain_proof_forest_max_path_edges =
        std::max(accumulated_stats_.explain_proof_forest_max_path_edges,
                 stats.explain_proof_forest_max_path_edges);
    accumulated_stats_.explain_proof_forest_missing_lca += stats.explain_proof_forest_missing_lca;
    accumulated_stats_.explain_proof_forest_shorter_than_bfs +=
        stats.explain_proof_forest_shorter_than_bfs;
    accumulated_stats_.explain_proof_forest_equal_to_bfs += stats.explain_proof_forest_equal_to_bfs;
    accumulated_stats_.explain_proof_forest_longer_than_bfs +=
        stats.explain_proof_forest_longer_than_bfs;
    accumulated_stats_.explain_causal_edge_skips += stats.explain_causal_edge_skips;
    accumulated_stats_.explain_direct_edges += stats.explain_direct_edges;
    accumulated_stats_.explain_congruence_edges += stats.explain_congruence_edges;
    accumulated_stats_.explain_reason_duplicate_literals += stats.explain_reason_duplicate_literals;
    accumulated_stats_.conflict_reason_max_length =
        std::max(accumulated_stats_.conflict_reason_max_length, stats.conflict_reason_max_length);
    accumulated_stats_.conflict_reason_length_le_4 += stats.conflict_reason_length_le_4;
    accumulated_stats_.conflict_reason_length_le_8 += stats.conflict_reason_length_le_8;
    accumulated_stats_.conflict_reason_length_le_16 += stats.conflict_reason_length_le_16;
    accumulated_stats_.conflict_reason_length_le_32 += stats.conflict_reason_length_le_32;
    accumulated_stats_.conflict_reason_length_le_64 += stats.conflict_reason_length_le_64;
    accumulated_stats_.conflict_reason_length_gt_64 += stats.conflict_reason_length_gt_64;
    accumulated_stats_.backtracks += stats.backtracks;
    accumulated_stats_.trail_entries += stats.trail_entries;
    accumulated_stats_.class_member_trail_entries_saved += stats.class_member_trail_entries_saved;
    accumulated_stats_.use_list_appends += stats.use_list_appends;
    accumulated_stats_.use_list_duplicate_skips += stats.use_list_duplicate_skips;
    accumulated_stats_.affected_use_list_dedup += stats.affected_use_list_dedup;
    accumulated_stats_.max_use_list_size =
        std::max(accumulated_stats_.max_use_list_size, stats.max_use_list_size);
    accumulated_stats_.changed_term_drains += stats.changed_term_drains;
    accumulated_stats_.changed_terms_delivered += stats.changed_terms_delivered;
    accumulated_stats_.changed_term_generation_rebases += stats.changed_term_generation_rebases;
    accumulated_stats_.max_checkpoint_depth =
        std::max(accumulated_stats_.max_checkpoint_depth, stats.max_checkpoint_depth);
  }

  void ensure_current_level_checkpoint() {
    if (checkpoints_.empty()) {
      return;
    }
    Checkpoint& checkpoint = checkpoints_.back();
    if (!checkpoint.closure_checkpoint.has_value()) {
      checkpoint.closure_checkpoint = closure_.push();
    }
  }

  const core::TermManager& terms_;
  PureTheoryFilterMode pure_theory_filter_ = PureTheoryFilterMode::Auto;
  struct Checkpoint {
    std::optional<std::size_t> closure_checkpoint;
  };

  theory::equality::FlatTermIndex flat_;
  theory::equality::FlatCongruenceClosure closure_;
  util::Statistics& statistics_;
  theory::equality::FlatTermId true_flat_ = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat_ = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms_;
  std::unordered_map<int, std::size_t> flat_atom_by_var_;
  std::vector<Checkpoint> checkpoints_;
  bool flush_live_stats_ = false;
  std::size_t unflushed_publications_ = 0;
  std::size_t assignments_since_publication_ = 0;
  theory::equality::IncrementalCongruenceClosureStats accumulated_stats_;
  theory::equality::IncrementalCongruenceClosureStats last_published_stats_;
  bool model_scope_active_ = false;
};

struct ModelPropagationResult {
  std::vector<sat::Clause> clauses;
  std::size_t candidates = 0;
  std::size_t reason_lits = 0;
  std::size_t candidate_budget = 0;
  std::size_t delivery_budget = 0;
  bool budget_cutoff = false;
  bool hit_rate_cutoff = false;
};

#if PRIMO_HAVE_IPASIR
class OnlineEufPropagator final : public sat::ExternalPropagator {
 public:
  OnlineEufPropagator(const core::TermManager& terms,
                      const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                      core::TermId true_term, core::TermId false_term,
                      const CheckSatOptions& options, bool adaptive_propagation,
                      util::Statistics& statistics, const PreparedFlatTheory* prepared = nullptr,
                      bool count_conflicts = true)
      : terms_(terms),
        theory_atoms_(theory_atoms),
        true_term_(true_term),
        false_term_(false_term),
        options_(options),
        adaptive_propagation_(adaptive_propagation),
        statistics_(statistics),
        count_conflicts_(count_conflicts) {
    int max_theory_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      max_theory_var =
          std::max(max_theory_var, atom.lit.value > 0 ? atom.lit.value : -atom.lit.value);
    }
    observed_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    atom_index_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, std::nullopt);
    pending_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    queued_candidate_indices_.assign(theory_atoms_.size(), 0);
    for (std::size_t atom_index = 0; atom_index < theory_atoms_.size(); ++atom_index) {
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const std::size_t index = static_cast<std::size_t>(var);
      observed_vars_[index] = 1;
      atom_index_by_var_[index] = atom_index;
    }
    if (options_.euf_dynamic_ackermann_shadow || options_.euf_diseq_ackermann) {
      dynamic_ackermann_candidates_ =
          build_dynamic_ackermann_candidate_index(terms_, theory_atoms_);
    }
    if (options_.euf_diseq_ackermann && options_.euf_propagation != EufPropagationMode::Conflict) {
      build_diseq_ackermann_watch_index();
    }
    if (options_.euf_engine == EufEngine::Incremental) {
      incremental_state_.emplace(terms_, theory_atoms_, true_term_, false_term_,
                                 options_.pure_theory_filter, options_.euf_explain_search,
                                 options_.euf_proof_forest_diagnostics, statistics_,
                                 /*flush_live_stats=*/false, prepared);
      build_candidate_index();
    }
  }

  bool observe_generated_interface_equality(const BooleanClausifier::TheoryAtom& generated_atom) {
    const int var =
        generated_atom.lit.value > 0 ? generated_atom.lit.value : -generated_atom.lit.value;
    ensure_var_capacity(var);
    const std::size_t var_index = static_cast<std::size_t>(var);
    if (atom_index_by_var_[var_index].has_value()) {
      const BooleanClausifier::TheoryAtom& existing =
          atom_at(atom_index_by_var_[var_index].value());
      return existing.term == generated_atom.term && existing.lit.value == generated_atom.lit.value;
    }
    std::optional<std::size_t> flat_atom_index;
    if (incremental_state_.has_value()) {
      flat_atom_index = incremental_state_->register_existing_equality_atom(generated_atom);
      if (!flat_atom_index.has_value()) {
        generated_registration_valid_ = false;
        statistics_.increment(":no.online.euf-generated-equality-registration-failures");
        return false;
      }
    }
    const std::size_t atom_index = atom_count();
    generated_theory_atoms_.push_back(generated_atom);
    atom_index_by_var_[var_index] = atom_index;
    observed_vars_[var_index] = 1;
    queued_candidate_indices_.push_back(0);
    if (flat_atom_index.has_value()) {
      const FlatAssignedTheoryAtom& flat_atom =
          incremental_state_->flat_atoms()[flat_atom_index.value()];
      if (candidate_indices_by_flat_term_.size() < incremental_state_->flat_term_count()) {
        candidate_indices_by_flat_term_.resize(incremental_state_->flat_term_count());
      }
      candidate_indices_by_flat_term_[flat_atom.lhs].push_back(atom_index);
      if (flat_atom.rhs != flat_atom.lhs) {
        candidate_indices_by_flat_term_[flat_atom.rhs].push_back(atom_index);
      }
    }
    statistics_.increment(":no.online.euf-generated-equalities-registered");
    return true;
  }

  bool assignment_notifications_deferred() const override { return model_scope_active(); }
  void notify_assignment(const std::vector<int>& lits) override { apply_assignment(lits); }
  void notify_new_decision_level() override { apply_new_decision_level(); }
  void notify_backtrack(std::size_t) override {
    throw std::logic_error("EUF backtrack requires the shared removal delta");
  }
  void notify_backtrack_delta(std::size_t level, const std::vector<int>& removed) override {
    apply_backtrack(level, removed);
  }

 private:
  bool model_scope_active() const {
    return incremental_state_.has_value() && incremental_state_->model_scope_active();
  }

  void apply_assignment(const std::vector<int>& lits) {
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
    finish_assignment_notification();
  }

  void apply_new_decision_level() {
    if (incremental_state_.has_value()) {
      incremental_state_->push_level();
    }
  }

  void apply_backtrack(std::size_t new_level, const std::vector<int>& removed) {
    for (const int lit : removed) {
      clear_pending_var(lit > 0 ? lit : -lit);
      reasons_.erase(lit);
      reasons_.erase(-lit);
    }
    if (incremental_state_.has_value()) {
      incremental_state_->pop_to_level(new_level);
    }
    clear_pending_queue();
    clear_all_pending_vars();
    candidate_queue_.clear();
    clear_all_candidate_marks();
    diseq_ackermann_queue_.clear();
    clear_all_diseq_ackermann_marks();
    external_clause_.clear();
    external_clause_index_ = 0;
  }

 public:
  bool cb_check_found_model(const std::vector<int>& model) override {
    (void)model;
    if (conflict_budget_exhausted()) return true;
    for (std::size_t i = 0; i < atom_count(); ++i) {
      if (!is_assigned_var(std::abs(atom_at(i).lit.value)))
        throw std::logic_error("EUF model contains an unnotified observed atom");
    }
    if (incremental_state_.has_value()) {
      incremental_state_->flush_stats();
    }
    return !incremental_state_.has_value() || !incremental_state_->conflict().has_value();
  }

  void flush_incremental_stats() {
    if (incremental_state_.has_value()) {
      incremental_state_->flush_stats();
    }
  }

  // The propagator's incremental closure already asserts and checks every
  // observed-variable assignment synchronously via notify_assignment, and any
  // conflict it finds is delivered to CaDiCaL through cb_has_external_clause
  // before search can report SAT. So once backend.solve() returns SAT, this
  // closure's conflict state is already the authoritative answer for the
  // current full assignment; the caller does not need to rebuild a second
  // closure from scratch to re-derive it.
  std::optional<sat::Clause> incremental_conflict() const {
    return incremental_state_.has_value() ? incremental_state_->conflict() : std::nullopt;
  }

  enum class ModelScopeAvailability {
    Available,
    NoIncrementalState,
    IncompleteAssignment,
    InvalidRegistration,
    Conflict,
  };

  ModelScopeAvailability model_scope_availability() const {
    if (!incremental_state_.has_value()) {
      return ModelScopeAvailability::NoIncrementalState;
    }
    if (!generated_registration_valid_) {
      return ModelScopeAvailability::InvalidRegistration;
    }
    for (std::size_t i = 0; i < atom_count(); ++i) {
      if (!is_assigned_var(std::abs(atom_at(i).lit.value)))
        return ModelScopeAvailability::IncompleteAssignment;
    }
    if (incremental_state_->conflict().has_value()) {
      return ModelScopeAvailability::Conflict;
    }
    return ModelScopeAvailability::Available;
  }

  std::optional<IncrementalFlatAssignmentState::ModelScope> begin_model_scope() {
    if (model_scope_availability() != ModelScopeAvailability::Available) {
      return std::nullopt;
    }
    return incremental_state_->begin_model_scope();
  }

  void record_dynamic_ackermann_shadow_round() {
    std::vector<std::optional<bool>> atom_values;
    atom_values.reserve(theory_atoms_.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        atom_values.push_back(std::nullopt);
        continue;
      }
      const bool assignment = assigned_value(var);
      atom_values.push_back(atom.lit.value > 0 ? assignment : !assignment);
    }
    record_dynamic_ackermann_shadow(dynamic_ackermann_candidates_, atom_values, statistics_);
  }

  int cb_propagate() override {
    if (conflict_budget_exhausted()) return 0;
    if (options_.euf_propagation == EufPropagationMode::Conflict) {
      return 0;
    }
    if (pending_.empty()) {
      compute_pending_propagations();
    }
    // The scan can consume the final conflict budget after queuing a unit.
    if (conflict_budget_exhausted() || pending_.empty()) {
      return 0;
    }
    const int lit = pending_.front();
    pending_.pop_front();
    clear_pending_var(lit > 0 ? lit : -lit);
    if (const auto found = reasons_.find(lit);
        found != reasons_.end() && found->second.source == ReasonSource::DiseqAckermann) {
      statistics_.increment(kStatEufDiseqAckReturned);
    }
    return lit;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    if (!reasons_.contains(propagated_lit)) {
      if (model_scope_active())
        throw std::logic_error("cannot reconstruct an EUF reason inside a model scope");
      compute_reason_for(propagated_lit);
    }
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      return 0;
    }
    Reason& reason = found->second;
    if (reason.active_lit != propagated_lit) {
      reason.active_lit = propagated_lit;
      reason.index = 0;
    }
    if (reason.index == 0) {
      ++reason.index;
      return propagated_lit;
    }
    const std::size_t antecedent_index = reason.index - 1;
    if (antecedent_index >= reason.antecedents.size()) {
      reason.index = 0;
      reason.active_lit = 0;
      return 0;
    }
    ++reason.index;
    return reason.antecedents[antecedent_index];
  }

  bool cb_has_external_clause(bool& is_forgettable) override {
    maybe_store_conflict();
    maybe_store_diseq_ackermann_conflict();
    if (external_clause_.empty()) {
      return false;
    }
    is_forgettable = false;
    return true;
  }

  bool cb_has_external_clause() override {
    maybe_store_conflict();
    maybe_store_diseq_ackermann_conflict();
    return !external_clause_.empty();
  }

  int cb_add_external_clause_lit() override {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  bool conflict_budget_exhausted() const {
    return count_conflicts_ &&
           theory_conflict_limit_reached(statistics_, options_.max_theory_conflicts);
  }

  enum class ReasonSource {
    EufPropagation,
    DiseqAckermann,
  };

  struct Reason {
    std::vector<int> antecedents;
    std::size_t index = 0;
    int active_lit = 0;
    ReasonSource source = ReasonSource::EufPropagation;
  };

  const BooleanClausifier::TheoryAtom* atom_for_var(int var) const {
    if (var < 0 || static_cast<std::size_t>(var) >= atom_index_by_var_.size()) {
      return nullptr;
    }
    const std::optional<std::size_t> atom_index = atom_index_by_var_[static_cast<std::size_t>(var)];
    return atom_index.has_value() ? &atom_at(atom_index.value()) : nullptr;
  }

  std::size_t atom_count() const { return theory_atoms_.size() + generated_theory_atoms_.size(); }

  const BooleanClausifier::TheoryAtom& atom_at(std::size_t index) const {
    if (index < theory_atoms_.size()) {
      return theory_atoms_[index];
    }
    return generated_theory_atoms_[index - theory_atoms_.size()];
  }

  void ensure_var_capacity(int var) {
    if (var < 0) {
      return;
    }
    const std::size_t size = static_cast<std::size_t>(var) + 1U;
    observed_vars_.resize(std::max(observed_vars_.size(), size), 0);
    atom_index_by_var_.resize(std::max(atom_index_by_var_.size(), size), std::nullopt);
    pending_vars_.resize(std::max(pending_vars_.size(), size), 0);
    diseq_ackermann_indices_by_var_.resize(std::max(diseq_ackermann_indices_by_var_.size(), size));
  }

  bool is_observed_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < observed_vars_.size() &&
           observed_vars_[static_cast<std::size_t>(var)] != 0;
  }

  bool is_assigned_var(int var) const {
    return is_observed_var(var) && assignment_view().literal(var) != 0;
  }

  bool assigned_value(int var) const { return assignment_view().literal(var) > 0; }

  bool literal_is_true(sat::Lit lit) const {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    if (!is_assigned_var(var)) {
      return false;
    }
    const bool assignment = assigned_value(var);
    return lit.value > 0 ? assignment : !assignment;
  }

  bool literal_is_false(sat::Lit lit) const {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    if (!is_assigned_var(var)) {
      return false;
    }
    return !literal_is_true(lit);
  }

  bool is_pending_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size() &&
           pending_vars_[static_cast<std::size_t>(var)] != 0;
  }

  void clear_pending_var(int var) {
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 0;
    }
  }

  void clear_all_pending_vars() { std::fill(pending_vars_.begin(), pending_vars_.end(), 0); }

  void clear_all_candidate_marks() {
    std::fill(queued_candidate_indices_.begin(), queued_candidate_indices_.end(), 0);
  }

  void clear_all_diseq_ackermann_marks() {
    std::fill(queued_diseq_ackermann_candidates_.begin(), queued_diseq_ackermann_candidates_.end(),
              0);
  }

  void notify_assignment_lit(int lit) {
    const int var = lit > 0 ? lit : -lit;
    if (!is_observed_var(var)) {
      return;
    }
    clear_pending_var(var);
    enqueue_diseq_ackermann_candidates(var);
    if (!incremental_state_.has_value()) {
      return;
    }
    const BooleanClausifier::TheoryAtom* atom = atom_for_var(var);
    if (atom == nullptr) {
      return;
    }
    const bool atom_value = atom->lit.value > 0 ? lit > 0 : lit < 0;
    statistics_.increment(kStatEufAssignments);
    const core::Term& atom_term = terms_.get(atom->term);
    if (atom_term.kind == core::TermKind::Equality) {
      statistics_.increment(atom_value ? kStatEufEqAssignments : kStatEufDiseqAssignments);
    }
    AssignedTheoryAtom assigned =
        assigned_theory_atom(options_.pure_theory_filter, *atom, atom_value);
    count_filtered_assignment(assigned, statistics_);
    incremental_state_->assert_assignment(assigned);
    enqueue_changed_candidates();
  }

  void clear_pending_queue() {
    for (const int lit : pending_) {
      const auto found = reasons_.find(lit);
      if (found != reasons_.end() && found->second.source == ReasonSource::DiseqAckermann) {
        statistics_.increment(kStatEufDiseqAckPendingCleared);
      }
    }
    pending_.clear();
  }

  void finish_assignment_notification() { clear_pending_queue(); }

  void build_candidate_index() {
    if (!incremental_state_.has_value()) {
      return;
    }
    candidate_indices_by_flat_term_.assign(incremental_state_->flat_term_count(), {});
    const std::vector<FlatAssignedTheoryAtom>& flat_atoms = incremental_state_->flat_atoms();
    for (std::size_t atom_index = 0; atom_index < flat_atoms.size(); ++atom_index) {
      const FlatAssignedTheoryAtom& atom = flat_atoms[atom_index];
      if (atom.lhs < candidate_indices_by_flat_term_.size()) {
        candidate_indices_by_flat_term_[atom.lhs].push_back(atom_index);
      }
      if (atom.rhs != atom.lhs && atom.rhs < candidate_indices_by_flat_term_.size()) {
        candidate_indices_by_flat_term_[atom.rhs].push_back(atom_index);
      }
    }
  }

  void enqueue_candidate(std::size_t atom_index) {
    if (atom_index >= atom_count()) {
      return;
    }
    if (queued_candidate_indices_[atom_index] == 0) {
      queued_candidate_indices_[atom_index] = 1;
      candidate_queue_.push_back(atom_index);
    }
  }

  void enqueue_changed_candidates() {
    if (!incremental_state_.has_value()) {
      return;
    }
    changed_terms_scratch_.clear();
    incremental_state_->drain_changed_terms(changed_terms_scratch_);
    for (const theory::equality::FlatTermId term : changed_terms_scratch_) {
      if (term >= candidate_indices_by_flat_term_.size()) {
        continue;
      }
      for (const std::size_t atom_index : candidate_indices_by_flat_term_[term]) {
        enqueue_candidate(atom_index);
      }
    }
  }

  void build_diseq_ackermann_watch_index() {
    queued_diseq_ackermann_candidates_.assign(dynamic_ackermann_candidates_.candidates.size(), 0);
    diseq_ackermann_indices_by_var_.assign(observed_vars_.size(), {});
    statistics_.increment(kStatEufDiseqAckMissingArgAtom,
                          dynamic_ackermann_candidates_.missing_argument_atoms);
    std::unordered_map<core::SymbolId, std::size_t> candidates_by_symbol;
    candidates_by_symbol.reserve(dynamic_ackermann_candidates_.candidates.size());
    std::size_t watched_candidates = 0;
    for (std::size_t candidate_index = 0;
         candidate_index < dynamic_ackermann_candidates_.candidates.size(); ++candidate_index) {
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      if (candidate.width > options_.euf_diseq_ackermann_max_width) {
        statistics_.increment(kStatEufDiseqAckWidthSkips);
        continue;
      }
      std::size_t& symbol_count = candidates_by_symbol[candidate.symbol];
      if (symbol_count >= options_.euf_diseq_ackermann_per_symbol) {
        statistics_.increment(kStatEufDiseqAckPerSymbolCutoffs);
        continue;
      }
      ++symbol_count;
      ++watched_candidates;
      for (const sat::Lit lit : candidate.clause) {
        const int var = lit.value > 0 ? lit.value : -lit.value;
        if (var < 0 || static_cast<std::size_t>(var) >= diseq_ackermann_indices_by_var_.size()) {
          continue;
        }
        std::vector<std::size_t>& watched =
            diseq_ackermann_indices_by_var_[static_cast<std::size_t>(var)];
        if (std::find(watched.begin(), watched.end(), candidate_index) == watched.end()) {
          watched.push_back(candidate_index);
        }
      }
    }
    statistics_.set(kStatEufDiseqAckCandidates, watched_candidates);
  }

  void enqueue_diseq_ackermann_candidate(std::size_t candidate_index) {
    if (candidate_index >= queued_diseq_ackermann_candidates_.size()) {
      return;
    }
    if (queued_diseq_ackermann_candidates_[candidate_index] != 0) {
      return;
    }
    queued_diseq_ackermann_candidates_[candidate_index] = 1;
    diseq_ackermann_queue_.push_back(candidate_index);
    statistics_.increment(kStatEufDiseqAckQueuePushes);
  }

  void enqueue_diseq_ackermann_candidates(int var) {
    if (!options_.euf_diseq_ackermann || var < 0 ||
        static_cast<std::size_t>(var) >= diseq_ackermann_indices_by_var_.size()) {
      return;
    }
    for (const std::size_t candidate_index :
         diseq_ackermann_indices_by_var_[static_cast<std::size_t>(var)]) {
      enqueue_diseq_ackermann_candidate(candidate_index);
    }
  }

  std::vector<AssignedTheoryAtom> current_assigned_atoms() const {
    std::vector<AssignedTheoryAtom> atoms;
    atoms.reserve(atom_count());
    for (std::size_t atom_index = 0; atom_index < atom_count(); ++atom_index) {
      const BooleanClausifier::TheoryAtom& atom = atom_at(atom_index);
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        continue;
      }
      const bool assignment = assigned_value(var);
      const bool atom_value = atom.lit.value > 0 ? assignment : !assignment;
      AssignedTheoryAtom assigned =
          assigned_theory_atom(options_.pure_theory_filter, atom, atom_value);
      atoms.push_back(assigned);
    }
    return atoms;
  }

  bool reason_is_valid_for_current_assignment(int propagated_lit, const sat::Clause& clause) const {
    if (clause.empty()) {
      return false;
    }
    std::size_t propagated_count = 0;
    for (const sat::Lit lit : clause) {
      if (lit.value == propagated_lit) {
        ++propagated_count;
        continue;
      }
      const int var = lit.value > 0 ? lit.value : -lit.value;
      if (!is_observed_var(var)) {
        return false;
      }
      if (!is_assigned_var(var)) {
        return false;
      }
      const bool assignment = assigned_value(var);
      const bool literal_is_true = lit.value > 0 ? assignment : !assignment;
      if (literal_is_true) {
        return false;
      }
    }
    return propagated_count == 1;
  }

  bool store_reason(int propagated_lit, const sat::Clause& clause,
                    ReasonSource source = ReasonSource::EufPropagation) {
    if (!reason_is_valid_for_current_assignment(propagated_lit, clause)) {
      return false;
    }
    std::vector<int> antecedents;
    antecedents.reserve(clause.size());
    for (const sat::Lit lit : clause) {
      if (lit.value != propagated_lit) {
        antecedents.push_back(lit.value);
      }
    }
    reasons_[propagated_lit] = Reason{std::move(antecedents), 0, 0, source};
    return true;
  }

  void compute_reason_for(int propagated_lit) {
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    const BooleanClausifier::TheoryAtom* atom = atom_for_var(var);
    if (atom == nullptr) {
      return;
    }
    const bool value = propagated_lit == atom->lit.value;
    const core::Term& term = terms_.get(atom->term);
    if (term.kind == core::TermKind::Equality && !value) {
      return;
    }

    std::vector<AssignedTheoryAtom> atoms;
    for (const AssignedTheoryAtom& assigned : current_assigned_atoms()) {
      const int assigned_var = assigned.lit > 0 ? assigned.lit : -assigned.lit;
      if (assigned_var != var) {
        atoms.push_back(assigned);
      }
    }
    atoms.push_back(assigned_theory_atom(options_.pure_theory_filter, *atom, value));
    std::optional<sat::Clause> clause =
        options_.euf_engine == EufEngine::Incremental
            ? flat_model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_,
                                                false_term_, options_.euf_explain_search,
                                                options_.euf_proof_forest_diagnostics, &statistics_)
            : model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_,
                                           false_term_);
    if (clause.has_value()) {
      (void)store_reason(propagated_lit, clause.value());
    }
  }

  void compute_pending_propagations() {
    statistics_.increment(kStatEufPropagationRounds);
    const std::size_t round = statistics_.get(kStatEufPropagationRounds);
    if (round < next_propagation_round_) {
      return;
    }
    next_propagation_round_ = round + propagation_interval_;
    if (options_.euf_dynamic_ackermann_shadow) {
      record_dynamic_ackermann_shadow_round();
    }
    compute_pending_diseq_ackermann_propagations();
    if (!pending_.empty() || !external_clause_.empty()) {
      return;
    }
    const std::vector<AssignedTheoryAtom> assigned = current_assigned_atoms();
    const std::size_t theory_atoms = atom_count();
    if (theory_atoms > 0 &&
        static_cast<double>(assigned.size()) / static_cast<double>(theory_atoms) >
            options_.euf_propagation_assign_threshold) {
      statistics_.increment(kStatEufPropagationHitRateCutoffs);
      backoff_propagation_interval();
      return;
    }
    const std::optional<std::size_t> candidate_limit =
        resolve_budget(options_.euf_propagation_budget, theory_atoms);
    const std::size_t candidate_budget = candidate_limit.value_or(theory_atoms);
    const std::optional<std::size_t> delivery_limit =
        resolve_delivery_budget(options_.euf_propagation_delivery, candidate_budget, theory_atoms);
    statistics_.increment(kStatEufPropagationBudget, candidate_limit.value_or(0));
    statistics_.increment(kStatEufPropagationDeliveryBudget, delivery_limit.value_or(0));

    std::size_t inspected = 0;
    std::size_t delivered = 0;
    constexpr std::size_t kMinInspectionsBeforeHitRateCutoff = 16;
    auto try_atom = [&](const BooleanClausifier::TheoryAtom& atom) {
      if (candidate_limit.has_value() && inspected >= candidate_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        return false;
      }
      if (delivery_limit.has_value() && delivered >= delivery_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        return false;
      }
      if (inspected >= kMinInspectionsBeforeHitRateCutoff && delivered == 0 &&
          options_.euf_propagation_budget.kind == AutoBudget::Kind::Auto) {
        statistics_.increment(kStatEufPropagationHitRateCutoffs);
        backoff_propagation_interval();
        return false;
      }

      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var) || is_pending_var(var)) {
        return true;
      }
      ++inspected;
      if (try_online_candidate(assigned, atom, true) ||
          try_online_candidate(assigned, atom, false)) {
        ++delivered;
      }
      return true;
    };

    const bool had_indexed_candidates = !candidate_queue_.empty();
    while (!candidate_queue_.empty()) {
      const std::size_t atom_index = candidate_queue_.front();
      candidate_queue_.pop_front();
      queued_candidate_indices_[atom_index] = 0;
      if (!try_atom(atom_at(atom_index))) {
        break;
      }
    }

    if (!had_indexed_candidates || (delivered == 0 && candidate_queue_.empty())) {
      for (std::size_t atom_index = 0; atom_index < atom_count(); ++atom_index) {
        if (!try_atom(atom_at(atom_index))) {
          break;
        }
      }
    }
    if (delivered == 0) {
      backoff_propagation_interval();
    } else {
      propagation_interval_ = options_.euf_propagation_interval;
    }
    statistics_.increment(kStatEufPropagationCandidates, inspected);
  }

  void backoff_propagation_interval() {
    if (!adaptive_propagation_) {
      return;
    }
    constexpr std::size_t kMaxAdaptivePropagationInterval = 1024;
    propagation_interval_ = std::min(kMaxAdaptivePropagationInterval, propagation_interval_ * 2);
  }

  bool try_online_candidate(const std::vector<AssignedTheoryAtom>& assigned,
                            const BooleanClausifier::TheoryAtom& atom, bool value) {
    const core::Term& term = terms_.get(atom.term);
    if (term.kind == core::TermKind::Equality && !value) {
      return false;
    }
    std::optional<sat::Clause> clause;
    if (incremental_state_.has_value()) {
      clause = incremental_state_->explain_candidate(atom, value);
    } else {
      std::vector<AssignedTheoryAtom> atoms = assigned;
      atoms.push_back(assigned_theory_atom(options_.pure_theory_filter, atom, value));
      clause =
          model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_, false_term_);
    }
    if (!clause.has_value()) {
      return false;
    }
    const int propagated_lit = value ? atom.lit.value : -atom.lit.value;
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    if (!store_reason(propagated_lit, clause.value())) {
      return false;
    }
    pending_.push_back(propagated_lit);
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 1;
    }
    statistics_.increment(kStatEufPropagationDelivered);
    statistics_.increment(kStatEufPropagationReasonLits,
                          reasons_[propagated_lit].antecedents.size());
    return true;
  }

  struct DiseqAckermannClauseState {
    bool active = false;
    bool satisfied = false;
    bool conflict = false;
    int propagated_lit = 0;
  };

  DiseqAckermannClauseState evaluate_diseq_ackermann_candidate(
      const DynamicAckermannCandidate& candidate) const {
    if (candidate.clause.empty() || !literal_is_false(candidate.clause.front())) {
      return DiseqAckermannClauseState{};
    }

    DiseqAckermannClauseState state;
    state.active = true;
    std::size_t unassigned_literals = 0;
    int unassigned_lit = 0;
    for (std::size_t lit_index = 1; lit_index < candidate.clause.size(); ++lit_index) {
      const sat::Lit lit = candidate.clause[lit_index];
      const int var = lit.value > 0 ? lit.value : -lit.value;
      if (literal_is_true(lit)) {
        state.satisfied = true;
        return state;
      }
      if (!is_assigned_var(var)) {
        if (is_pending_var(var)) {
          return state;
        }
        ++unassigned_literals;
        unassigned_lit = lit.value;
      }
    }

    if (unassigned_literals == 0) {
      state.conflict = true;
    } else if (unassigned_literals == 1) {
      state.propagated_lit = unassigned_lit;
    }
    return state;
  }

  bool store_diseq_ackermann_propagation(const DynamicAckermannCandidate& candidate,
                                         int propagated_lit) {
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    if (!store_reason(propagated_lit, candidate.clause, ReasonSource::DiseqAckermann)) {
      return false;
    }
    pending_.push_back(propagated_lit);
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 1;
    }
    statistics_.increment(kStatEufDiseqAckPropagated);
    statistics_.increment(kStatEufDiseqAckReasonLits, reasons_[propagated_lit].antecedents.size());
    statistics_.increment(kStatEufPropagationDelivered);
    statistics_.increment(kStatEufPropagationReasonLits,
                          reasons_[propagated_lit].antecedents.size());
    return true;
  }

  void compute_pending_diseq_ackermann_propagations() {
    if (!options_.euf_diseq_ackermann || diseq_ackermann_queue_.empty()) {
      return;
    }

    const std::optional<std::size_t> delivery_limit = resolve_delivery_budget(
        options_.euf_propagation_delivery, options_.euf_diseq_ackermann_queue_budget, atom_count());
    std::size_t inspected = 0;
    std::size_t delivered = 0;
    while (!diseq_ackermann_queue_.empty()) {
      if (inspected >= options_.euf_diseq_ackermann_queue_budget) {
        statistics_.increment(kStatEufDiseqAckQueueBudgetCutoffs);
        break;
      }
      if (delivery_limit.has_value() && delivered >= delivery_limit.value()) {
        break;
      }
      const std::size_t candidate_index = diseq_ackermann_queue_.front();
      diseq_ackermann_queue_.pop_front();
      if (candidate_index < queued_diseq_ackermann_candidates_.size()) {
        queued_diseq_ackermann_candidates_[candidate_index] = 0;
      }
      if (candidate_index >= dynamic_ackermann_candidates_.candidates.size()) {
        continue;
      }
      ++inspected;
      statistics_.increment(kStatEufDiseqAckQueuePops);
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      const DiseqAckermannClauseState state = evaluate_diseq_ackermann_candidate(candidate);
      if (!state.active) {
        continue;
      }
      if (state.satisfied) {
        statistics_.increment(kStatEufDiseqAckSatisfiedSkips);
        continue;
      }
      if (state.conflict) {
        if (external_clause_.empty()) {
          external_clause_ = candidate.clause;
          external_clause_index_ = 0;
          statistics_.increment(kStatEufDiseqAckConflict);
          statistics_.increment(kStatTheoryConflicts);
          if (count_conflicts_) record_theory_conflict(statistics_);
        }
        break;
      }
      if (state.propagated_lit == 0) {
        continue;
      }
      statistics_.increment(kStatEufDiseqAckUnit);
      if (store_diseq_ackermann_propagation(candidate, state.propagated_lit)) {
        ++delivered;
      }
    }
    statistics_.increment(kStatEufPropagationCandidates, inspected);
  }

  void maybe_store_conflict() {
    if (conflict_budget_exhausted()) return;
    if (!external_clause_.empty() || !incremental_state_.has_value()) {
      return;
    }
    std::optional<sat::Clause> conflict = incremental_state_->conflict();
    if (!conflict.has_value()) {
      return;
    }
    external_clause_ = std::move(conflict.value());
    external_clause_index_ = 0;
    statistics_.increment(kStatTheoryConflicts);
    if (count_conflicts_) record_theory_conflict(statistics_);
  }

  void maybe_store_diseq_ackermann_conflict() {
    if (conflict_budget_exhausted()) return;
    if (!external_clause_.empty() || !options_.euf_diseq_ackermann ||
        diseq_ackermann_queue_.empty()) {
      return;
    }
    std::size_t inspected = 0;
    while (!diseq_ackermann_queue_.empty()) {
      if (inspected >= options_.euf_diseq_ackermann_queue_budget) {
        statistics_.increment(kStatEufDiseqAckQueueBudgetCutoffs);
        return;
      }
      const std::size_t candidate_index = diseq_ackermann_queue_.front();
      diseq_ackermann_queue_.pop_front();
      if (candidate_index < queued_diseq_ackermann_candidates_.size()) {
        queued_diseq_ackermann_candidates_[candidate_index] = 0;
      }
      if (candidate_index >= dynamic_ackermann_candidates_.candidates.size()) {
        continue;
      }
      ++inspected;
      statistics_.increment(kStatEufDiseqAckQueuePops);
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      const DiseqAckermannClauseState state = evaluate_diseq_ackermann_candidate(candidate);
      if (!state.active) {
        continue;
      }
      if (state.satisfied) {
        statistics_.increment(kStatEufDiseqAckSatisfiedSkips);
        continue;
      }
      if (!state.conflict) {
        if (state.propagated_lit != 0) {
          statistics_.increment(kStatEufDiseqAckUnit);
          if (store_diseq_ackermann_propagation(candidate, state.propagated_lit)) {
            return;
          }
        }
        continue;
      }
      external_clause_ = candidate.clause;
      external_clause_index_ = 0;
      statistics_.increment(kStatEufDiseqAckConflict);
      statistics_.increment(kStatTheoryConflicts);
      if (count_conflicts_) record_theory_conflict(statistics_);
      return;
    }
  }

  const core::TermManager& terms_;
  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  std::vector<BooleanClausifier::TheoryAtom> generated_theory_atoms_;
  core::TermId true_term_ = core::kInvalidTermId;
  core::TermId false_term_ = core::kInvalidTermId;
  const CheckSatOptions& options_;
  bool adaptive_propagation_ = false;
  util::Statistics& statistics_;
  bool count_conflicts_;
  std::vector<unsigned char> observed_vars_;
  std::vector<std::optional<std::size_t>> atom_index_by_var_;
  bool generated_registration_valid_ = true;
  std::deque<int> pending_;
  std::vector<unsigned char> pending_vars_;
  std::deque<std::size_t> candidate_queue_;
  std::vector<unsigned char> queued_candidate_indices_;
  std::vector<std::vector<std::size_t>> candidate_indices_by_flat_term_;
  std::vector<theory::equality::FlatTermId> changed_terms_scratch_;
  std::unordered_map<int, Reason> reasons_;
  DynamicAckermannCandidateIndex dynamic_ackermann_candidates_;
  std::deque<std::size_t> diseq_ackermann_queue_;
  std::vector<unsigned char> queued_diseq_ackermann_candidates_;
  std::vector<std::vector<std::size_t>> diseq_ackermann_indices_by_var_;
  std::optional<IncrementalFlatAssignmentState> incremental_state_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
  std::size_t propagation_interval_ = options_.euf_propagation_interval;
  std::size_t next_propagation_round_ = 1;
};
#endif

ModelPropagationResult learn_model_propagations(const core::TermManager& terms,
                                                const std::vector<AssignedTheoryAtom>& atoms,
                                                core::TermId true_term, core::TermId false_term,
                                                const CheckSatOptions& options,
                                                std::unordered_set<std::string>& learned_clauses,
                                                util::Statistics& statistics) {
  ModelPropagationResult result;
  const std::size_t theory_atoms = atoms.size();
  const std::optional<std::size_t> candidate_limit =
      resolve_budget(options.euf_propagation_budget, theory_atoms);
  const std::size_t candidate_budget = candidate_limit.value_or(theory_atoms);
  const std::optional<std::size_t> delivery_limit =
      resolve_delivery_budget(options.euf_propagation_delivery, candidate_budget, theory_atoms);
  result.candidate_budget = candidate_limit.value_or(0);
  result.delivery_budget = delivery_limit.value_or(0);

  constexpr std::size_t kMinInspectionsBeforeHitRateCutoff = 16;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (candidate_limit.has_value() && result.candidates >= candidate_limit.value()) {
      result.budget_cutoff = true;
      break;
    }
    if (delivery_limit.has_value() && result.clauses.size() >= delivery_limit.value()) {
      result.budget_cutoff = true;
      break;
    }
    if (result.candidates >= kMinInspectionsBeforeHitRateCutoff && result.clauses.empty() &&
        options.euf_propagation_budget.kind == AutoBudget::Kind::Auto) {
      result.hit_rate_cutoff = true;
      break;
    }

    ++result.candidates;
    std::optional<sat::Clause> clause =
        options.euf_engine == EufEngine::Incremental
            ? flat_model_propagation_clause_for(terms, atoms, i, true_term, false_term,
                                                options.euf_explain_search,
                                                options.euf_proof_forest_diagnostics, &statistics)
            : model_propagation_clause_for(terms, atoms, i, true_term, false_term);
    if (!clause.has_value()) {
      continue;
    }
    const std::string key = clause_key(clause.value());
    if (!learned_clauses.insert(key).second) {
      continue;
    }
    result.reason_lits += clause->empty() ? 0 : clause->size() - 1;
    result.clauses.push_back(std::move(clause.value()));
  }
  return result;
}

std::optional<sat::Clause> find_theory_conflict(const core::TermManager& terms,
                                                const std::vector<AssignedTheoryAtom>& atoms,
                                                core::TermId true_term, core::TermId false_term) {
  theory::equality::ExplainedCongruenceClosure closure(terms);
  std::vector<AssignedTheoryAtom> disequalities;

  closure.add_term(true_term);
  closure.add_term(false_term);
  for (const AssignedTheoryAtom& atom : atoms) {
    if (atom.filtered) {
      continue;
    }
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      disequalities.push_back(atom);
      continue;
    }
    if (!add_explained_theory_equality(closure, terms, atom, true_term, false_term)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  if (std::optional<std::vector<sat::Lit>> reason = closure.explain_equal(true_term, false_term);
      reason.has_value()) {
    return reason.value();
  }

  for (const AssignedTheoryAtom& atom : disequalities) {
    const core::Term& term = terms.get(atom.term);
    if (std::optional<std::vector<sat::Lit>> reason =
            closure.explain_equal(term.children.at(0), term.children.at(1));
        reason.has_value()) {
      sat::Clause conflict = reason.value();
      conflict.push_back(blocking_lit(atom));
      return conflict;
    }
  }

  return std::nullopt;
}

std::optional<sat::Clause> find_incremental_theory_conflict(
    const PreparedFlatTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms,
    util::Statistics& statistics) {
  if (atoms.size() != prepared.flat_atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  theory::equality::FlatCongruenceClosure closure = prepared.base_closure;
  for (std::size_t i = 0; i < prepared.flat_atoms.size(); ++i) {
    FlatAssignedTheoryAtom atom = prepared.flat_atoms[i];
    atom.source.value = atoms[i].value;
    atom.source.filtered = atoms[i].filtered;
    if (atom.source.filtered) {
      continue;
    }
    assert_flat_assigned_atom(closure, atom, prepared.true_flat, prepared.false_flat);
    if (closure.conflict().has_value()) {
      break;
    }
  }

  add_incremental_stats(statistics, closure.stats());
  if (closure.conflict().has_value()) {
    return closure.conflict().value();
  }
  return std::nullopt;
}

struct EufClosureCheckResult {
  std::optional<sat::Clause> conflict;
  theory::equality::FlatCongruenceClosure closure;
};

EufClosureCheckResult check_incremental_euf_closure(const PreparedFlatTheory& prepared,
                                                    const std::vector<AssignedTheoryAtom>& atoms,
                                                    util::Statistics& statistics) {
  if (atoms.size() != prepared.flat_atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  theory::equality::FlatCongruenceClosure closure = prepared.base_closure;
  for (std::size_t i = 0; i < prepared.flat_atoms.size(); ++i) {
    FlatAssignedTheoryAtom atom = prepared.flat_atoms[i];
    atom.source.value = atoms[i].value;
    atom.source.filtered = atoms[i].filtered;
    if (atom.source.filtered) {
      continue;
    }
    assert_flat_assigned_atom(closure, atom, prepared.true_flat, prepared.false_flat);
    if (closure.conflict().has_value()) {
      break;
    }
  }

  add_incremental_stats(statistics, closure.stats());
  return EufClosureCheckResult{closure.conflict(), std::move(closure)};
}

bool theory_atom_occurs_in_formula(const BooleanClausifier& clausifier, core::TermId term) {
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    if (atom.term == term && (atom.occurs_positive || atom.occurs_negative)) {
      return true;
    }
  }
  return false;
}

std::optional<sat::Lit> create_interface_equality_lit(const core::TermManager& terms,
                                                      BooleanClausifier& clausifier,
                                                      TheoryCombinationManager& manager,
                                                      core::TermId equality) {
  const core::Term& term = terms.get(equality);
  if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return std::nullopt;
  }
  if (const std::optional<sat::Lit> existing = manager.interface_equality_lit(equality);
      existing.has_value()) {
    return existing;
  }
  if (!manager.ensure_interface_pair(term.children.at(0), term.children.at(1)).has_value()) {
    return std::nullopt;
  }
  const BooleanClausifier::TheoryAtom& theory_atom = clausifier.ensure_theory_atom(equality);
  if (!manager.register_interface_equality_atom(equality, theory_atom.lit)) {
    return std::nullopt;
  }
  return theory_atom.lit;
}

std::optional<bool> lit_value_in_backend(const sat::IpasirUpBackend& backend, sat::Lit lit) {
  const int var = lit.value > 0 ? lit.value : -lit.value;
  const std::optional<bool> assignment = backend.value(var);
  if (!assignment.has_value()) {
    return std::nullopt;
  }
  return lit.value > 0 ? assignment.value() : !assignment.value();
}

std::size_t registered_interface_equality_atom_count(const TheoryCombinationManager& manager) {
  return manager.registered_interface_equality_atom_count();
}

void append_interface_equalities(
    const std::vector<InterfaceEqualityRegistryEntry>& interface_equalities, bool value,
    std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    std::vector<AssignedTheoryAtom>& assignments) {
  theory_atoms.reserve(theory_atoms.size() + interface_equalities.size());
  assignments.reserve(assignments.size() + interface_equalities.size());
  for (const InterfaceEqualityRegistryEntry& atom : interface_equalities) {
    theory_atoms.push_back(BooleanClausifier::TheoryAtom{atom.lit, atom.atom, false, false});
    assignments.push_back(AssignedTheoryAtom{atom.lit.value, value, atom.atom, false});
  }
}

void append_true_interface_equalities(
    const std::vector<InterfaceEqualityRegistryEntry>& interface_equalities,
    std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    std::vector<AssignedTheoryAtom>& assignments) {
  append_interface_equalities(interface_equalities, true, theory_atoms, assignments);
}

struct InterfacePropagationResult {
  std::size_t candidates = 0;
  std::size_t deliverable_existing = 0;
  std::size_t missing_atoms = 0;
  std::size_t created_atoms = 0;
  std::size_t already_true = 0;
  std::size_t assigned_false = 0;
  std::size_t clauses = 0;
  std::size_t reason_lits = 0;

  bool triggered_recheck() const { return clauses != 0; }
};

struct ExplainedInterfaceEquality {
  core::TermId equality = core::kInvalidTermId;
  std::optional<sat::Clause> reason;
};

std::vector<core::TermId> interface_equality_terms(
    const std::vector<ExplainedInterfaceEquality>& equalities) {
  std::vector<core::TermId> result;
  result.reserve(equalities.size());
  for (const ExplainedInterfaceEquality& equality : equalities) {
    result.push_back(equality.equality);
  }
  return result;
}

void publish_interface_propagation_result(const InterfacePropagationResult& result,
                                          std::string_view prefix, util::Statistics& statistics) {
  statistics.increment(":no.interface.equality-candidates", result.candidates);
  statistics.increment(":no.interface.equality-existing-atoms", result.deliverable_existing);
  statistics.increment(":no.interface.equality-missing-atoms", result.missing_atoms);
  statistics.increment(":no.interface.equality-created-atoms", result.created_atoms);
  statistics.increment(":no.interface.equality-already-true", result.already_true);
  statistics.increment(":no.interface.equality-assigned-false", result.assigned_false);
  statistics.increment(":no.interface.equality-clauses", result.clauses);
  statistics.increment(":no.interface.equality-reason-lits", result.reason_lits);
  if (result.triggered_recheck()) {
    statistics.increment(":no.interface.equality-rechecks");
  }

  statistics.increment(std::string{prefix} + "-candidates", result.candidates);
  statistics.increment(std::string{prefix} + "-existing-atoms", result.deliverable_existing);
  statistics.increment(std::string{prefix} + "-missing-atoms", result.missing_atoms);
  statistics.increment(std::string{prefix} + "-created-atoms", result.created_atoms);
  statistics.increment(std::string{prefix} + "-clauses", result.clauses);
  if (result.triggered_recheck()) {
    statistics.increment(std::string{prefix} + "-rechecks");
  }
}

InterfacePropagationResult force_implied_interface_equalities(
    const core::TermManager& terms, BooleanClausifier& clausifier, sat::IpasirUpBackend& backend,
    TheoryCombinationManager& manager,
    const std::vector<ExplainedInterfaceEquality>& implied_equalities,
    const std::vector<AssignedTheoryAtom>& fallback_reason_assignments,
    InterfaceEqualityClassification source_classification, util::Statistics& statistics) {
  InterfacePropagationResult result;
  result.candidates = implied_equalities.size();
  std::vector<sat::Clause> clauses_to_learn;
  clauses_to_learn.reserve(implied_equalities.size());
  const auto make_reason_clause = [&](const ExplainedInterfaceEquality& equality,
                                      sat::Lit implied_lit) {
    sat::Clause clause = equality.reason.value_or(sat::Clause{});
    if (equality.reason.has_value()) {
      statistics.increment(":no.interface.equality-local-reasons");
      statistics.increment(":no.interface.equality-local-reason-lits", clause.size());
    } else {
      statistics.increment(":no.interface.equality-fallback-reasons");
      clause.reserve(fallback_reason_assignments.size() + 1);
      for (const AssignedTheoryAtom& atom : fallback_reason_assignments) {
        if (!atom.filtered) {
          append_lit_once(clause, blocking_lit(atom));
        }
      }
    }
    append_lit_once(clause, implied_lit);
    result.reason_lits += clause.size() > 0 ? clause.size() - 1U : 0U;
    return clause;
  };
  std::vector<const ExplainedInterfaceEquality*> equalities_to_create;
  equalities_to_create.reserve(implied_equalities.size());
  for (const ExplainedInterfaceEquality& explained_equality : implied_equalities) {
    const core::TermId equality = explained_equality.equality;
    const core::Term& equality_term = terms.get(equality);
    if (equality_term.kind != core::TermKind::Equality || equality_term.children.size() != 2 ||
        !manager.ensure_interface_pair(equality_term.children.at(0), equality_term.children.at(1))
             .has_value()) {
      ++result.missing_atoms;
      continue;
    }
    manager.mark_interface_equality_classification(equality, source_classification);
    const std::optional<sat::Lit> lit = manager.interface_equality_lit(equality);
    if (!lit.has_value()) {
      ++result.missing_atoms;
      equalities_to_create.push_back(&explained_equality);
      continue;
    }
    ++result.deliverable_existing;
    const std::optional<bool> value = lit_value_in_backend(backend, lit.value());
    if (value.has_value() && value.value()) {
      ++result.already_true;
      manager.mark_interface_equality_classification(equality,
                                                     InterfaceEqualityClassification::AlreadyTrue);
      continue;
    }
    if (value.has_value() && !value.value()) {
      ++result.assigned_false;
      manager.mark_interface_equality_classification(
          equality, InterfaceEqualityClassification::AssignedFalse);
    }
    manager.mark_interface_equality_classification(equality,
                                                   InterfaceEqualityClassification::SatForced);
    clauses_to_learn.push_back(make_reason_clause(explained_equality, lit.value()));
  }
  for (const ExplainedInterfaceEquality* explained_equality : equalities_to_create) {
    const core::TermId equality = explained_equality->equality;
    const std::optional<sat::Lit> lit =
        create_interface_equality_lit(terms, clausifier, manager, equality);
    if (!lit.has_value()) {
      continue;
    }
    ++result.created_atoms;
    manager.mark_interface_equality_classification(equality,
                                                   InterfaceEqualityClassification::SatForced);
    clauses_to_learn.push_back(make_reason_clause(*explained_equality, lit.value()));
  }
  for (const sat::Clause& clause : clauses_to_learn) {
    backend.add_clause(clause);
  }
  result.clauses = clauses_to_learn.size();
  statistics.set(":no.interface.sat-equality-atoms",
                 registered_interface_equality_atom_count(manager));
  statistics.set(":no.interface.materialized-pairs", manager.summary().equality_registry.size());
  statistics.set(":no.interface.materialized-missing-pairs", manager.summary().missing_equalities);
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.increment(":no.interface.sat-equalities-forced", result.clauses);
  return result;
}

bool clause_mentions_interface_equality(const sat::Clause& clause,
                                        const TheoryCombinationManager& manager) {
  for (const sat::Lit lit : clause) {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    for (const InterfaceEqualityRegistryEntry& atom : manager.summary().equality_registry) {
      if (atom.lit.value == 0) {
        continue;
      }
      const int atom_var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (var == atom_var) {
        return true;
      }
    }
  }
  return false;
}

void record_interface_conflict(const std::optional<sat::Clause>& conflict,
                               const TheoryCombinationManager& manager, std::string_view source_key,
                               util::Statistics& statistics) {
  if (!conflict.has_value() || !clause_mentions_interface_equality(conflict.value(), manager)) {
    return;
  }
  statistics.increment(":no.interface.conflicts");
  statistics.increment(source_key);
}

std::size_t unordered_pair_count(std::size_t size) {
  if (size < 2) {
    return 0;
  }
  return (size * (size - 1U)) / 2U;
}

int sat_var(sat::Lit lit) { return lit.value > 0 ? lit.value : -lit.value; }

struct EufCarePairDiagnostics {
  std::size_t full_pairs = 0;
  std::size_t argument_definitions = 0;
  std::size_t function_classes = 0;
  std::size_t definition_pairs_full = 0;
  std::size_t definition_pairs_within_classes = 0;
  std::size_t argument_pairs_checked = 0;
  std::vector<TermPairKey> term_pairs;
  std::vector<core::TermId> argument_terms;
};

// Jovanovic and Barrett, "Sharing is Caring", Sections 3-4: the EUF care
// graph needs argument equalities only between applications whose function
// terms are equal in the current closure. This is a candidate filter for model
// decisions, never a theory entailment or a completeness reduction.
EufCarePairDiagnostics euf_care_pair_diagnostics(const FlatEufModelView& euf,
                                                 const std::vector<core::TermId>& shared_terms) {
  EufCarePairDiagnostics diagnostics;
  diagnostics.full_pairs = unordered_pair_count(shared_terms.size());

  std::unordered_map<theory::equality::FlatTermId, core::TermId> shared_term_by_flat;
  shared_term_by_flat.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    if (const std::optional<theory::equality::FlatTermId> flat = euf.flat.existing_flat_term(term);
        flat.has_value()) {
      shared_term_by_flat.emplace(flat.value(), term);
    }
  }

  struct SharedArgumentDefinition {
    theory::equality::FlatTermId function = theory::equality::kInvalidFlatTermId;
    theory::equality::FlatTermId argument = theory::equality::kInvalidFlatTermId;
  };

  std::vector<SharedArgumentDefinition> shared_argument_definitions;
  std::vector<theory::equality::FlatTermId> function_terms;
  shared_argument_definitions.reserve(euf.flat.definitions().size());
  function_terms.reserve(euf.flat.definitions().size());
  for (const theory::equality::FlatDefinition& definition : euf.flat.definitions()) {
    if (shared_term_by_flat.contains(definition.argument)) {
      shared_argument_definitions.push_back(
          SharedArgumentDefinition{definition.function, definition.argument});
      function_terms.push_back(definition.function);
      diagnostics.argument_terms.push_back(shared_term_by_flat.at(definition.argument));
    }
  }
  diagnostics.argument_definitions = shared_argument_definitions.size();
  diagnostics.definition_pairs_full = unordered_pair_count(shared_argument_definitions.size());
  std::sort(diagnostics.argument_terms.begin(), diagnostics.argument_terms.end());
  diagnostics.argument_terms.erase(
      std::unique(diagnostics.argument_terms.begin(), diagnostics.argument_terms.end()),
      diagnostics.argument_terms.end());

  std::sort(function_terms.begin(), function_terms.end());
  function_terms.erase(std::unique(function_terms.begin(), function_terms.end()),
                       function_terms.end());
  const std::vector<std::vector<theory::equality::FlatTermId>> function_classes =
      euf.closure.equivalence_classes(function_terms);
  diagnostics.function_classes = function_classes.size();

  std::unordered_map<theory::equality::FlatTermId, std::size_t> function_class_by_term;
  function_class_by_term.reserve(function_terms.size());
  for (std::size_t class_index = 0; class_index < function_classes.size(); ++class_index) {
    for (const theory::equality::FlatTermId function : function_classes[class_index]) {
      function_class_by_term.emplace(function, class_index);
    }
  }

  std::vector<std::vector<theory::equality::FlatTermId>> arguments_by_function_class(
      function_classes.size());
  for (const SharedArgumentDefinition& definition : shared_argument_definitions) {
    arguments_by_function_class.at(function_class_by_term.at(definition.function))
        .push_back(definition.argument);
  }

  std::unordered_set<FlatPairKey, FlatPairKeyHash> candidate_pairs;
  for (std::vector<theory::equality::FlatTermId>& arguments : arguments_by_function_class) {
    diagnostics.definition_pairs_within_classes += unordered_pair_count(arguments.size());
    std::sort(arguments.begin(), arguments.end());
    arguments.erase(std::unique(arguments.begin(), arguments.end()), arguments.end());
    diagnostics.argument_pairs_checked += unordered_pair_count(arguments.size());
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      for (std::size_t j = i + 1; j < arguments.size(); ++j) {
        candidate_pairs.insert(canonical_flat_pair(arguments[i], arguments[j]));
      }
    }
  }

  diagnostics.term_pairs.reserve(candidate_pairs.size());
  for (const FlatPairKey& pair : candidate_pairs) {
    const auto lhs = shared_term_by_flat.find(pair.lhs);
    const auto rhs = shared_term_by_flat.find(pair.rhs);
    if (lhs != shared_term_by_flat.end() && rhs != shared_term_by_flat.end()) {
      diagnostics.term_pairs.push_back(canonical_term_pair(lhs->second, rhs->second));
    }
  }
  std::sort(diagnostics.term_pairs.begin(), diagnostics.term_pairs.end(),
            [](const TermPairKey& lhs, const TermPairKey& rhs) {
              return lhs.lhs == rhs.lhs ? lhs.rhs < rhs.rhs : lhs.lhs < rhs.lhs;
            });
  diagnostics.term_pairs.erase(
      std::unique(diagnostics.term_pairs.begin(), diagnostics.term_pairs.end()),
      diagnostics.term_pairs.end());
  return diagnostics;
}

void publish_euf_care_pair_diagnostics(const EufCarePairDiagnostics& diagnostics, bool computed,
                                       util::Statistics& statistics) {
  statistics.increment(":no.interface.euf-care-checks");
  statistics.set(":no.interface.euf-care-full-pairs", diagnostics.full_pairs);
  statistics.set(":no.interface.euf-care-pairs", diagnostics.term_pairs.size());
  statistics.set(":no.interface.euf-care-pruned-pairs",
                 diagnostics.full_pairs > diagnostics.term_pairs.size()
                     ? diagnostics.full_pairs - diagnostics.term_pairs.size()
                     : 0);
  if (!computed) {
    return;
  }
  statistics.increment(":no.interface.euf-care-argument-definitions",
                       diagnostics.argument_definitions);
  statistics.increment(":no.interface.euf-care-function-classes", diagnostics.function_classes);
  statistics.increment(":no.interface.euf-care-definition-pairs-full",
                       diagnostics.definition_pairs_full);
  statistics.increment(":no.interface.euf-care-definition-pairs-within-classes",
                       diagnostics.definition_pairs_within_classes);
  statistics.increment(":no.interface.euf-care-argument-pairs-checked",
                       diagnostics.argument_pairs_checked);
  statistics.increment(
      ":no.interface.euf-care-definition-pairs-pruned",
      diagnostics.definition_pairs_full - diagnostics.definition_pairs_within_classes);
  statistics.increment(
      ":no.interface.euf-care-duplicate-argument-pairs-pruned",
      diagnostics.definition_pairs_within_classes - diagnostics.argument_pairs_checked);
}

// Real for QF_LRA/QF_UFLRA, Int for QF_LIA/QF_UFLIA -- both are LraTableau atoms;
// only the sort differs. mk_eq is sort-agnostic (any Bool/Uninterpreted
// equality reaches this too), so the sort check is what actually
// distinguishes an arithmetic equality from an EUF one.
bool is_arithmetic_equality_term(const core::TermManager& terms, const core::Term& term) {
  if (term.kind != core::TermKind::Equality) {
    return false;
  }
  const core::SortId sort = terms.get(term.children.at(0)).sort;
  return sort == terms.symbols().real_sort() || sort == terms.symbols().int_sort();
}

// Which bound on a prepared atom's tableau variable, and at what value, one
// truth assignment of that atom imposes (Section 3.1's infinitesimal-delta
// trick for strict inequalities).
struct BoundAssertion {
  bool is_upper;
  theory::arithmetic::DeltaRational value;
};

// How bound_assertion() may round a threshold on an integer variable.
// StrictOnly is the established behavior: strict bounds become integral
// non-strict ones. All also rounds non-strict thresholds inward, which keeps
// every bound on an integer variable integral (`--lia-round-integer-bounds`).
enum class IntegerBoundRounding : std::uint8_t { None, StrictOnly, All };

// `relation`/`rhs` describe the atom as `t ~ rhs`; `asserts_lower_when_true`
// says the atom was stored in the flipped orientation, i.e. it really means
// `t >= rhs` (or `t > rhs`), which arises when a single-variable atom's
// coefficient was negative and got divided out -- see PreparedLraTheory.
//
// All eight combinations, worked through explicitly because this is the one
// place a sign or strictness slip would silently change what the solver
// proves:
//
//   t <= c  TRUE -> upper = c        FALSE -> lower = c + delta
//   t <  c  TRUE -> upper = c - d    FALSE -> lower = c
//   t >= c  TRUE -> lower = c        FALSE -> upper = c - delta
//   t >  c  TRUE -> lower = c + d    FALSE -> upper = c
//
// which collapses to: negating an atom flips both its direction and its
// strictness, and the flipped orientation flips the direction once more.
//
// Shared between assert_lra_atom_bound (which applies this as an actual
// Assert-Bound call) and OnlineLraPropagator's propagation scan (which only
// compares against it, via LraTableau::upper_bound/lower_bound, to check
// whether some *other* atom on the same variable is already entailed).
// LraRelation::Eq isn't handled here -- it's unreachable via the frontend
// (Real/Int equality is lowered to a pair of Le atoms by the
// lower_real_equalities preprocessing pass, which always runs before theory
// dispatch) and doesn't fit the single-bound shape these cases share.
//
// `rounding` (PreparedAtom::integer_rounding) is QF_LIA's hook into this: a
// non-strict Le atom assigned FALSE is exactly as "strict" here as an
// explicit Lt atom assigned TRUE (see is_strict below) -- e.g. `distinct`
// lowers to a negated `And(Le,Le)`, so a negated Le is a completely
// ordinary way for a QF_LIA disequality to reach this function, not an edge
// case. Over Real, strictness becomes the infinitesimal-delta trick; over
// Int, `t < rhs` means `t <= ceil(rhs) - 1` and `t > rhs` means
// `t >= floor(rhs) + 1` -- NOT a blind `rhs -/+ 1`. `rhs` here is
// PreparedAtom::rhs, i.e. already divided by the atom's polynomial's
// leading coefficient (PreparedLraTheory's scale-canonicalization, item
// H1b) -- so it is not guaranteed to already be an integer even for an
// Int atom (`5x < 0` canonicalizes to `x < 0`'s rhs 0, but `5x < 1`
// canonicalizes to rhs 1/5). ceil()/floor() give the right integer
// threshold either way; a naive +/-1 does not when rhs isn't already
// integer, and previously left the tableau bound on a fractional value no
// amount of branch-and-bound could resolve (see the 2026-08-19 bug in
// docs/qf-lia-branch-and-bound.md, found by local FuzzSMT differential
// fuzzing against z3 -- 8/100 QF_LIA cases answered wrong before this
// fix).
BoundAssertion bound_assertion(theory::arithmetic::LraRelation relation, const util::Rational& rhs,
                               bool asserts_lower_when_true, bool atom_value,
                               IntegerBoundRounding rounding = IntegerBoundRounding::None) {
  const bool strict = relation == theory::arithmetic::LraRelation::Lt;
  const bool is_upper = asserts_lower_when_true ? !atom_value : atom_value;
  const bool is_strict = atom_value ? strict : !strict;
  if (!is_strict) {
    // King thesis Section 4.3 and Achterberg et al. Section 4.2: an integer
    // variable's bound can be rounded inward, `t <= r` to `t <= floor(r)`
    // and `t >= r` to `t >= ceil(r)`. Only valid when the bounded variable
    // is integer in every model, which is what IntegerBoundRounding::All
    // asserts (docs/papers/mip-presolve-integrality.md, obligation 1).
    if (rounding == IntegerBoundRounding::All) {
      return BoundAssertion{is_upper, theory::arithmetic::DeltaRational::from_rational(
                                          is_upper ? rhs.floor() : rhs.ceil())};
    }
    return BoundAssertion{is_upper, theory::arithmetic::DeltaRational::from_rational(rhs)};
  }
  if (rounding != IntegerBoundRounding::None) {
    const util::Rational integer_rhs =
        is_upper ? rhs.ceil() - util::Rational(1) : rhs.floor() + util::Rational(1);
    return BoundAssertion{is_upper, theory::arithmetic::DeltaRational::from_rational(integer_rhs)};
  }
  // A strict upper bound is `t <= rhs - delta`; a strict lower one is
  // `t >= rhs + delta`.
  return BoundAssertion{is_upper,
                        theory::arithmetic::DeltaRational{rhs, util::Rational(is_upper ? -1 : 1)}};
}

#if PRIMO_HAVE_IPASIR
// Whether `var`'s current bound in the direction `assertion` constrains is
// already at least as tight as `assertion` -- i.e. whether the atom that
// assertion came from is already entailed. Returns the entailing bound's
// reason literal alongside, for use as the propagation's antecedent.
std::optional<sat::Lit> bound_entails(const theory::arithmetic::LraTableau& tableau,
                                      theory::arithmetic::LraVarId var,
                                      const BoundAssertion& assertion) {
  if (assertion.is_upper) {
    const auto upper = tableau.upper_bound(var);
    if (upper.has_value() && upper->value <= assertion.value) {
      return upper->reason;
    }
    return std::nullopt;
  }
  const auto lower = tableau.lower_bound(var);
  if (lower.has_value() && lower->value >= assertion.value) {
    return lower->reason;
  }
  return std::nullopt;
}

#endif

// The four caps that used to live here as constants are now
// CheckSatOptions::lra_row_propagation_limits, whose defaults are these same
// values -- see LraRowPropagationLimits in check_sat.hpp for the rationale.
// They were exposed on the CLI for a long time while nothing read the struct,
// so the options parsed and did nothing; docs/tuning-parameters.md records
// how that was found.

// Applies one assigned LRA atom's relation+truth-value+strictness to
// `tableau` as a single Assert-Bound call (Eq asserts both bounds). Shared
// between the fully-lazy per-round-normalize path (not currently used by
// solve_qf_lra, kept for reference/tests) and the incremental
// prepared-atom path below.
std::optional<std::vector<sat::Lit>> assert_lra_atom_bound(
    theory::arithmetic::LraTableau& tableau, theory::arithmetic::LraVarId bound_var,
    theory::arithmetic::LraRelation relation, const util::Rational& rhs, bool atom_value,
    bool asserts_lower_when_true, sat::Lit reason,
    IntegerBoundRounding rounding = IntegerBoundRounding::None) {
  switch (relation) {
    case theory::arithmetic::LraRelation::Eq: {
      if (!atom_value) {
        throw UnsupportedInput("QF_LRA disequality (a negated Real equality) is not yet supported");
      }
      // Equality is symmetric, so the flipped orientation makes no difference
      // here: both bounds land on the same value either way.
      auto conflict = tableau.assert_upper(
          bound_var, theory::arithmetic::DeltaRational::from_rational(rhs), reason);
      if (conflict.has_value()) {
        return conflict;
      }
      return tableau.assert_lower(bound_var, theory::arithmetic::DeltaRational::from_rational(rhs),
                                  reason);
    }
    case theory::arithmetic::LraRelation::Le:
    case theory::arithmetic::LraRelation::Lt: {
      const BoundAssertion assertion =
          bound_assertion(relation, rhs, asserts_lower_when_true, atom_value, rounding);
      return assertion.is_upper ? tableau.assert_upper(bound_var, assertion.value, reason)
                                : tableau.assert_lower(bound_var, assertion.value, reason);
    }
  }
  throw std::logic_error("unreachable: LraRelation switch is exhaustive");
}

// Canonical, order-independent identity for a LinearPolynomial's variable
// content (not its constant, which normalize_atom already moved to the
// atom's rhs): a sorted-by-TermId vector of (var, coefficient) pairs, used
// to let atoms that normalize to the same polynomial share a tableau slack
// variable/row (see PreparedLraTheory below).
//
// Scale-canonical (heuristics-note item H1b, the long-deferred roadmap item
// 3): every coefficient is divided by the coefficient of the lowest-indexed
// variable, so proportional polynomials collapse to one key. Thus `x+y<=5`
// and `2x+2y<=10` both assert the upper bound 5 on one shared slack, while
// `-x-y<=-5` asserts the lower bound 5 on that same slack. Monniaux (CAV 2009,
// Section 2.1) describes the same normalization, "so that two inequalities
// constraining the same direction in space appear with the exact same
// left-hand side"; his form scales to coprime *integers*, ours to a leading
// coefficient of 1. These are different coefficient representations but the
// same rational scale equivalence class; this preparation does not GCD-rewrite
// the original core term or merge its SAT atom.
struct PolynomialKey {
  std::vector<std::pair<core::TermId, util::Rational>> sorted_terms;

  bool operator==(const PolynomialKey& other) const { return sorted_terms == other.sorted_terms; }
};

struct PolynomialKeyHash {
  std::size_t operator()(const PolynomialKey& key) const {
    std::size_t seed = key.sorted_terms.size();
    const util::RationalHash rational_hash;
    for (const auto& [var, coefficient] : key.sorted_terms) {
      seed ^= std::hash<core::TermId>{}(var) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      seed ^= rational_hash(coefficient) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

// A polynomial's scale-canonical key together with the factor divided out.
// The caller needs the factor because dividing the polynomial has to be
// matched by dividing the atom's right-hand side, and a negative factor
// reverses the relation (which PreparedAtom::asserts_lower_when_true
// records).
struct CanonicalPolynomial {
  PolynomialKey key;
  util::Rational scale{1};
};

// With `primitive_integer` (Int atoms under --lia-primitive-int-rows) the
// coefficients become coprime integers with a positive leading coefficient
// instead of a leading coefficient of one (King thesis Section 4.3). The
// returned scale is the factor divided out either way, so callers divide the
// threshold by it and read a negative scale as a reversed relation.
CanonicalPolynomial canonical_polynomial(const theory::arithmetic::LinearPolynomial& polynomial,
                                         bool primitive_integer = false) {
  CanonicalPolynomial canonical;
  canonical.key.sorted_terms.assign(polynomial.terms().begin(), polynomial.terms().end());
  std::sort(canonical.key.sorted_terms.begin(), canonical.key.sorted_terms.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  if (canonical.key.sorted_terms.empty()) {
    return canonical;  // A constant polynomial has nothing to scale by.
  }
  if (primitive_integer) {
    std::vector<util::Rational> coefficients;
    coefficients.reserve(canonical.key.sorted_terms.size());
    for (const auto& [var, coefficient] : canonical.key.sorted_terms) {
      coefficients.push_back(coefficient);
    }
    // primitive_integer_scale is positive; the sign comes from the leading
    // coefficient so proportional polynomials of either sign share a key.
    util::Rational factor = util::Rational::primitive_integer_scale(coefficients);
    if (canonical.key.sorted_terms.front().second.sign() < 0) {
      factor = -factor;
    }
    canonical.scale = util::Rational(1) / factor;
    if (factor == util::Rational(1)) {
      return canonical;
    }
    for (auto& [var, coefficient] : canonical.key.sorted_terms) {
      coefficient = coefficient * factor;
    }
    return canonical;
  }
  canonical.scale = canonical.key.sorted_terms.front().second;
  if (canonical.scale == util::Rational(1)) {
    return canonical;  // Already canonical; skip the divisions entirely.
  }
  for (auto& [var, coefficient] : canonical.key.sorted_terms) {
    coefficient = coefficient / canonical.scale;
  }
  return canonical;
}

// Precomputes the QF_LRA tableau's structural part from the complete theory-
// atom list -- one row per scale-canonical compound polynomial, shared problem
// variables -- exactly once, independent of any SAT assignment (Dutertre & de
// Moura Section 4). The resulting slack ids and atom-to-bound_var mapping are
// global to this prepared problem and stable across all theory callbacks.
// Mirrors PreparedFlatTheory's role for EUF: the per-round work becomes
// "copy this base and assert bounds", not "re-normalize and re-add rows
// from scratch every round" (what the fully-lazy path above still does).
//
// Atoms with proportional compound LinearPolynomials share a single slack
// variable/row rather than each getting its own. Their scaled right-hand sides,
// orientations and SAT literals remain per atom -- sharing structure does not
// discard a weaker bound that may be selected alone or negated in another SAT
// assignment. This is what makes same-slack bound propagation
// (OnlineLraPropagator, below) find anything to propagate: propagation
// needs multiple atoms to share a tableau variable, and without this dedup
// they essentially never would.
// Integer-specific preparation switches; all default off, which keeps the
// established QF_LIA preparation (docs/papers/mip-presolve-integrality.md).
struct IntegerPreparationOptions {
  // Round non-strict thresholds on integer-valued variables (gap 1).
  bool round_bounds = false;
  // Store Int rows with coprime integer coefficients, making every Int
  // slack integer-valued (gap 2).
  bool primitive_rows = false;
};

struct PreparedLraTheory {
  // `sparse_leaving` is a search option rather than a structural one, but it
  // is configured here because the base tableau is what every solving path
  // copies from -- both OnlineLraPropagator and the non-IPASIR fallback.
  PreparedLraTheory(const core::TermManager& terms,
                    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                    theory::arithmetic::LraTableau::PivotingRule pivoting_rule, bool sparse_leaving,
                    bool fixed_elimination, bool collect_model_variables,
                    bool least_violated_leaving = false,
                    theory::arithmetic::LraTableau::Tuning tuning = {},
                    IntegerPreparationOptions integer_options = {});

  // Extend the immutable structural base between SAT calls. Generated
  // Nelson--Oppen bridge atoms arrive one pair at a time; retaining the
  // term-variable and polynomial-slack indices makes their installation
  // proportional to the new atoms instead of rebuilding every old row.
  std::vector<theory::arithmetic::LraVarId> append_atom(const BooleanClausifier::TheoryAtom& atom);

  struct PreparedAtom {
    bool is_arithmetic = false;
    // The tableau variable this atom bounds: a slack for a compound
    // polynomial, or the problem variable itself for a single-variable atom
    // (see the constructor).
    theory::arithmetic::LraVarId bound_var = 0;
    theory::arithmetic::LraRelation relation = theory::arithmetic::LraRelation::Le;
    util::Rational rhs;
    // Set when dividing out a negative coefficient flipped the relation, so
    // the atom really means `bound_var >= rhs` rather than `<= rhs`. See
    // bound_assertion(), which is the single place this is interpreted.
    bool asserts_lower_when_true = false;
    // NO/LRA interface equality propagation: when the original atom is a
    // non-strict real inequality `(<= lhs rhs)`, LRA can publish the pair
    // once both directions are true. Strict inequalities and normalized
    // internal tableau shape do not matter for that rule.
    TermPairKey non_strict_le_pair;
    // QF_LIA only (always None for QF_LRA/QF_UFLRA): None unless bound_var
    // is known to be integer-valued. bound_assertion() then turns a *negated*
    // Le atom's implicit strictness into an integer +-1 offset instead of
    // the Real-only infinitesimal-delta trick -- see its own comment.
    // Separate from lra_atom.cpp's tightening, which only rewrites atoms that
    // are syntactically Lt; this covers Le atoms that become strict only
    // because the SAT search assigned them false. All additionally rounds
    // non-strict thresholds (`--lia-round-integer-bounds`).
    IntegerBoundRounding integer_rounding = IntegerBoundRounding::None;
  };

  // Prevalence counters for the integer-bound gaps described in
  // docs/implementation/lia.md. They observe preparation and change nothing.
  struct IntegerPreparationStats {
    // Atoms whose bounded variable is known to be integer but whose threshold
    // is fractional. One SAT polarity of such an atom is a non-strict bound,
    // which bound_assertion() rounds only under --lia-round-integer-bounds.
    std::size_t fractional_int_thresholds = 0;
    // Compound Int atoms whose slack is not marked integer, so neither of
    // their bounds is rounded.
    std::size_t non_integer_int_slack_atoms = 0;
    // Distinct Int slacks whose leading-coefficient-one row has a fractional
    // coefficient, so the slack can take non-integer values in integer models.
    std::size_t non_integer_int_slacks = 0;
  };

  // Slacks known to be integer in every integer model, in the order they were
  // first marked. A slack qualifies when some atom bounding it marks it
  // integer: its row is then an integer polynomial over Int variables. The
  // LIA adapter adds them to its integer obligations under
  // --lia-integer-slacks (docs/papers/mip-presolve-integrality.md, gap 3).
  std::vector<theory::arithmetic::LraVarId> integer_slacks;

  // The rounding mode for a bound on an integer-valued variable; None for
  // anything else. Fixed at construction so appended atoms match.
  IntegerBoundRounding integer_rounding_for(bool integer_valued) const {
    if (!integer_valued) {
      return IntegerBoundRounding::None;
    }
    return integer_options_.round_bounds ? IntegerBoundRounding::All
                                         : IntegerBoundRounding::StrictOnly;
  }

  const core::TermManager& terms_;
  bool collect_model_variables_ = false;
  IntegerPreparationOptions integer_options_;
  std::unordered_set<theory::arithmetic::LraVarId> integer_slack_set_;
  IntegerPreparationStats integer_stats;
  theory::arithmetic::LraTableau base_tableau;
  std::vector<PreparedAtom> atoms;  // parallel to theory_atoms
  // Indices (into `atoms`) of every arithmetic atom bounding a given tableau
  // variable -- built once here so OnlineLraPropagator's propagation scan
  // doesn't have to search linearly. Only populated for real tableau
  // variables (PreparedAtom::is_arithmetic); non-arithmetic atoms' default
  // bound_var (0) is a sentinel and must not be mixed in.
  std::unordered_map<theory::arithmetic::LraVarId, std::vector<std::size_t>> atoms_by_bound_var;
  std::unordered_map<core::TermId, theory::arithmetic::LraVarId> variables_by_term;
  std::unordered_map<core::TermId, theory::arithmetic::LraVarId> var_ids_;
  std::unordered_map<PolynomialKey, theory::arithmetic::LraVarId, PolynomialKeyHash>
      slack_by_polynomial_;
};

PreparedLraTheory::PreparedLraTheory(const core::TermManager& terms,
                                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                     theory::arithmetic::LraTableau::PivotingRule pivoting_rule,
                                     bool sparse_leaving, bool fixed_elimination,
                                     bool collect_model_variables, bool least_violated_leaving,
                                     theory::arithmetic::LraTableau::Tuning tuning,
                                     IntegerPreparationOptions integer_options)
    : terms_(terms),
      collect_model_variables_(collect_model_variables),
      integer_options_(integer_options),
      base_tableau(pivoting_rule) {
  base_tableau.set_sparse_leaving(sparse_leaving);
  base_tableau.set_least_violated_leaving(least_violated_leaving);
  base_tableau.set_tuning(tuning);
  atoms.reserve(theory_atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    static_cast<void>(append_atom(atom));
  }
  base_tableau.set_fixed_elimination(fixed_elimination);
}

std::vector<theory::arithmetic::LraVarId> PreparedLraTheory::append_atom(
    const BooleanClausifier::TheoryAtom& atom) {
  std::vector<theory::arithmetic::LraVarId> new_model_variables;
  auto get_var = [&](core::TermId term_id) {
    const auto found = var_ids_.find(term_id);
    if (found != var_ids_.end()) {
      return found->second;
    }
    const theory::arithmetic::LraVarId id = base_tableau.add_variable();
    var_ids_.emplace(term_id, id);
    if (collect_model_variables_) {
      variables_by_term.emplace(term_id, id);
      new_model_variables.push_back(id);
    }
    return id;
  };

  const core::Term& term = terms_.get(atom.term);
  const bool is_comparison = term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt;
  if (!is_comparison && !is_arithmetic_equality_term(terms_, term)) {
    atoms.push_back(PreparedAtom{});
    return new_model_variables;
  }

  const theory::arithmetic::LraAtom normalized =
      theory::arithmetic::normalize_atom(terms_, atom.term);
  const bool is_integer_atom = terms_.get(term.children.at(0)).sort == terms_.symbols().int_sort();
  // Scale-canonicalize first (item H1b), then divide the atom's right-hand
  // side by the same factor. A negative factor reverses the relation, which
  // is what asserts_lower_when_true records -- interpreted in exactly one
  // place, bound_assertion().
  const bool primitive_row = is_integer_atom && integer_options_.primitive_rows;
  const CanonicalPolynomial canonical = canonical_polynomial(normalized.polynomial, primitive_row);
  const util::Rational rhs =
      canonical.scale == util::Rational(1) ? normalized.rhs : normalized.rhs / canonical.scale;
  const bool flipped = canonical.scale.sign() < 0;

  // A single-variable atom is a bound on that variable itself: no slack and
  // no row at all (item H1a). Monniaux (CAV 2009, Section 2.1) states this
  // as ordinary practice -- "for each left hand side that is not a variable,
  // a new variable is introduced" -- and Dutertre & de Moura's Section 2
  // does the same; Primo previously introduced a slack for *every* distinct
  // polynomial, which on the lagging families meant 36-95% of all tableau
  // rows were trivial `s = c*x` rows (measurements in
  // docs/qf-lra-heuristics.md, item H1). After canonicalization the single
  // remaining coefficient is exactly 1, so the variable is the polynomial.
  //
  // Together with canonicalization this puts one variable's lower and upper
  // bound on the same tableau variable: `(<= 0 x)` normalizes to a
  // coefficient of -1 and `(<= x 10)` to +1, so under the old exact-content
  // key they landed on two *different* slacks and the tableau could not see
  // that `x` was bounded on both sides without pivoting.
  if (canonical.key.sorted_terms.size() == 1) {
    const theory::arithmetic::LraVarId bound_var = get_var(canonical.key.sorted_terms[0].first);
    const std::size_t atom_index = atoms.size();
    const TermPairKey non_strict_le_pair =
        term.kind == core::TermKind::Le ? TermPairKey{term.children.at(0), term.children.at(1)}
                                        : TermPairKey{};
    if (is_integer_atom && !rhs.is_integer()) {
      ++integer_stats.fractional_int_thresholds;
    }
    atoms.push_back(PreparedAtom{true, bound_var, normalized.relation, rhs, flipped,
                                 non_strict_le_pair, integer_rounding_for(is_integer_atom)});
    atoms_by_bound_var[bound_var].push_back(atom_index);
    return new_model_variables;
  }

  const auto existing_slack = slack_by_polynomial_.find(canonical.key);
  theory::arithmetic::LraVarId slack;
  if (existing_slack != slack_by_polynomial_.end()) {
    slack = existing_slack->second;
  } else {
    std::vector<std::pair<theory::arithmetic::LraVarId, util::Rational>> row;
    row.reserve(canonical.key.sorted_terms.size());
    for (const auto& [var_term, coefficient] : canonical.key.sorted_terms) {
      row.emplace_back(get_var(var_term), coefficient);
    }
    slack = base_tableau.add_variable();
    base_tableau.add_row(slack, row);
    slack_by_polynomial_.emplace(canonical.key, slack);
    if (is_integer_atom && std::any_of(row.begin(), row.end(), [](const auto& entry) {
          return !entry.second.is_integer();
        })) {
      ++integer_stats.non_integer_int_slacks;
    }
  }
  const std::size_t atom_index = atoms.size();
  const TermPairKey non_strict_le_pair = term.kind == core::TermKind::Le
                                             ? TermPairKey{term.children.at(0), term.children.at(1)}
                                             : TermPairKey{};
  // Unlike the single-variable case above, `slack` is a fresh tableau
  // variable satisfying `slack = canonical.polynomial`, i.e. the original
  // integer-coefficient polynomial divided by `canonical.scale` -- so
  // `slack` itself is only guaranteed integer-valued when that division
  // was by +-1. For any other scale (routine here: scale-canonicalizing a
  // polynomial whose lowest-indexed variable has a large coefficient, e.g.
  // a bit-position term scaled by 2^32, produces exactly this) `slack`
  // takes on fractional values even though the underlying polynomial is
  // integer, and bound_assertion()'s strict-bound floor/ceil tightening
  // -- sound only for a value that is itself forced to be integer, per its
  // own comment -- would tighten the *slack's* fractional value directly,
  // which is unsound: it can fabricate a bound (e.g. rounding a value like
  // -5/2^32 down to -1) that excludes integer polynomial values the
  // original atom never ruled out. See lia_gomory_cuts.cpp's
  // find_eligible_row for the same slack-vs-declared-variable distinction
  // already made there for exactly this reason.
  //
  // Under --lia-primitive-int-rows the stored row has integer coefficients
  // over Int variables, so the slack is integer in every integer model
  // whatever the scale (King thesis Section 4.3, footnote 4;
  // docs/papers/mip-presolve-integrality.md, obligation 2).
  const bool slack_is_integer =
      is_integer_atom && (primitive_row || canonical.scale == util::Rational(1) ||
                          canonical.scale == util::Rational(-1));
  if (is_integer_atom && !slack_is_integer) {
    ++integer_stats.non_integer_int_slack_atoms;
  }
  if (slack_is_integer && integer_slack_set_.insert(slack).second) {
    integer_slacks.push_back(slack);
  }
  if (slack_is_integer && !rhs.is_integer()) {
    ++integer_stats.fractional_int_thresholds;
  }
  atoms.push_back(PreparedAtom{true, slack, normalized.relation, rhs, flipped, non_strict_le_pair,
                               integer_rounding_for(slack_is_integer)});
  atoms_by_bound_var[slack].push_back(atom_index);
  return new_model_variables;
}

std::optional<util::Rational> positive_delta_upper_bound(theory::arithmetic::DeltaRational lhs,
                                                         theory::arithmetic::DeltaRational rhs) {
  const util::Rational rational_gap = rhs.rational - lhs.rational;
  const util::Rational infinitesimal_diff = lhs.infinitesimal - rhs.infinitesimal;
  if (rational_gap.sign() > 0 && infinitesimal_diff.sign() > 0) {
    return rational_gap / infinitesimal_diff;
  }
  return std::nullopt;
}

bool tighten_delta_upper_bound(std::optional<util::Rational>& upper,
                               const std::optional<util::Rational>& candidate) {
  if (!candidate.has_value() || candidate->sign() <= 0) {
    return false;
  }
  if (!upper.has_value() || candidate.value() < upper.value()) {
    upper = candidate;
    return true;
  }
  return false;
}

util::Rational choose_concrete_delta(const PreparedLraTheory& prepared,
                                     const theory::arithmetic::LraTableau& tableau,
                                     const std::vector<AssignedTheoryAtom>& assigned_atoms) {
  std::optional<util::Rational> upper;
  for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms.at(i);
    if (!prepared_atom.is_arithmetic || assigned_atoms.at(i).filtered) {
      continue;
    }
    const theory::arithmetic::DeltaRational value = tableau.assignment(prepared_atom.bound_var);
    if (prepared_atom.relation == theory::arithmetic::LraRelation::Eq) {
      continue;
    }
    const BoundAssertion assertion = bound_assertion(
        prepared_atom.relation, prepared_atom.rhs, prepared_atom.asserts_lower_when_true,
        assigned_atoms.at(i).value, prepared_atom.integer_rounding);
    if (assertion.is_upper) {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(value, assertion.value));
    } else {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(assertion.value, value));
    }
  }
  return upper.has_value() ? upper.value() / util::Rational(2) : util::Rational(1);
}

util::Rational choose_concrete_delta(
    const PreparedLraTheory& prepared,
    const std::vector<theory::arithmetic::DeltaRational>& candidate_values,
    const std::vector<AssignedTheoryAtom>& assigned_atoms,
    const std::vector<theory::arithmetic::LraVarId>& order_variables,
    util::Statistics& statistics) {
  std::optional<util::Rational> upper;
  for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms.at(i);
    if (!prepared_atom.is_arithmetic || assigned_atoms.at(i).filtered) {
      continue;
    }
    const theory::arithmetic::DeltaRational& value = candidate_values.at(prepared_atom.bound_var);
    if (prepared_atom.relation == theory::arithmetic::LraRelation::Eq) {
      continue;
    }
    const BoundAssertion assertion = bound_assertion(
        prepared_atom.relation, prepared_atom.rhs, prepared_atom.asserts_lower_when_true,
        assigned_atoms.at(i).value, prepared_atom.integer_rounding);
    if (assertion.is_upper) {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(value, assertion.value));
    } else {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(assertion.value, value));
    }
  }

  // Dutertre-de Moura, "A Fast Linear-Arithmetic Solver for DPLL(T)",
  // Section 3.3: instantiate the symbolic infinitesimal below every crossing
  // point whose order must survive in the concrete model. Adjacent distinct
  // shared values suffice after sorting, avoiding a quadratic pair scan.
  std::vector<theory::arithmetic::DeltaRational> ordered_values;
  ordered_values.reserve(order_variables.size());
  for (const theory::arithmetic::LraVarId variable : order_variables) {
    ordered_values.push_back(candidate_values.at(variable));
  }
  std::sort(ordered_values.begin(), ordered_values.end());
  ordered_values.erase(std::unique(ordered_values.begin(), ordered_values.end()),
                       ordered_values.end());
  for (std::size_t index = 1; index < ordered_values.size(); ++index) {
    statistics.increment(":no.shared-model.concrete-delta-order-pairs");
    const std::optional<util::Rational> candidate =
        positive_delta_upper_bound(ordered_values[index - 1], ordered_values[index]);
    if (candidate.has_value()) {
      statistics.increment(":no.shared-model.concrete-delta-order-bounds");
    }
    if (tighten_delta_upper_bound(upper, candidate)) {
      statistics.increment(":no.shared-model.concrete-delta-order-tightenings");
    }
  }
  return upper.has_value() ? upper.value() / util::Rational(2) : util::Rational(1);
}

util::Rational concretize_delta_rational(theory::arithmetic::DeltaRational value,
                                         const util::Rational& delta) {
  return value.rational + value.infinitesimal * delta;
}

using RealModelValues = std::unordered_map<core::TermId, util::Rational>;

RealModelValues real_model_values_from_lra(const std::vector<core::TermId>& model_terms,
                                           const PreparedLraTheory& prepared,
                                           const theory::arithmetic::LraTableau& tableau,
                                           const std::vector<AssignedTheoryAtom>& assigned_atoms) {
  const util::Rational concrete_delta = choose_concrete_delta(prepared, tableau, assigned_atoms);
  RealModelValues values;
  values.reserve(prepared.variables_by_term.size() + model_terms.size());
  for (const auto& [term_id, variable] : prepared.variables_by_term) {
    values.emplace(term_id,
                   concretize_delta_rational(tableau.assignment(variable), concrete_delta));
  }
  return values;
}

std::shared_ptr<const core::Model> build_qf_lra_model(
    core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const ModelConversionChain& conversions, const PreparedLraTheory& prepared,
    const theory::arithmetic::LraTableau& tableau,
    const std::vector<AssignedTheoryAtom>& assigned_atoms, util::Statistics& statistics,
    std::optional<std::vector<core::SymbolId>> visible_symbols = std::nullopt) {
  const RealModelValues values =
      real_model_values_from_lra(model_terms, prepared, tableau, assigned_atoms);
  std::vector<TheoryModelAssignment> assignments;
  assignments.reserve(assigned_atoms.size());
  for (const auto& atom : assigned_atoms) {
    assignments.push_back({atom.term, atom.value, atom.filtered});
  }
  return build_typed_model(terms, {}, assignments, values, conversions, nullptr, &statistics,
                           std::move(visible_symbols));
}

void load_assignment_snapshot(const sat::IpasirUpBackend& backend,
                              const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                              sat::Trail& snapshot) {
  snapshot.clear();
  for (const auto& atom : atoms) {
    const int var = std::abs(atom.lit.value);
    const auto value = backend.value(var);
    if (!value) throw UnsupportedInput("SAT backend did not assign theory atom");
    snapshot.assign(*value ? var : -var);
  }
}

std::vector<AssignedTheoryAtom> assigned_atoms_from_view(
    sat::TrailView view, const std::vector<BooleanClausifier::TheoryAtom>& atoms,
    PureTheoryFilterMode mode = PureTheoryFilterMode::Off) {
  std::vector<AssignedTheoryAtom> result;
  result.reserve(atoms.size());
  for (const auto& atom : atoms) {
    const int literal = view.literal(std::abs(atom.lit.value));
    if (literal == 0) throw std::logic_error("assignment view omitted a theory atom");
    result.push_back(assigned_theory_atom(mode, atom, literal == atom.lit.value));
  }
  return result;
}

bool is_lra_theory_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt) {
    return true;
  }
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
}

bool is_real_euf_definition_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort() &&
         contains_application_term(terms, term_id);
}

bool is_euf_theory_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    if (terms.get(term.children.at(0)).sort == terms.symbols().real_sort()) {
      return contains_application_term(terms, term_id);
    }
    return terms.get(term.children.at(0)).sort != terms.symbols().real_sort();
  }
  return (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
         term.sort == terms.symbols().bool_sort();
}

bool contains_real_sorted_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort == terms.symbols().real_sort()) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_real_sorted_term(terms, child)) {
      return true;
    }
  }
  return false;
}

bool contains_nonconstant_real_sorted_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort == terms.symbols().real_sort() && term.kind != core::TermKind::Constant) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_nonconstant_real_sorted_term(terms, child)) {
      return true;
    }
  }
  return false;
}

void require_supported_mixed_theory_atom(const core::TermManager& terms,
                                         const BooleanClausifier::TheoryAtom& atom) {
  if (is_euf_theory_atom(terms, atom.term)) {
    if (contains_real_sorted_term(terms, atom.term) &&
        !is_real_euf_definition_atom(terms, atom.term) &&
        contains_nonconstant_real_sorted_term(terms, atom.term)) {
      throw UnsupportedInput(
          "QF_UFLRA with EUF atoms over non-constant Real terms needs purification");
    }
    return;
  }
  if (is_lra_theory_atom(terms, atom.term)) {
    if (contains_application_term(terms, atom.term)) {
      throw UnsupportedInput(
          "QF_UFLRA arithmetic atom escaped Real-valued UF-application purification");
    }
    return;
  }
  throw UnsupportedInput("unsupported mixed-theory atom");
}

void collect_real_constants_in_term(const core::TermManager& terms, core::TermId term_id,
                                    std::unordered_set<core::TermId>& out) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().real_sort()) {
    out.insert(term_id);
  }
  for (const core::TermId child : term.children) {
    collect_real_constants_in_term(terms, child, out);
  }
}

std::vector<core::TermId> shared_real_constants(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& euf_atoms,
    const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms) {
  std::unordered_set<core::TermId> euf_terms;
  std::unordered_set<core::TermId> lra_terms;
  for (const BooleanClausifier::TheoryAtom& atom : euf_atoms) {
    collect_real_constants_in_term(terms, atom.term, euf_terms);
  }
  for (const BooleanClausifier::TheoryAtom& atom : lra_atoms) {
    const core::Term& term = terms.get(atom.term);
    const bool is_comparison = term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt;
    if (!is_comparison && !is_arithmetic_equality_term(terms, term)) {
      continue;
    }
    const theory::arithmetic::LraAtom normalized =
        theory::arithmetic::normalize_atom(terms, atom.term);
    for (const auto& [variable, coefficient] : normalized.polynomial.terms()) {
      if (!coefficient.is_zero()) {
        lra_terms.insert(variable);
      }
    }
  }

  std::vector<core::TermId> shared;
  shared.reserve(std::min(euf_terms.size(), lra_terms.size()));
  for (const core::TermId term : euf_terms) {
    if (lra_terms.contains(term)) {
      shared.push_back(term);
    }
  }
  std::sort(shared.begin(), shared.end());
  return shared;
}

void collect_constants_of_sort_in_term(const core::TermManager& terms, core::TermId term_id,
                                       core::SortId sort, std::unordered_set<core::TermId>& out) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Constant && term.sort == sort) {
    out.insert(term_id);
  }
  for (const core::TermId child : term.children) {
    collect_constants_of_sort_in_term(terms, child, sort, out);
  }
}

bool less_term_pair(const TermPairKey& lhs, const TermPairKey& rhs) {
  return lhs.lhs == rhs.lhs ? lhs.rhs < rhs.rhs : lhs.lhs < rhs.lhs;
}

std::unordered_set<core::TermId> term_id_set(const std::vector<core::TermId>& terms) {
  return std::unordered_set<core::TermId>(terms.begin(), terms.end());
}

void add_candidate_term_pair(std::unordered_set<TermPairKey, TermPairKeyHash>& pairs,
                             const std::unordered_set<core::TermId>& allowed_terms,
                             core::TermId lhs, core::TermId rhs) {
  if (lhs == rhs || !allowed_terms.contains(lhs) || !allowed_terms.contains(rhs)) {
    return;
  }
  pairs.insert(canonical_term_pair(lhs, rhs));
}

std::vector<core::TermId> shared_real_constants_in_term(
    const core::TermManager& terms, core::TermId term_id,
    const std::unordered_set<core::TermId>& shared_terms) {
  std::unordered_set<core::TermId> real_terms;
  collect_real_constants_in_term(terms, term_id, real_terms);
  std::vector<core::TermId> result;
  result.reserve(real_terms.size());
  for (const core::TermId term : real_terms) {
    if (shared_terms.contains(term)) {
      result.push_back(term);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

void add_euf_cooccurrence_candidate_pairs(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& euf_atoms,
    const std::unordered_set<core::TermId>& shared_terms,
    std::unordered_set<TermPairKey, TermPairKeyHash>& pairs) {
  for (const BooleanClausifier::TheoryAtom& atom : euf_atoms) {
    const std::vector<core::TermId> atom_terms =
        shared_real_constants_in_term(terms, atom.term, shared_terms);
    for (std::size_t i = 0; i < atom_terms.size(); ++i) {
      for (std::size_t j = i + 1; j < atom_terms.size(); ++j) {
        add_candidate_term_pair(pairs, shared_terms, atom_terms[i], atom_terms[j]);
      }
    }
  }
}

void add_true_paired_lra_bound_candidate_pairs(
    const core::TermManager& terms, const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::unordered_set<core::TermId>& shared_terms,
    std::unordered_set<TermPairKey, TermPairKeyHash>& pairs) {
  std::unordered_set<TermPairKey, TermPairKeyHash> true_directed_bounds;
  true_directed_bounds.reserve(lra_assignments.size());
  for (const AssignedTheoryAtom& assignment : lra_assignments) {
    if (assignment.filtered || !assignment.value) {
      continue;
    }
    const core::Term& term = terms.get(assignment.term);
    if (term.kind != core::TermKind::Le || term.children.size() != 2) {
      continue;
    }
    const core::TermId lhs = term.children.at(0);
    const core::TermId rhs = term.children.at(1);
    if (lhs == rhs || !shared_terms.contains(lhs) || !shared_terms.contains(rhs)) {
      continue;
    }
    true_directed_bounds.insert(TermPairKey{lhs, rhs});
  }

  for (const TermPairKey& bound : true_directed_bounds) {
    if (true_directed_bounds.contains(TermPairKey{bound.rhs, bound.lhs})) {
      add_candidate_term_pair(pairs, shared_terms, bound.lhs, bound.rhs);
    }
  }
}

std::vector<TermPairKey> qf_uflra_candidate_final_scan_pairs(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& euf_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const EufCarePairDiagnostics& euf_care,
    const std::vector<core::TermId>& shared_real_terms) {
  const std::unordered_set<core::TermId> shared_terms = term_id_set(shared_real_terms);
  std::unordered_set<TermPairKey, TermPairKeyHash> pairs;
  pairs.reserve(euf_care.term_pairs.size() + euf_atoms.size() + lra_assignments.size());
  for (const TermPairKey& pair : euf_care.term_pairs) {
    add_candidate_term_pair(pairs, shared_terms, pair.lhs, pair.rhs);
  }
  add_euf_cooccurrence_candidate_pairs(terms, euf_atoms, shared_terms, pairs);
  add_true_paired_lra_bound_candidate_pairs(terms, lra_assignments, shared_terms, pairs);

  std::vector<TermPairKey> result(pairs.begin(), pairs.end());
  std::sort(result.begin(), result.end(), less_term_pair);
  return result;
}

std::vector<core::TermId> euf_real_model_terms_in_atoms(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& atoms) {
  std::unordered_set<core::TermId> real_terms;
  std::unordered_set<core::TermId> visited;
  std::vector<core::TermId> pending;
  pending.reserve(atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    pending.push_back(atom.term);
  }
  while (!pending.empty()) {
    const core::TermId term_id = pending.back();
    pending.pop_back();
    if (!visited.insert(term_id).second) {
      continue;
    }
    const core::Term& term = terms.get(term_id);
    if (term.sort == terms.symbols().real_sort() &&
        (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application)) {
      real_terms.insert(term_id);
    }
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
  std::vector<core::TermId> result(real_terms.begin(), real_terms.end());
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<ExplainedInterfaceEquality> euf_implied_real_equalities(
    core::TermManager& terms, const FlatEufModelView& euf,
    const std::vector<core::TermId>& shared_terms, util::Statistics& statistics) {
  std::vector<ExplainedInterfaceEquality> equalities;
  std::vector<std::pair<core::TermId, theory::equality::FlatTermId>> mapped_terms;
  std::vector<theory::equality::FlatTermId> flat_terms;
  mapped_terms.reserve(shared_terms.size());
  flat_terms.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    const std::optional<theory::equality::FlatTermId> flat_term = euf.flat.existing_flat_term(term);
    if (flat_term.has_value()) {
      mapped_terms.emplace_back(term, flat_term.value());
      flat_terms.push_back(flat_term.value());
    }
  }

  const std::vector<std::vector<theory::equality::FlatTermId>> classes =
      euf.closure.equivalence_classes(flat_terms);
  statistics.increment(":no.interface.euf-implied-class-projections");
  statistics.increment(":no.interface.euf-implied-class-projection-terms", flat_terms.size());
  std::unordered_map<theory::equality::FlatTermId, std::size_t> class_by_flat_term;
  class_by_flat_term.reserve(flat_terms.size());
  for (std::size_t class_index = 0; class_index < classes.size(); ++class_index) {
    for (const theory::equality::FlatTermId flat_term : classes[class_index]) {
      class_by_flat_term.emplace(flat_term, class_index);
    }
  }

  std::vector<std::optional<std::pair<core::TermId, theory::equality::FlatTermId>>>
      class_representatives(classes.size());
  for (const auto& [term, flat_term] : mapped_terms) {
    std::optional<std::pair<core::TermId, theory::equality::FlatTermId>>& representative =
        class_representatives.at(class_by_flat_term.at(flat_term));

    if (!representative.has_value()) {
      representative = std::pair{term, flat_term};
      continue;
    }

    const core::TermId equality = terms.mk_eq(representative->first, term);
    if (terms.get(equality).kind == core::TermKind::Equality) {
      std::optional<std::vector<sat::Lit>> reason =
          euf.closure.explain_equal(representative->second, flat_term);
      if (!reason.has_value()) {
        statistics.increment(":no.interface.euf-explanation-missing");
      }
      equalities.push_back(ExplainedInterfaceEquality{equality, std::move(reason)});
    }
  }
  return equalities;
}

std::vector<ExplainedInterfaceEquality> lra_entailed_shared_equalities(
    core::TermManager& terms, const PreparedLraTheory& prepared,
    const theory::arithmetic::LraTableau& tableau, const std::vector<core::TermId>& shared_terms,
    QfUflraLraEqualityProducer producer, util::Statistics& statistics) {
  std::vector<theory::arithmetic::LraVarId> shared_variables;
  std::unordered_map<theory::arithmetic::LraVarId, core::TermId> term_by_variable;
  shared_variables.reserve(shared_terms.size());
  term_by_variable.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    const auto variable = prepared.variables_by_term.find(term);
    if (variable == prepared.variables_by_term.end()) {
      statistics.increment(":no.interface.lra-opportunistic-unmapped-terms");
      continue;
    }
    if (term_by_variable.emplace(variable->second, term).second) {
      shared_variables.push_back(variable->second);
    }
  }

  const PhaseClock::time_point producer_start = PhaseClock::now();
  std::vector<theory::arithmetic::LraTableau::EntailedEquality> entailed;
  if (producer == QfUflraLraEqualityProducer::ModelUnit) {
    statistics.increment(":no.interface.lra-model-unit-selected");
    theory::arithmetic::LraTableau::ModelFilteredEqualityResult query =
        tableau.model_filtered_unit_equalities(shared_variables);
    entailed = std::move(query.equalities);
    statistics.increment(":no.interface.lra-model-unit-classes", query.stats.model_classes);
    statistics.increment(":no.interface.lra-model-unit-class-terms", query.stats.class_terms);
    statistics.increment(":no.interface.lra-model-unit-candidate-pairs",
                         query.stats.candidate_pairs);
    statistics.increment(":no.interface.lra-model-unit-rows-inspected", query.stats.rows_inspected);
    statistics.increment(":no.interface.lra-model-unit-nonzeros-inspected",
                         query.stats.nonzeros_inspected);
    statistics.increment(":no.interface.lra-model-unit-fixed-matches", query.stats.fixed_matches);
    statistics.increment(":no.interface.lra-model-unit-one-row-matches",
                         query.stats.one_row_matches);
    statistics.increment(":no.interface.lra-model-unit-two-row-matches",
                         query.stats.two_row_matches);
    statistics.increment(":no.interface.lra-model-unit-nonunit-rejections",
                         query.stats.nonunit_rejections);
    statistics.increment(":no.interface.lra-model-unit-nonfixed-row-rejections",
                         query.stats.nonfixed_row_rejections);
    statistics.increment(":no.interface.lra-model-unit-reason-lits", query.stats.reason_literals);
    statistics.increment(":no.interface.lra-legacy-scan-skipped");
  } else {
    statistics.increment(":no.interface.lra-legacy-producer-selected");
    entailed = tableau.entailed_equalities(shared_variables);
  }
  statistics.increment(":time.no-lra-equality-producer-us",
                       elapsed_microseconds(producer_start, PhaseClock::now()));
  std::vector<ExplainedInterfaceEquality> equalities;
  equalities.reserve(entailed.size());
  for (const theory::arithmetic::LraTableau::EntailedEquality& equality : entailed) {
    const auto lhs = term_by_variable.find(equality.lhs);
    const auto rhs = term_by_variable.find(equality.rhs);
    if (lhs == term_by_variable.end() || rhs == term_by_variable.end()) {
      statistics.increment(":no.interface.lra-opportunistic-unmapped-equalities");
      continue;
    }
    equalities.push_back(
        ExplainedInterfaceEquality{terms.mk_eq(lhs->second, rhs->second), equality.reasons});
    statistics.increment(":no.interface.lra-opportunistic-reason-lits", equality.reasons.size());
    switch (equality.rule) {
      case theory::arithmetic::LraTableau::EntailedEqualityRule::FixedEq:
        statistics.increment(":no.interface.lra-opportunistic-fixed-eq");
        break;
      case theory::arithmetic::LraTableau::EntailedEqualityRule::EqRow:
        statistics.increment(":no.interface.lra-opportunistic-eq-row");
        break;
      case theory::arithmetic::LraTableau::EntailedEqualityRule::EqOffsetRows:
        statistics.increment(":no.interface.lra-opportunistic-eq-offset-rows");
        break;
      case theory::arithmetic::LraTableau::EntailedEqualityRule::EqRows:
        statistics.increment(":no.interface.lra-opportunistic-eq-rows");
        break;
    }
  }
  return equalities;
}

// de Moura-Bjorner MBTC Sections 4, 5, and 5.2 use equal-value model classes and
// model diversification to reduce equality candidates. Here the classes are
// only a necessary-condition filter for the exact convex oracle: every retained
// pair still needs both strict conflicts before it is published as entailed.
struct LraModelEqualPartition {
  std::vector<std::vector<core::TermId>> classes;
  std::vector<core::TermId> shared_terms;
  std::unordered_map<core::TermId, std::size_t> class_by_term;
  std::unordered_map<core::TermId, theory::arithmetic::DeltaRational> value_by_term;
  RealModelValues concrete_values;
  std::unordered_set<core::TermId> mapped_terms;
  std::unordered_set<core::TermId> unmapped_terms;
  std::vector<TermPairKey> unresolved_forbidden_pairs;
  std::size_t candidate_pair_count = 0;
  std::size_t candidate_pairs_before_mutation = 0;

  bool may_be_equal(core::TermId lhs, core::TermId rhs) const {
    if (unmapped_terms.contains(lhs) || unmapped_terms.contains(rhs)) {
      return true;
    }
    const auto lhs_class = class_by_term.find(lhs);
    const auto rhs_class = class_by_term.find(rhs);
    return lhs_class != class_by_term.end() && rhs_class != class_by_term.end() &&
           lhs_class->second == rhs_class->second;
  }
};

// Enumerates the logical model-equality relation without retaining its
// quadratic expansion. Class pairs come first, followed by every pair touching
// an unmapped term; both phases are deterministic.
class LraModelPairCursor {
 public:
  explicit LraModelPairCursor(const LraModelEqualPartition& partition,
                              bool skip_class_phase = false)
      : partition_(partition), class_index_(skip_class_phase ? partition.classes.size() : 0) {}

  std::optional<TermPairKey> next() {
    while (class_index_ < partition_.classes.size()) {
      const std::vector<core::TermId>& equal_class = partition_.classes[class_index_];
      if (class_lhs_ + 1 < equal_class.size()) {
        if (class_rhs_ < equal_class.size()) {
          return canonical_term_pair(equal_class[class_lhs_], equal_class[class_rhs_++]);
        }
        ++class_lhs_;
        class_rhs_ = class_lhs_ + 1;
        continue;
      }
      ++class_index_;
      class_lhs_ = 0;
      class_rhs_ = 1;
    }

    while (!partition_.unmapped_terms.empty() && shared_lhs_ < partition_.shared_terms.size()) {
      if (shared_rhs_ >= partition_.shared_terms.size()) {
        ++shared_lhs_;
        shared_rhs_ = shared_lhs_ + 1;
        continue;
      }
      const core::TermId lhs = partition_.shared_terms[shared_lhs_];
      const core::TermId rhs = partition_.shared_terms[shared_rhs_++];
      if (partition_.unmapped_terms.contains(lhs) || partition_.unmapped_terms.contains(rhs)) {
        return canonical_term_pair(lhs, rhs);
      }
    }
    return std::nullopt;
  }

 private:
  const LraModelEqualPartition& partition_;
  std::size_t class_index_ = 0;
  std::size_t class_lhs_ = 0;
  std::size_t class_rhs_ = 1;
  std::size_t shared_lhs_ = 0;
  std::size_t shared_rhs_ = 1;
};

LraModelEqualPartition lra_model_equal_partition(
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& tableau,
    const std::vector<AssignedTheoryAtom>& assignments,
    const std::vector<core::TermId>& shared_terms, const std::vector<TermPairKey>& forbidden_pairs,
    const std::vector<core::TermId>& mutation_terms, ModelEqualityMutationScope mutation_scope,
    bool diversify_model, bool build_concrete_values, util::Statistics& statistics) {
  using theory::arithmetic::LraVarId;

  std::vector<LraVarId> shared_variables;
  std::unordered_map<LraVarId, std::vector<core::TermId>> terms_by_variable;
  std::vector<core::TermId> unmapped_terms;
  shared_variables.reserve(shared_terms.size());
  terms_by_variable.reserve(shared_terms.size());
  unmapped_terms.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    const auto variable = prepared.variables_by_term.find(term);
    if (variable == prepared.variables_by_term.end()) {
      unmapped_terms.push_back(term);
      continue;
    }
    auto [mapped_terms, inserted] = terms_by_variable.try_emplace(variable->second);
    mapped_terms->second.push_back(term);
    if (inserted) {
      shared_variables.push_back(variable->second);
    }
  }

  struct ProjectedClasses {
    std::vector<std::vector<core::TermId>> classes;
    std::unordered_set<core::TermId> mapped_terms;
    std::size_t candidate_pairs = 0;
    std::size_t conservative_pairs = 0;
  };
  const auto project_classes = [&](const std::vector<std::vector<LraVarId>>& variable_classes) {
    ProjectedClasses projection;
    projection.mapped_terms.reserve(shared_terms.size());
    std::unordered_set<LraVarId> variables_in_multi_var_class;
    const auto add_class = [&](std::vector<core::TermId> class_terms) {
      std::sort(class_terms.begin(), class_terms.end());
      class_terms.erase(std::unique(class_terms.begin(), class_terms.end()), class_terms.end());
      projection.mapped_terms.insert(class_terms.begin(), class_terms.end());
      if (class_terms.size() < 2) {
        return;
      }
      projection.candidate_pairs += unordered_pair_count(class_terms.size());
      projection.classes.push_back(std::move(class_terms));
    };

    for (const std::vector<LraVarId>& equal_class : variable_classes) {
      std::vector<core::TermId> class_terms;
      for (const LraVarId variable : equal_class) {
        variables_in_multi_var_class.insert(variable);
        const std::vector<core::TermId>& mapped_terms = terms_by_variable.at(variable);
        class_terms.insert(class_terms.end(), mapped_terms.begin(), mapped_terms.end());
      }
      add_class(std::move(class_terms));
    }
    // Several terms may exceptionally map to one tableau variable. Such terms
    // form a model class even when the variable has no equal-valued peer.
    for (const auto& [variable, mapped_terms] : terms_by_variable) {
      if (!variables_in_multi_var_class.contains(variable)) {
        add_class(mapped_terms);
      }
    }

    projection.conservative_pairs =
        unmapped_terms.size() * (shared_terms.size() - unmapped_terms.size()) +
        unordered_pair_count(unmapped_terms.size());
    projection.candidate_pairs += projection.conservative_pairs;
    std::sort(projection.classes.begin(), projection.classes.end());
    return projection;
  };
  const auto projected_pair_count =
      [&](const std::vector<std::vector<LraVarId>>& variable_classes) {
        std::size_t pair_count = 0;
        std::unordered_set<LraVarId> variables_in_multi_var_class;
        for (const std::vector<LraVarId>& equal_class : variable_classes) {
          std::size_t term_count = 0;
          for (const LraVarId variable : equal_class) {
            variables_in_multi_var_class.insert(variable);
            term_count += terms_by_variable.at(variable).size();
          }
          pair_count += unordered_pair_count(term_count);
        }
        for (const auto& [variable, mapped_terms] : terms_by_variable) {
          if (!variables_in_multi_var_class.contains(variable)) {
            pair_count += unordered_pair_count(mapped_terms.size());
          }
        }
        pair_count += unmapped_terms.size() * (shared_terms.size() - unmapped_terms.size());
        pair_count += unordered_pair_count(unmapped_terms.size());
        return pair_count;
      };

  const std::vector<std::vector<LraVarId>> original_classes =
      tableau.model_equal_classes(shared_variables);
  std::vector<std::pair<LraVarId, LraVarId>> forbidden_variables;
  forbidden_variables.reserve(forbidden_pairs.size());
  for (const TermPairKey& pair : forbidden_pairs) {
    const auto lhs = prepared.variables_by_term.find(pair.lhs);
    const auto rhs = prepared.variables_by_term.find(pair.rhs);
    if (lhs != prepared.variables_by_term.end() && rhs != prepared.variables_by_term.end()) {
      forbidden_variables.emplace_back(lhs->second, rhs->second);
    }
  }

  ProjectedClasses projection;
  LraModelEqualPartition partition;
  std::vector<theory::arithmetic::DeltaRational> candidate_values;
  if (diversify_model) {
    bool mutation_accepted = true;
    const std::size_t original_candidate_count = projected_pair_count(original_classes);
    std::vector<LraVarId> mutation_variables;
    if (mutation_scope == ModelEqualityMutationScope::All) {
      mutation_variables = shared_variables;
    } else {
      std::unordered_set<LraVarId> seen_mutation_variables;
      mutation_variables.reserve(mutation_terms.size());
      for (const core::TermId term : mutation_terms) {
        const auto variable = prepared.variables_by_term.find(term);
        if (variable != prepared.variables_by_term.end() &&
            seen_mutation_variables.insert(variable->second).second) {
          mutation_variables.push_back(variable->second);
        }
      }
      std::sort(mutation_variables.begin(), mutation_variables.end());
    }
    statistics.increment(":no.interface.lra-model-mutation-full-variables",
                         shared_variables.size());
    statistics.increment(":no.interface.lra-model-mutation-scoped-variables",
                         mutation_variables.size());
    if (mutation_variables.empty()) {
      statistics.increment(":no.interface.lra-model-mutation-empty-scopes");
    }
    const theory::arithmetic::LraTableau::ModelMutationResult mutation =
        tableau.diversify_shared_model(mutation_variables, forbidden_variables);
    candidate_values = mutation.values;
    if (mutation_scope == ModelEqualityMutationScope::All) {
      projection = project_classes(mutation.classes);
    } else {
      std::vector<LraVarId> ordered_variables = shared_variables;
      std::sort(ordered_variables.begin(), ordered_variables.end(),
                [&candidate_values](LraVarId lhs, LraVarId rhs) {
                  if (candidate_values.at(lhs) == candidate_values.at(rhs)) {
                    return lhs < rhs;
                  }
                  return candidate_values.at(lhs) < candidate_values.at(rhs);
                });
      std::vector<std::vector<LraVarId>> full_mutated_classes;
      for (std::size_t begin = 0; begin < ordered_variables.size();) {
        std::size_t end = begin + 1;
        while (end < ordered_variables.size() && candidate_values.at(ordered_variables[begin]) ==
                                                     candidate_values.at(ordered_variables[end])) {
          ++end;
        }
        if (end - begin > 1) {
          full_mutated_classes.emplace_back(
              ordered_variables.begin() + static_cast<std::ptrdiff_t>(begin),
              ordered_variables.begin() + static_cast<std::ptrdiff_t>(end));
        }
        begin = end;
      }
      projection = project_classes(full_mutated_classes);
      if (projection.candidate_pairs > original_candidate_count) {
        mutation_accepted = false;
        statistics.increment(":no.interface.lra-model-mutation-rejected-global-pair-regressions");
        projection = project_classes(original_classes);
        candidate_values.clear();
        candidate_values.reserve(tableau.variable_count());
        for (LraVarId variable = 0; variable < tableau.variable_count(); ++variable) {
          candidate_values.push_back(tableau.assignment(variable));
        }
      }
    }
    partition.candidate_pairs_before_mutation = original_candidate_count;
    statistics.increment(":no.interface.lra-model-mutation-calls");
    statistics.increment(":no.interface.lra-model-mutation-attempted-updates",
                         mutation.updates.size());
    statistics.increment(":no.interface.lra-model-mutation-updates",
                         mutation_accepted ? mutation.updates.size() : 0);
    statistics.increment(":no.interface.lra-model-mutation-value-classes-before",
                         mutation.classes_before);
    statistics.increment(":no.interface.lra-model-mutation-value-classes-after",
                         mutation.classes_after);
    statistics.increment(":no.interface.lra-model-mutation-candidate-pairs-before",
                         original_candidate_count);
    statistics.increment(":no.interface.lra-model-mutation-candidate-pairs-after",
                         projection.candidate_pairs);
    statistics.increment(":no.interface.lra-model-mutation-pruned-pairs",
                         original_candidate_count > projection.candidate_pairs
                             ? original_candidate_count - projection.candidate_pairs
                             : 0);
    statistics.increment(":no.shared-model.forbidden-before", mutation.forbidden_equalities_before);
    statistics.increment(":no.shared-model.forbidden-after",
                         mutation_accepted ? mutation.forbidden_equalities_after
                                           : mutation.forbidden_equalities_before);
  } else {
    projection = project_classes(original_classes);
    partition.candidate_pairs_before_mutation = projection.candidate_pairs;
    candidate_values.reserve(tableau.variable_count());
    for (LraVarId variable = 0; variable < tableau.variable_count(); ++variable) {
      candidate_values.push_back(tableau.assignment(variable));
    }
    const std::size_t forbidden_violations = static_cast<std::size_t>(
        std::count_if(forbidden_variables.begin(), forbidden_variables.end(),
                      [&candidate_values](const auto& pair) {
                        return candidate_values.at(pair.first) == candidate_values.at(pair.second);
                      }));
    statistics.increment(":no.shared-model.forbidden-before", forbidden_violations);
    statistics.increment(":no.shared-model.forbidden-after", forbidden_violations);
  }

  partition.classes = std::move(projection.classes);
  partition.shared_terms = shared_terms;
  std::sort(partition.shared_terms.begin(), partition.shared_terms.end());
  partition.shared_terms.erase(
      std::unique(partition.shared_terms.begin(), partition.shared_terms.end()),
      partition.shared_terms.end());
  partition.mapped_terms = std::move(projection.mapped_terms);
  partition.unmapped_terms.insert(unmapped_terms.begin(), unmapped_terms.end());
  partition.candidate_pair_count = projection.candidate_pairs;
  partition.class_by_term.reserve(partition.mapped_terms.size());
  partition.value_by_term.reserve(partition.mapped_terms.size());
  for (const auto& [variable, mapped_terms] : terms_by_variable) {
    for (const core::TermId term : mapped_terms) {
      partition.value_by_term.emplace(term, candidate_values.at(variable));
    }
  }
  if (build_concrete_values) {
    partition.concrete_values.reserve(prepared.variables_by_term.size());
    const util::Rational concrete_delta = choose_concrete_delta(
        prepared, candidate_values, assignments, shared_variables, statistics);
    for (const auto& [term, variable] : prepared.variables_by_term) {
      partition.concrete_values.emplace(
          term, concretize_delta_rational(candidate_values.at(variable), concrete_delta));
    }
  }
  for (std::size_t class_index = 0; class_index < partition.classes.size(); ++class_index) {
    for (const core::TermId term : partition.classes[class_index]) {
      partition.class_by_term.emplace(term, class_index);
    }
  }
  for (const TermPairKey& pair : forbidden_pairs) {
    const auto lhs = partition.value_by_term.find(pair.lhs);
    const auto rhs = partition.value_by_term.find(pair.rhs);
    if (lhs == partition.value_by_term.end() || rhs == partition.value_by_term.end() ||
        lhs->second == rhs->second) {
      partition.unresolved_forbidden_pairs.push_back(pair);
    }
  }
  statistics.set(":no.shared-model.forbidden-equalities", forbidden_pairs.size());
  statistics.set(":no.shared-model.unresolved-forbidden",
                 partition.unresolved_forbidden_pairs.size());
  const std::size_t full_pair_count = unordered_pair_count(shared_terms.size());
  statistics.increment(":no.interface.lra-model-filter-builds");
  statistics.increment(":no.interface.lra-model-filter-full-pairs", full_pair_count);
  statistics.increment(":no.interface.lra-model-filter-candidate-pairs",
                       partition.candidate_pair_count);
  statistics.increment(":no.interface.lra-model-filter-pruned-pairs",
                       full_pair_count - partition.candidate_pair_count);
  statistics.increment(":no.interface.lra-model-filter-unmapped-terms", unmapped_terms.size());
  statistics.increment(":no.interface.lra-model-filter-conservative-pairs",
                       projection.conservative_pairs);
  statistics.increment(":no.interface.lra-model-filter-classes", partition.classes.size());
  statistics.increment(":no.interface.lra-model-filter-class-terms",
                       partition.class_by_term.size());
  statistics.increment(":no.interface.lra-model-filter-physical-pairs", 0);
  return partition;
}

std::vector<TermPairKey> filter_lra_model_candidate_pairs(const std::vector<TermPairKey>& pairs,
                                                          const LraModelEqualPartition& partition,
                                                          util::Statistics& statistics) {
  std::vector<TermPairKey> filtered;
  filtered.reserve(pairs.size());
  for (const TermPairKey& pair : pairs) {
    if (partition.may_be_equal(pair.lhs, pair.rhs)) {
      filtered.push_back(pair);
    }
  }
  statistics.increment(":no.interface.lra-model-filter-sparse-pairs", pairs.size());
  statistics.increment(":no.interface.lra-model-filter-sparse-retained", filtered.size());
  return filtered;
}

bool flat_closure_equal_on_terms(const FlatEufModelView& euf, core::TermId lhs, core::TermId rhs);

std::vector<TermPairKey> unresolved_interface_pairs(const FlatEufModelView& euf,
                                                    const TheoryCombinationManager& manager,
                                                    const std::vector<TermPairKey>& pairs,
                                                    util::Statistics& statistics) {
  std::vector<TermPairKey> unresolved;
  unresolved.reserve(pairs.size());
  for (const TermPairKey& pair : pairs) {
    if (manager.interface_equality_assignment(pair.lhs, pair.rhs) ==
            InterfaceEqualityAssignment::True ||
        flat_closure_equal_on_terms(euf, pair.lhs, pair.rhs)) {
      statistics.increment(":no.interface.lra-exact-fallback-skipped-pairs");
      continue;
    }
    unresolved.push_back(pair);
  }
  statistics.increment(":no.interface.lra-exact-fallback-pairs", unresolved.size());
  return unresolved;
}

struct LraTheoryCheckResult {
  std::optional<sat::Clause> conflict;
  theory::arithmetic::LraTableau tableau;
};

bool same_theory_assignments(const std::vector<AssignedTheoryAtom>& lhs,
                             const std::vector<AssignedTheoryAtom>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (lhs[index].lit != rhs[index].lit || lhs[index].value != rhs[index].value ||
        lhs[index].term != rhs[index].term || lhs[index].filtered != rhs[index].filtered) {
      return false;
    }
  }
  return true;
}

bool same_interface_equality_atoms(const std::vector<core::TermId>& atoms,
                                   const std::vector<InterfaceEqualityRegistryEntry>& equalities) {
  if (atoms.size() != equalities.size()) {
    return false;
  }
  for (std::size_t index = 0; index < atoms.size(); ++index) {
    if (atoms[index] != equalities[index].atom) {
      return false;
    }
  }
  return true;
}

void assign_interface_equality_atoms(const std::vector<InterfaceEqualityRegistryEntry>& equalities,
                                     std::vector<core::TermId>& atoms) {
  atoms.resize(equalities.size());
  for (std::size_t index = 0; index < equalities.size(); ++index) {
    atoms[index] = equalities[index].atom;
  }
}

// QF_UFLRA-local ownership for one augmented component-model epoch. The exact
// interface key protects structural preparation reuse; the exact theory-
// assignment key protects checked-tableau reuse. This is the specialized
// component state suggested by de Moura-Bjorner Section 4, not generic M state.
struct MixedLraModelEpoch {
  std::vector<core::TermId> true_interface_equalities;
  std::optional<PreparedLraTheory> prepared;
  std::vector<AssignedTheoryAtom> checked_assignments;
  std::optional<theory::arithmetic::LraTableau> tableau;
  std::optional<sat::Clause> conflict;
  bool check_valid = false;
};

struct MixedEufCareEpoch {
  std::vector<AssignedTheoryAtom> checked_assignments;
  EufCarePairDiagnostics diagnostics;
  bool valid = false;
};

std::optional<sat::Clause> check_lra_theory_in_place(const PreparedLraTheory& prepared,
                                                     const std::vector<AssignedTheoryAtom>& atoms,
                                                     theory::arithmetic::LraTableau& tableau);

LraTheoryCheckResult check_incremental_lra_theory(const PreparedLraTheory& prepared,
                                                  const std::vector<AssignedTheoryAtom>& atoms);

std::optional<sat::Clause> find_incremental_lra_theory_conflict(
    const PreparedLraTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms);

bool lra_fixed_elimination_for_mixed(const CheckSatOptions& options) {
  return options.lra_fixed_elimination || options.qf_uflra_fixed_elimination;
}

std::optional<sat::Clause> lra_assignment_conflict_with_extra_atom(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments, core::TermId extra_atom,
    const CheckSatOptions& options, util::Statistics* statistics = nullptr) {
  if (statistics != nullptr) {
    statistics->increment(":no.interface.lra-extra-atom-probes");
    statistics->increment(":no.interface.lra-extra-atoms-probed");
  }
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, extra_atom});
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, extra_atom, false});
  const PreparedLraTheory augmented_lra_theory(
      terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, lra_fixed_elimination_for_mixed(options), false,
      /*least_violated_leaving=*/false, lra_tuning_from_options(options.lra_tuning));
  return find_incremental_lra_theory_conflict(augmented_lra_theory, augmented_lra_assignments);
}

struct LraImpliedRealEqualitiesResult {
  std::vector<ExplainedInterfaceEquality> equalities;
  bool probe_limit_hit = false;
};

struct LraEqualityProbeContext {
  theory::arithmetic::LraTableau tableau;
  theory::arithmetic::LraVarId difference_var = 0;
};

constexpr std::size_t kCheckedTableauProbeMaxDifferenceTerms = 8;
constexpr std::size_t kCheckedTableauProbeMaxPivotTermOpsPerVariable = 8;

// Per-pair exact convex-equality oracle. Following Dutertre and de Moura,
// SRI-CSL-06-01 Sections 3.1, 3.2.3, and 3.2.4, temporary assertions are
// reversible bounds. The accepted, feasible tableau already contains the
// current SAT assignment, so each context copies that state and expresses its
// difference row in the current nonbasic basis. One context is discarded after
// checking both strict orientations.
std::optional<LraEqualityProbeContext> prepare_lra_equality_probe_context(
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& checked_tableau,
    const std::vector<AssignedTheoryAtom>& lra_assignments, core::TermId lhs, core::TermId rhs,
    util::Statistics* statistics) {
  const auto lhs_var = prepared.variables_by_term.find(lhs);
  const auto rhs_var = prepared.variables_by_term.find(rhs);
  if (lhs_var == prepared.variables_by_term.end() || rhs_var == prepared.variables_by_term.end()) {
    if (statistics != nullptr) {
      statistics->increment(":no.interface.lra-probe-reuse-mapping-fallbacks");
    }
    return std::nullopt;
  }
  using DifferenceTerm = std::pair<theory::arithmetic::LraVarId, util::Rational>;
  const auto variable_expression = [&checked_tableau](theory::arithmetic::LraVarId var) {
    std::vector<DifferenceTerm> expression;
    if (!checked_tableau.is_basic(var)) {
      expression.emplace_back(var, util::Rational(1));
      return expression;
    }
    const theory::arithmetic::LraTableau::Row& row = checked_tableau.row(var);
    expression.reserve(row.size());
    for (const theory::arithmetic::LraTableau::RowEntry& entry : row) {
      expression.emplace_back(entry.var, entry.coeff);
    }
    return expression;
  };
  const std::vector<DifferenceTerm> lhs_expression = variable_expression(lhs_var->second);
  const std::vector<DifferenceTerm> rhs_expression = variable_expression(rhs_var->second);

  // Both endpoint expressions are sorted by variable id, as required by
  // LraTableau::Row. Merge them directly to retain that invariant without a
  // per-pair node-allocating coefficient map.
  std::vector<std::pair<theory::arithmetic::LraVarId, util::Rational>> difference_row;
  difference_row.reserve(lhs_expression.size() + rhs_expression.size());
  std::size_t lhs_index = 0;
  std::size_t rhs_index = 0;
  while (lhs_index < lhs_expression.size() || rhs_index < rhs_expression.size()) {
    theory::arithmetic::LraVarId var = 0;
    util::Rational coefficient(0);
    if (rhs_index == rhs_expression.size() ||
        (lhs_index < lhs_expression.size() &&
         lhs_expression[lhs_index].first < rhs_expression[rhs_index].first)) {
      var = lhs_expression[lhs_index].first;
      coefficient = lhs_expression[lhs_index].second;
      ++lhs_index;
    } else if (lhs_index == lhs_expression.size() ||
               rhs_expression[rhs_index].first < lhs_expression[lhs_index].first) {
      var = rhs_expression[rhs_index].first;
      coefficient = -rhs_expression[rhs_index].second;
      ++rhs_index;
    } else {
      var = lhs_expression[lhs_index].first;
      coefficient = lhs_expression[lhs_index].second - rhs_expression[rhs_index].second;
      ++lhs_index;
      ++rhs_index;
    }
    if (!coefficient.is_zero()) {
      difference_row.emplace_back(var, std::move(coefficient));
    }
  }
  if (statistics != nullptr) {
    statistics->set(
        ":no.interface.lra-probe-reuse-checked-tableau-max-difference-terms",
        std::max<std::uint64_t>(
            statistics->get(":no.interface.lra-probe-reuse-checked-tableau-max-difference-terms"),
            difference_row.size()));
    statistics->set(
        ":no.interface.lra-probe-reuse-checked-tableau-max-pivot-term-ops",
        std::max<std::uint64_t>(
            statistics->get(":no.interface.lra-probe-reuse-checked-tableau-max-pivot-term-ops"),
            checked_tableau.row_term_op_count()));
  }

  const bool difference_is_sparse = difference_row.size() <= kCheckedTableauProbeMaxDifferenceTerms;
  const bool basis_has_bounded_pivot_cost =
      checked_tableau.row_term_op_count() <=
      kCheckedTableauProbeMaxPivotTermOpsPerVariable * checked_tableau.variable_count();
  // A dense current basis can make one temporary repair much more expensive
  // than replaying the sparse base. These conservative guards retain the old
  // path when the checked snapshot already signals that risk.
  const bool use_checked_tableau = difference_is_sparse && basis_has_bounded_pivot_cost;
  theory::arithmetic::LraTableau tableau =
      use_checked_tableau ? checked_tableau : prepared.base_tableau;
  if (!use_checked_tableau) {
    if (statistics != nullptr) {
      statistics->increment(":no.interface.lra-probe-reuse-checked-tableau-fallbacks");
      if (!difference_is_sparse) {
        statistics->increment(":no.interface.lra-probe-reuse-checked-tableau-difference-fallbacks");
      }
      if (!basis_has_bounded_pivot_cost) {
        statistics->increment(":no.interface.lra-probe-reuse-checked-tableau-basis-cost-fallbacks");
      }
    }
    difference_row = {{lhs_var->second, util::Rational(1)}, {rhs_var->second, util::Rational(-1)}};
  }
  const theory::arithmetic::LraVarId difference_var = tableau.add_variable();
  // A checked copy can contain older trail entries. Every probe checkpoint is
  // taken after this row is installed, no pop crosses its creation point, and
  // the whole context is destroyed after the pair.
  tableau.add_row(difference_var, difference_row);
  if (use_checked_tableau) {
    assert(tableau.assignment(difference_var) == checked_tableau.assignment(lhs_var->second) -
                                                     checked_tableau.assignment(rhs_var->second));
  }
  if (!use_checked_tableau) {
    if (lra_assignments.size() != prepared.atoms.size()) {
      throw UnsupportedInput("internal theory atom mapping mismatch in LRA equality probe");
    }
    for (std::size_t i = 0; i < lra_assignments.size(); ++i) {
      const AssignedTheoryAtom& assignment = lra_assignments[i];
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
      if (!prepared_atom.is_arithmetic || assignment.filtered) {
        continue;
      }
      if (statistics != nullptr) {
        statistics->increment(":no.interface.lra-probe-reuse-assignment-bounds");
      }
      const std::optional<sat::Clause> conflict = assert_lra_atom_bound(
          tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs,
          assignment.value, prepared_atom.asserts_lower_when_true, blocking_lit(assignment));
      if (conflict.has_value()) {
        if (statistics != nullptr) {
          statistics->increment(":no.interface.lra-probe-reuse-context-conflicts");
        }
        throw std::logic_error(
            "internal LRA equality probe context conflicts after an accepted assignment");
      }
    }
    if (tableau.check().has_value()) {
      if (statistics != nullptr) {
        statistics->increment(":no.interface.lra-probe-reuse-context-conflicts");
      }
      throw std::logic_error(
          "internal LRA equality probe context conflicts after an accepted assignment");
    }
  }
  if (statistics != nullptr) {
    statistics->increment(":no.interface.lra-probe-reuse-pairs");
    if (use_checked_tableau) {
      statistics->increment(":no.interface.lra-probe-reuse-checked-tableau-copies");
    }
  }
  return LraEqualityProbeContext{std::move(tableau), difference_var};
}

std::optional<sat::Clause> probe_lra_equality_orientation(LraEqualityProbeContext& context,
                                                          bool lhs_less_than_rhs,
                                                          util::Statistics* statistics) {
  if (statistics != nullptr) {
    statistics->increment(":no.interface.lra-extra-atom-probes");
    statistics->increment(":no.interface.lra-extra-atoms-probed");
    statistics->increment(":no.interface.lra-probe-reuse-orientations");
  }
  const std::size_t checkpoint = context.tableau.push();
  const theory::arithmetic::DeltaRational strict_zero{util::Rational(0),
                                                      util::Rational(lhs_less_than_rhs ? -1 : 1)};
  std::optional<sat::Clause> conflict =
      lhs_less_than_rhs
          ? context.tableau.assert_upper(context.difference_var, strict_zero, sat::Lit{0})
          : context.tableau.assert_lower(context.difference_var, strict_zero, sat::Lit{0});
  if (!conflict.has_value()) {
    conflict = context.tableau.check();
  }
  context.tableau.pop(checkpoint);
  assert(context.tableau.occurs_in_index_matches_full_rebuild());
  return conflict;
}

bool consume_no_extra_atom_probe_budget(const CheckSatOptions& options,
                                        util::Statistics& statistics, std::size_t extra_atom_count,
                                        bool arrangement_probe) {
  if (options.nelson_oppen_arrangement_probe_limit.has_value() &&
      statistics.get(":no.interface.lra-extra-atom-probes") >=
          options.nelson_oppen_arrangement_probe_limit.value()) {
    statistics.set(":no.interface.lra-extra-atom-probe-limit-hit", 1);
    return false;
  }
  if (arrangement_probe) {
    statistics.increment(":no.interface.arrangement-lra-extra-atom-probes");
    statistics.increment(":no.interface.arrangement-lra-extra-atoms-probed", extra_atom_count);
  }
  return true;
}

struct LraImpliedRealEqualityProbeResult {
  std::optional<ExplainedInterfaceEquality> equality;
  bool probe_limit_hit = false;
};

LraImpliedRealEqualityProbeResult probe_lra_implied_real_equality(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& checked_tableau,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const TermPairKey& pair,
    const CheckSatOptions& options, util::Statistics* statistics) {
  if (statistics != nullptr &&
      !consume_no_extra_atom_probe_budget(options, *statistics, 1, false)) {
    return LraImpliedRealEqualityProbeResult{std::nullopt, true};
  }
  std::optional<LraEqualityProbeContext> reusable_context = prepare_lra_equality_probe_context(
      prepared, checked_tableau, lra_assignments, pair.lhs, pair.rhs, statistics);
  const auto probe_orientation = [&](bool lhs_less_than_rhs) {
    if (reusable_context.has_value()) {
      return probe_lra_equality_orientation(reusable_context.value(), lhs_less_than_rhs,
                                            statistics);
    }
    return lra_assignment_conflict_with_extra_atom(
        terms, lra_atoms, lra_assignments,
        lhs_less_than_rhs ? terms.mk_lt(pair.lhs, pair.rhs) : terms.mk_lt(pair.rhs, pair.lhs),
        options, statistics);
  };

  std::optional<sat::Clause> lhs_lt_rhs_conflict = probe_orientation(true);
  if (!lhs_lt_rhs_conflict.has_value()) {
    return {};
  }
  if (statistics != nullptr &&
      !consume_no_extra_atom_probe_budget(options, *statistics, 1, false)) {
    return LraImpliedRealEqualityProbeResult{std::nullopt, true};
  }
  std::optional<sat::Clause> rhs_lt_lhs_conflict = probe_orientation(false);
  if (!rhs_lt_lhs_conflict.has_value()) {
    return {};
  }

  sat::Clause reason;
  for (const sat::Clause* conflict : {&lhs_lt_rhs_conflict.value(), &rhs_lt_lhs_conflict.value()}) {
    for (const sat::Lit lit : *conflict) {
      if (lit.value != 0) {
        append_lit_once(reason, lit);
      }
    }
  }
  return LraImpliedRealEqualityProbeResult{
      ExplainedInterfaceEquality{terms.mk_eq(pair.lhs, pair.rhs), std::move(reason)}, false};
}

LraImpliedRealEqualitiesResult lra_implied_real_equalities_for_pairs(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& checked_tableau,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const std::vector<TermPairKey>& pairs,
    const CheckSatOptions& options, util::Statistics* statistics = nullptr) {
  std::vector<ExplainedInterfaceEquality> equalities;
  equalities.reserve(pairs.size());
  for (const TermPairKey& pair : pairs) {
    LraImpliedRealEqualityProbeResult probe = probe_lra_implied_real_equality(
        terms, lra_atoms, prepared, checked_tableau, lra_assignments, pair, options, statistics);
    if (probe.probe_limit_hit) {
      return LraImpliedRealEqualitiesResult{std::move(equalities), true};
    }
    if (probe.equality.has_value()) {
      equalities.push_back(std::move(probe.equality.value()));
    }
  }
  return LraImpliedRealEqualitiesResult{std::move(equalities), false};
}

// Resolves entailed equalities within one LRA model-equal class using at
// most (k-1) probes when the class is fully entailed-equal, degrading
// gracefully -- never worse than full pairwise probing -- when it splits
// into genuinely distinct sub-groups. LRA-entailment of equality is
// transitive: once probe(anchor, x) and probe(anchor, y) both succeed, x=y
// is entailed without a direct probe, with reason = union(reason_x, reason_y).
// Cross-group pairs never need checking: if x fails against anchor but were
// entailed-equal to some member m of anchor's already-linked group, then
// x=m and m=anchor would together entail x=anchor by transitivity,
// contradicting the failure.
LraImpliedRealEqualitiesResult resolve_class_entailed_equalities(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& checked_tableau,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& class_terms, const FlatEufModelView& euf,
    const TheoryCombinationManager& manager, const CheckSatOptions& options,
    util::Statistics& statistics) {
  std::vector<ExplainedInterfaceEquality> equalities;
  std::vector<core::TermId> remaining = class_terms;
  while (remaining.size() > 1) {
    const core::TermId anchor = remaining.back();
    remaining.pop_back();
    std::vector<core::TermId> linked;
    std::vector<sat::Clause> reason_from_anchor;
    std::vector<core::TermId> unresolved;
    for (const core::TermId candidate : remaining) {
      const TermPairKey pair = canonical_term_pair(anchor, candidate);
      statistics.increment(":no.interface.lra-model-filter-physical-pairs");
      statistics.increment(":no.interface.lra-exact-fallback-streamed-pairs");
      if (manager.interface_equality_assignment(pair.lhs, pair.rhs) ==
              InterfaceEqualityAssignment::True ||
          flat_closure_equal_on_terms(euf, pair.lhs, pair.rhs)) {
        // Already true against this anchor specifically -- not (yet) known
        // true against any other class member, so it still needs a real
        // probe if it's grouped under a different anchor later. Defer it
        // rather than dropping it, matching what an exhaustive scan would
        // have found for it.
        statistics.increment(":no.interface.lra-exact-fallback-skipped-pairs");
        unresolved.push_back(candidate);
        continue;
      }
      statistics.increment(":no.interface.lra-exact-fallback-pairs");
      LraImpliedRealEqualityProbeResult probe = probe_lra_implied_real_equality(
          terms, lra_atoms, prepared, checked_tableau, lra_assignments, pair, options, &statistics);
      if (probe.probe_limit_hit) {
        return LraImpliedRealEqualitiesResult{std::move(equalities), true};
      }
      if (probe.equality.has_value()) {
        linked.push_back(candidate);
        reason_from_anchor.push_back(probe.equality->reason.value());
        equalities.push_back(std::move(probe.equality.value()));
      } else {
        unresolved.push_back(candidate);
      }
    }
    for (std::size_t i = 0; i < linked.size(); ++i) {
      for (std::size_t j = i + 1; j < linked.size(); ++j) {
        const TermPairKey transitive_pair = canonical_term_pair(linked[i], linked[j]);
        if (manager.interface_equality_assignment(transitive_pair.lhs, transitive_pair.rhs) ==
                InterfaceEqualityAssignment::True ||
            flat_closure_equal_on_terms(euf, transitive_pair.lhs, transitive_pair.rhs)) {
          continue;
        }
        statistics.increment(":no.interface.lra-exact-fallback-transitive-pairs");
        sat::Clause combined = reason_from_anchor[i];
        for (const sat::Lit lit : reason_from_anchor[j]) {
          append_lit_once(combined, lit);
        }
        equalities.push_back(
            ExplainedInterfaceEquality{terms.mk_eq(linked[i], linked[j]), std::move(combined)});
      }
    }
    remaining = std::move(unresolved);
  }
  return LraImpliedRealEqualitiesResult{std::move(equalities), false};
}

LraImpliedRealEqualitiesResult lra_implied_real_equalities_for_partition(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const PreparedLraTheory& prepared_lra, const theory::arithmetic::LraTableau& checked_tableau,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const LraModelEqualPartition& partition,
    const FlatEufModelView& euf, const TheoryCombinationManager& manager,
    const CheckSatOptions& options, util::Statistics& statistics) {
  std::vector<ExplainedInterfaceEquality> equalities;
  for (const std::vector<core::TermId>& equal_class : partition.classes) {
    LraImpliedRealEqualitiesResult class_result = resolve_class_entailed_equalities(
        terms, lra_atoms, prepared_lra, checked_tableau, lra_assignments, equal_class, euf, manager,
        options, statistics);
    equalities.insert(equalities.end(), std::make_move_iterator(class_result.equalities.begin()),
                      std::make_move_iterator(class_result.equalities.end()));
    if (class_result.probe_limit_hit) {
      return LraImpliedRealEqualitiesResult{std::move(equalities), true};
    }
  }
  // Unmapped-terms phase: left exactly as-is. There's no class structure to
  // exploit transitivity over here (these terms aren't grouped by value).
  LraModelPairCursor cursor(partition, /*skip_class_phase=*/true);
  while (const std::optional<TermPairKey> pair = cursor.next()) {
    statistics.increment(":no.interface.lra-model-filter-physical-pairs");
    statistics.increment(":no.interface.lra-exact-fallback-streamed-pairs");
    if (manager.interface_equality_assignment(pair->lhs, pair->rhs) ==
            InterfaceEqualityAssignment::True ||
        flat_closure_equal_on_terms(euf, pair->lhs, pair->rhs)) {
      statistics.increment(":no.interface.lra-exact-fallback-skipped-pairs");
      continue;
    }
    statistics.increment(":no.interface.lra-exact-fallback-pairs");
    LraImpliedRealEqualityProbeResult probe =
        probe_lra_implied_real_equality(terms, lra_atoms, prepared_lra, checked_tableau,
                                        lra_assignments, pair.value(), options, &statistics);
    if (probe.probe_limit_hit) {
      return LraImpliedRealEqualitiesResult{std::move(equalities), true};
    }
    if (probe.equality.has_value()) {
      equalities.push_back(std::move(probe.equality.value()));
    }
  }
  return LraImpliedRealEqualitiesResult{std::move(equalities), false};
}

bool lra_assignment_conflicts_with_extra_atoms(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& extra_atoms, const CheckSatOptions& options,
    util::Statistics* statistics = nullptr) {
  if (statistics != nullptr) {
    statistics->increment(":no.interface.lra-extra-atom-probes");
    statistics->increment(":no.interface.lra-extra-atoms-probed", extra_atoms.size());
  }
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.reserve(lra_atoms.size() + extra_atoms.size());
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.reserve(lra_assignments.size() + extra_atoms.size());
  for (const core::TermId extra_atom : extra_atoms) {
    augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, extra_atom});
    augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, extra_atom, false});
  }
  const PreparedLraTheory augmented_lra_theory(
      terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, lra_fixed_elimination_for_mixed(options), false,
      /*least_violated_leaving=*/false, lra_tuning_from_options(options.lra_tuning));
  return find_incremental_lra_theory_conflict(augmented_lra_theory, augmented_lra_assignments)
      .has_value();
}

bool flat_closure_equal_on_terms(const FlatEufModelView& euf, core::TermId lhs, core::TermId rhs) {
  const std::optional<theory::equality::FlatTermId> lhs_flat = euf.flat.existing_flat_term(lhs);
  const std::optional<theory::equality::FlatTermId> rhs_flat = euf.flat.existing_flat_term(rhs);
  return lhs_flat.has_value() && rhs_flat.has_value() &&
         euf.closure.are_equal(lhs_flat.value(), rhs_flat.value());
}

bool flat_closure_forbids_equality_on_terms(const FlatEufModelView& euf, core::TermId lhs,
                                            core::TermId rhs) {
  const std::optional<theory::equality::FlatTermId> lhs_flat = euf.flat.existing_flat_term(lhs);
  const std::optional<theory::equality::FlatTermId> rhs_flat = euf.flat.existing_flat_term(rhs);
  if (!lhs_flat.has_value() || !rhs_flat.has_value() ||
      euf.closure.are_equal(lhs_flat.value(), rhs_flat.value())) {
    return false;
  }

  const std::size_t checkpoint = euf.closure.push();
  euf.closure.assert_equal(lhs_flat.value(), rhs_flat.value());
  const bool forbidden = euf.closure.conflict().has_value();
  euf.closure.pop(checkpoint);
  return forbidden;
}

// EUF-only Real terms are not Nelson-Oppen interface terms and must not enter
// arrangement search. They still need concrete representatives when a combined
// model is printed. Assign every Real constant and Real-valued application in
// each EUF class so the model printer cannot lose a purified proxy equality.
// Give every unanchored class a fresh value while preserving the LRA value of
// any class containing a shared term.
bool complete_euf_real_model_values(const FlatEufModelView& euf,
                                    const std::vector<core::TermId>& euf_real_terms,
                                    const std::vector<core::TermId>& shared_real_terms,
                                    RealModelValues& values, util::Statistics& statistics) {
  std::unordered_set<core::TermId> shared(shared_real_terms.begin(), shared_real_terms.end());
  std::vector<theory::equality::FlatTermId> flat_terms;
  flat_terms.reserve(euf_real_terms.size());
  std::unordered_map<theory::equality::FlatTermId, std::vector<core::TermId>> terms_by_flat;
  terms_by_flat.reserve(euf_real_terms.size());
  for (const core::TermId term : euf_real_terms) {
    const std::optional<theory::equality::FlatTermId> flat = euf.flat.existing_flat_term(term);
    if (!flat.has_value()) {
      statistics.increment(":no.interface.model-euf-real-unmapped-terms");
      return false;
    }
    flat_terms.push_back(flat.value());
    terms_by_flat[flat.value()].push_back(term);
  }

  std::vector<util::Rational> occupied;
  occupied.reserve(shared_real_terms.size() + euf_real_terms.size());
  for (const core::TermId term : shared_real_terms) {
    const auto found = values.find(term);
    if (found == values.end()) {
      statistics.increment(":no.interface.model-euf-real-value-conflicts");
      return false;
    }
    if (std::find(occupied.begin(), occupied.end(), found->second) == occupied.end()) {
      occupied.push_back(found->second);
    }
  }

  std::int64_t next_fresh = 0;
  const std::vector<std::vector<theory::equality::FlatTermId>> classes =
      euf.closure.equivalence_classes(flat_terms);
  statistics.set(":no.interface.model-euf-real-classes", classes.size());
  for (const std::vector<theory::equality::FlatTermId>& equal_class : classes) {
    std::optional<util::Rational> class_value;
    for (const theory::equality::FlatTermId flat : equal_class) {
      for (const core::TermId term : terms_by_flat.at(flat)) {
        if (!shared.contains(term)) {
          continue;
        }
        const util::Rational& value = values.at(term);
        if (class_value.has_value() && class_value.value() != value) {
          statistics.increment(":no.interface.model-euf-real-value-conflicts");
          return false;
        }
        class_value = value;
      }
    }

    if (class_value.has_value()) {
      statistics.increment(":no.interface.model-euf-real-anchored-classes");
    } else {
      util::Rational fresh(next_fresh++);
      while (std::find(occupied.begin(), occupied.end(), fresh) != occupied.end()) {
        fresh = util::Rational(next_fresh++);
      }
      class_value = std::move(fresh);
      occupied.push_back(class_value.value());
      statistics.increment(":no.interface.model-euf-real-fresh-classes");
    }
    for (const theory::equality::FlatTermId flat : equal_class) {
      for (const core::TermId term : terms_by_flat.at(flat)) {
        values.insert_or_assign(term, class_value.value());
      }
    }
  }
  return true;
}

struct SharedModelReconciliationResult {
  bool compatible = false;
  bool requires_exact_fallback = false;
  std::vector<TermPairKey> split_pairs;
};

// de Moura-Bjorner, Model-Based Theory Combination, Sections 4 and 5: test
// whether EUF can absorb a spanning presentation of the LRA candidate model.
// These merges are model choices, not consequences. Collect every rejected
// edge from one candidate projection as an assumption frontier; the eventual
// failed-assumption clause, rather than this scan, is the SAT-visible result.
SharedModelReconciliationResult reconcile_lra_candidate_with_euf(const FlatEufModelView& euf,
                                                                 TheoryCombinationManager& manager,
                                                                 util::Statistics& statistics) {
  std::vector<std::pair<core::TermId, theory::equality::FlatTermId>> shared_flat_terms;
  shared_flat_terms.reserve(manager.shared_model().lra_mapped_terms.size());
  for (const core::TermId term : manager.shared_model().lra_mapped_terms) {
    const std::optional<theory::equality::FlatTermId> flat = euf.flat.existing_flat_term(term);
    if (!flat.has_value()) {
      statistics.increment(":no.shared-model.compatibility-unmapped");
      return SharedModelReconciliationResult{
          .compatible = false, .requires_exact_fallback = true, .split_pairs = {}};
    }
    shared_flat_terms.emplace_back(term, flat.value());
  }
  std::sort(shared_flat_terms.begin(), shared_flat_terms.end());

  std::vector<theory::equality::FlatTermId> flat_terms;
  flat_terms.reserve(shared_flat_terms.size());
  std::unordered_map<theory::equality::FlatTermId, core::TermId> term_by_flat;
  term_by_flat.reserve(shared_flat_terms.size());
  for (const auto& [term, flat] : shared_flat_terms) {
    flat_terms.push_back(flat);
    term_by_flat.emplace(flat, term);
  }

  const auto lra_agrees_with_euf_projection = [&]() {
    const std::vector<std::vector<theory::equality::FlatTermId>> classes =
        euf.closure.equivalence_classes(flat_terms);
    statistics.increment(":no.shared-model.compatibility-projections");
    statistics.increment(":no.shared-model.compatibility-projection-terms", flat_terms.size());
    for (const std::vector<theory::equality::FlatTermId>& equal_class : classes) {
      if (equal_class.size() < 2) {
        continue;
      }
      const core::TermId representative = term_by_flat.at(equal_class.front());
      for (std::size_t index = 1; index < equal_class.size(); ++index) {
        if (!manager.lra_model_agrees_equal(representative, term_by_flat.at(equal_class[index]))) {
          return false;
        }
      }
    }
    return true;
  };

  const std::size_t checkpoint = euf.closure.push();
  std::vector<SharedModelEquality> accepted_equalities;
  accepted_equalities.reserve(manager.shared_model().lra_model_spanning_equalities.size());
  if (!lra_agrees_with_euf_projection()) {
    euf.closure.pop(checkpoint);
    statistics.increment(":no.shared-model.compatibility-initial-disagreements");
    return SharedModelReconciliationResult{
        .compatible = false, .requires_exact_fallback = true, .split_pairs = {}};
  }
  std::vector<TermPairKey> rejected_edges;
  rejected_edges.reserve(manager.shared_model().lra_model_spanning_equalities.size());
  for (const SharedModelEquality& equality : manager.shared_model().lra_model_spanning_equalities) {
    const std::optional<theory::equality::FlatTermId> lhs =
        euf.flat.existing_flat_term(equality.lhs);
    const std::optional<theory::equality::FlatTermId> rhs =
        euf.flat.existing_flat_term(equality.rhs);
    if (!lhs.has_value() || !rhs.has_value()) {
      euf.closure.pop(checkpoint);
      statistics.increment(":no.shared-model.compatibility-unmapped");
      return SharedModelReconciliationResult{
          .compatible = false, .requires_exact_fallback = true, .split_pairs = {}};
    }
    statistics.increment(":no.shared-model.compatibility-edges");
    if (euf.closure.are_equal(lhs.value(), rhs.value())) {
      accepted_equalities.push_back(equality);
      statistics.increment(":no.shared-model.compatibility-already-equal");
      continue;
    }

    const std::size_t edge_checkpoint = euf.closure.push();
    euf.closure.assert_equal(lhs.value(), rhs.value());
    if (euf.closure.conflict().has_value()) {
      euf.closure.pop(edge_checkpoint);
      statistics.increment(":no.shared-model.compatibility-splits");
      rejected_edges.push_back(canonical_term_pair(equality.lhs, equality.rhs));
      continue;
    }
    if (!lra_agrees_with_euf_projection()) {
      euf.closure.pop(edge_checkpoint);
      statistics.increment(":no.shared-model.compatibility-consequence-disagreements");
      statistics.increment(":no.shared-model.compatibility-splits");
      rejected_edges.push_back(canonical_term_pair(equality.lhs, equality.rhs));
      continue;
    }
    accepted_equalities.push_back(equality);
    statistics.increment(":no.shared-model.compatibility-merges");
  }

  if (!rejected_edges.empty()) {
    euf.closure.pop(checkpoint);
    std::sort(rejected_edges.begin(), rejected_edges.end(), less_term_pair);
    const auto unique_end = std::unique(rejected_edges.begin(), rejected_edges.end());
    statistics.increment(":no.shared-model.split-frontier-duplicates",
                         static_cast<std::size_t>(rejected_edges.end() - unique_end));
    rejected_edges.erase(unique_end, rejected_edges.end());
    statistics.increment(":no.shared-model.split-frontier-batches");
    statistics.increment(":no.shared-model.split-frontier-pairs", rejected_edges.size());
    statistics.set(":no.shared-model.split-frontier-max",
                   std::max<std::uint64_t>(statistics.get(":no.shared-model.split-frontier-max"),
                                           rejected_edges.size()));
    return SharedModelReconciliationResult{.compatible = false,
                                           .requires_exact_fallback = false,
                                           .split_pairs = std::move(rejected_edges)};
  }

  for (const SharedModelEquality& equality : accepted_equalities) {
    manager.record_internal_model_equality(equality.lhs, equality.rhs);
  }
  statistics.increment(":no.shared-model.compatibility-successes");
  return SharedModelReconciliationResult{
      .compatible = true, .requires_exact_fallback = false, .split_pairs = {}};
}

class CheckedProjectionComponent final : public theory::TheorySolver {
 public:
  using MutationFunction =
      std::function<theory::TheoryMutationResult(const theory::TheoryMutationRequest&)>;

  CheckedProjectionComponent(std::string_view id, core::SortId shared_sort,
                             theory::Convexity convexity, theory::TheoryModelProjection projection,
                             std::vector<theory::InterfacePair> care_pairs = {},
                             MutationFunction mutation = {})
      : properties_{.id = id,
                    .shared_sorts = {shared_sort},
                    .stably_infinite = theory::StableInfiniteness::Yes,
                    .convexity = convexity,
                    .produces_model_projection = true,
                    .propagates_equalities = false,
                    .supplies_care_pairs = !care_pairs.empty()},
        projection_(std::move(projection)),
        care_pairs_(std::move(care_pairs)),
        mutation_(std::move(mutation)) {}

  const theory::TheoryProperties& properties() const override { return properties_; }

  theory::TheoryCheckResult check(std::span<const theory::AssignedTheoryLiteral>,
                                  std::span<const theory::InterfaceRelation>) override {
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::Consistent;
    result.model = projection_;
    return result;
  }

  std::vector<theory::InterfacePair> care_pairs(
      const theory::TheoryModelToken& token) const override {
    if (token != projection_.token) {
      return {};
    }
    return care_pairs_;
  }

  theory::TheoryMutationResult mutate(const theory::TheoryMutationRequest& request) override {
    if (!mutation_) {
      return theory::TheoryMutationResult{};
    }
    return mutation_(request);
  }

 private:
  theory::TheoryProperties properties_;
  theory::TheoryModelProjection projection_;
  std::vector<theory::InterfacePair> care_pairs_;
  MutationFunction mutation_;
};

std::optional<theory::TheoryModelProjection> project_euf_shared_model(
    const FlatEufModelView& euf, const std::vector<core::TermId>& shared_terms,
    core::SortId shared_sort, std::uint64_t assignment_epoch, std::uint64_t revision = 1) {
  std::unordered_map<theory::equality::FlatTermId, std::vector<core::TermId>> terms_by_flat;
  std::vector<theory::equality::FlatTermId> flat_terms;
  terms_by_flat.reserve(shared_terms.size());
  flat_terms.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    const std::optional<theory::equality::FlatTermId> flat = euf.flat.existing_flat_term(term);
    if (!flat.has_value()) {
      return std::nullopt;
    }
    auto [entry, inserted] = terms_by_flat.try_emplace(flat.value());
    entry->second.push_back(term);
    if (inserted) {
      flat_terms.push_back(flat.value());
    }
  }
  std::sort(flat_terms.begin(), flat_terms.end());
  std::vector<theory::SharedModelClass> classes;
  for (const std::vector<theory::equality::FlatTermId>& flat_class :
       euf.closure.equivalence_classes(flat_terms)) {
    theory::SharedModelClass model_class;
    for (const theory::equality::FlatTermId flat : flat_class) {
      const std::vector<core::TermId>& terms = terms_by_flat.at(flat);
      model_class.terms.insert(model_class.terms.end(), terms.begin(), terms.end());
    }
    std::sort(model_class.terms.begin(), model_class.terms.end());
    classes.push_back(std::move(model_class));
  }
  std::sort(classes.begin(), classes.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.terms < rhs.terms; });
  return theory::TheoryModelProjection{
      .token = {.component = "euf", .assignment_epoch = assignment_epoch, .revision = revision},
      .partitions = {{.sort = shared_sort, .equality_classes = std::move(classes)}}};
}

// The revision is a parameter, not a constant, because reconcile() rejects a
// mutation whose token does not strictly advance it. A component that mutates
// through this helper must pass its own monotone counter; 1 is only the right
// value for the first projection of an assignment.
theory::TheoryModelProjection project_lra_shared_model(const LraModelEqualPartition& partition,
                                                       core::SortId shared_sort,
                                                       std::uint64_t assignment_epoch,
                                                       std::uint64_t revision = 1) {
  std::vector<theory::SharedModelClass> classes;
  std::unordered_set<core::TermId> classified;
  classes.reserve(partition.classes.size() + partition.shared_terms.size());
  for (const std::vector<core::TermId>& equal_class : partition.classes) {
    classes.push_back(theory::SharedModelClass{.terms = equal_class});
    classified.insert(equal_class.begin(), equal_class.end());
  }
  for (const core::TermId term : partition.shared_terms) {
    if (!classified.contains(term)) {
      classes.push_back(theory::SharedModelClass{.terms = {term}});
    }
  }
  std::sort(classes.begin(), classes.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.terms < rhs.terms; });
  return theory::TheoryModelProjection{
      .token = {.component = "lra", .assignment_epoch = assignment_epoch, .revision = revision},
      .partitions = {{.sort = shared_sort, .equality_classes = std::move(classes)}}};
}

void publish_model_based_combination_statistics(const ModelBasedCombinationStatistics& source,
                                                util::Statistics& statistics) {
  statistics.set(":combination.model.component-checks", source.component_checks);
  statistics.set(":combination.model.snapshots", source.snapshots);
  statistics.set(":combination.model.mutations", source.mutations);
  statistics.set(":combination.model.fixed-points", source.fixed_points);
  statistics.set(":combination.model.care-pairs", source.care_pairs);
  statistics.set(":combination.model.disagreements", source.disagreements);
  statistics.set(":combination.model.learned-clauses", source.learned_clauses);
  statistics.set(":combination.model.decisions", source.model_decisions);
  statistics.set(":combination.model.fresh-decisions", source.fresh_decisions);
  statistics.set(":combination.model.stale-snapshots", source.stale_snapshots);
  statistics.set(":combination.model.rejected-mutations", source.rejected_mutations);
}

bool equality_list_contains_pair(const core::TermManager& terms,
                                 const std::vector<core::TermId>& equalities, core::TermId lhs,
                                 core::TermId rhs) {
  for (const core::TermId equality_id : equalities) {
    const core::Term& equality = terms.get(equality_id);
    if (equality.kind != core::TermKind::Equality || equality.children.size() != 2) {
      continue;
    }
    const core::TermId current_lhs = equality.children.at(0);
    const core::TermId current_rhs = equality.children.at(1);
    if ((current_lhs == lhs && current_rhs == rhs) || (current_lhs == rhs && current_rhs == lhs)) {
      return true;
    }
  }
  return false;
}

struct LraToEufEqualityFilterResult {
  std::vector<ExplainedInterfaceEquality> forced_equalities;
  std::vector<core::TermId> model_equalities;
  std::optional<sat::Clause> direct_conflict;
  std::size_t already_true = 0;
  std::size_t mergeable_skipped = 0;
  std::size_t mergeable_forced = 0;
  std::size_t forbidden_forced = 0;
  std::size_t euf_consequences = 0;
  std::size_t model_disagreements = 0;
  std::size_t unmapped_consequences = 0;
};

std::vector<std::pair<core::TermId, theory::equality::FlatTermId>> shared_interface_flat_terms(
    const FlatEufModelView& euf, const TheoryCombinationManager& manager) {
  std::vector<std::pair<core::TermId, theory::equality::FlatTermId>> flat_terms;
  flat_terms.reserve(manager.summary().terms.size());
  for (const InterfaceTerm& interface_term : manager.summary().terms) {
    const std::optional<theory::equality::FlatTermId> flat =
        euf.flat.existing_flat_term(interface_term.term);
    if (flat.has_value()) {
      flat_terms.emplace_back(interface_term.term, flat.value());
    }
  }
  std::sort(flat_terms.begin(), flat_terms.end());
  return flat_terms;
}

LraToEufEqualityFilterResult filter_lra_implied_equalities_for_euf(
    core::TermManager& terms, TheoryCombinationManager& manager, const FlatEufModelView& euf,
    const std::vector<ExplainedInterfaceEquality>& equalities, bool reconcile_shared_model,
    util::Statistics& statistics) {
  LraToEufEqualityFilterResult result;
  result.forced_equalities.reserve(equalities.size());
  result.model_equalities.reserve(equalities.size());
  std::unordered_set<TermPairKey, TermPairKeyHash> model_equality_pairs;
  std::unordered_set<TermPairKey, TermPairKeyHash> forced_equality_pairs;
  model_equality_pairs.reserve(equalities.size());
  forced_equality_pairs.reserve(equalities.size());
  const auto append_unique_model_equality = [&](core::TermId equality, core::TermId lhs,
                                                core::TermId rhs) {
    statistics.increment(":no.interface.lra-to-euf-pair-dedup-lookups");
    if (model_equality_pairs.insert(canonical_term_pair(lhs, rhs)).second) {
      result.model_equalities.push_back(equality);
    } else {
      statistics.increment(":no.interface.lra-to-euf-pair-dedup-duplicates");
    }
  };
  const auto append_unique_forced_equality =
      [&]<typename Equality>(Equality&& equality, core::TermId lhs, core::TermId rhs) {
        statistics.increment(":no.interface.lra-to-euf-pair-dedup-lookups");
        if (forced_equality_pairs.insert(canonical_term_pair(lhs, rhs)).second) {
          result.forced_equalities.push_back(std::forward<Equality>(equality));
          return true;
        } else {
          statistics.increment(":no.interface.lra-to-euf-pair-dedup-duplicates");
          return false;
        }
      };
  const std::size_t filter_checkpoint = euf.closure.push();

  for (const ExplainedInterfaceEquality& explained_equality : equalities) {
    const core::TermId equality_id = explained_equality.equality;
    const core::Term& equality = terms.get(equality_id);
    if (equality.kind != core::TermKind::Equality || equality.children.size() != 2) {
      continue;
    }
    const core::TermId lhs = equality.children.at(0);
    const core::TermId rhs = equality.children.at(1);
    if (!manager.ensure_interface_pair(lhs, rhs).has_value()) {
      continue;
    }
    manager.mark_interface_equality_classification(equality_id,
                                                   InterfaceEqualityClassification::LraImplied);
    append_unique_model_equality(equality_id, lhs, rhs);

    if (flat_closure_equal_on_terms(euf, lhs, rhs)) {
      ++result.already_true;
      manager.mark_interface_equality_classification(
          equality_id, InterfaceEqualityClassification::EufAlreadyTrue);
      continue;
    }

    const std::optional<theory::equality::FlatTermId> lhs_flat = euf.flat.existing_flat_term(lhs);
    const std::optional<theory::equality::FlatTermId> rhs_flat = euf.flat.existing_flat_term(rhs);
    if (!lhs_flat.has_value() || !rhs_flat.has_value()) {
      ++result.mergeable_forced;
      manager.mark_interface_equality_classification(equality_id,
                                                     InterfaceEqualityClassification::EufMergeable);
      append_unique_forced_equality(explained_equality, lhs, rhs);
      continue;
    }

    if (reconcile_shared_model) {
      if (!explained_equality.reason.has_value()) {
        ++result.mergeable_forced;
        result.forced_equalities.push_back(explained_equality);
        forced_equality_pairs.insert(canonical_term_pair(lhs, rhs));
        statistics.increment(":no.shared-model.worklist-missing-reasons");
        continue;
      }
      if (manager.enqueue_explained_shared_equality(lhs, rhs, explained_equality.reason.value(),
                                                    SharedEqualitySource::LraEntailed)) {
        statistics.increment(":no.shared-model.worklist-enqueued");
      } else {
        statistics.increment(":no.shared-model.worklist-deduplicated");
      }
      continue;
    }

    const std::size_t before_pair = euf.closure.push();
    euf.closure.assert_equal(lhs_flat.value(), rhs_flat.value());
    if (!euf.closure.conflict().has_value()) {
      manager.mark_interface_equality_classification(equality_id,
                                                     InterfaceEqualityClassification::EufMergeable);
      ++result.mergeable_forced;
      append_unique_forced_equality(explained_equality, lhs, rhs);
      euf.closure.pop(before_pair);
      continue;
    }

    euf.closure.pop(before_pair);
    if (append_unique_forced_equality(explained_equality, lhs, rhs)) {
      ++result.forbidden_forced;
      manager.mark_interface_equality_classification(equality_id,
                                                     InterfaceEqualityClassification::EufForbidden);
    }
  }
  if (reconcile_shared_model) {
    const std::vector<ExplainedSharedEquality> worklist =
        manager.drain_explained_shared_equalities();
    statistics.increment(":no.shared-model.worklist-drained", worklist.size());
    for (const ExplainedSharedEquality& explained : worklist) {
      const std::optional<theory::equality::FlatTermId> lhs_flat =
          euf.flat.existing_flat_term(explained.lhs);
      const std::optional<theory::equality::FlatTermId> rhs_flat =
          euf.flat.existing_flat_term(explained.rhs);
      if (!lhs_flat.has_value() || !rhs_flat.has_value()) {
        ++result.mergeable_forced;
        const core::TermId equality = terms.mk_eq(explained.lhs, explained.rhs);
        result.forced_equalities.push_back(ExplainedInterfaceEquality{equality, explained.reasons});
        forced_equality_pairs.insert(canonical_term_pair(explained.lhs, explained.rhs));
        continue;
      }
      const std::size_t merge_checkpoint = euf.closure.push();
      euf.closure.assert_equal(lhs_flat.value(), rhs_flat.value(), explained.reasons);
      if (const std::optional<sat::Clause> conflict = euf.closure.conflict();
          conflict.has_value()) {
        result.direct_conflict = conflict;
        euf.closure.pop(merge_checkpoint);
        statistics.increment(":no.shared-model.direct-conflicts");
        statistics.increment(":no.shared-model.direct-conflict-lits", conflict->size());
        break;
      }
      manager.mark_interface_equality_classification(explained.lhs, explained.rhs,
                                                     InterfaceEqualityClassification::EufMergeable);
    }

    if (!result.direct_conflict.has_value() && !worklist.empty()) {
      const std::vector<std::pair<core::TermId, theory::equality::FlatTermId>> shared_flat_terms =
          shared_interface_flat_terms(euf, manager);
      std::vector<theory::equality::FlatTermId> flat_terms;
      flat_terms.reserve(shared_flat_terms.size());
      std::unordered_map<theory::equality::FlatTermId, core::TermId> term_by_flat;
      term_by_flat.reserve(shared_flat_terms.size());
      for (const auto& [term, flat] : shared_flat_terms) {
        flat_terms.push_back(flat);
        term_by_flat.emplace(flat, term);
      }
      const std::vector<std::vector<theory::equality::FlatTermId>> classes =
          euf.closure.equivalence_classes(flat_terms);
      statistics.increment(":no.shared-model.euf-class-projections");
      statistics.increment(":no.shared-model.euf-class-projection-terms", flat_terms.size());
      for (const std::vector<theory::equality::FlatTermId>& equal_class : classes) {
        if (equal_class.size() < 2) {
          continue;
        }
        const theory::equality::FlatTermId representative = equal_class.front();
        for (std::size_t index = 1; index < equal_class.size(); ++index) {
          const core::TermId lhs = term_by_flat.at(representative);
          const core::TermId rhs = term_by_flat.at(equal_class[index]);
          const core::TermId equality = terms.mk_eq(lhs, rhs);
          ++result.euf_consequences;
          manager.mark_interface_equality_classification(
              equality, InterfaceEqualityClassification::EufImplied);
          if (manager.lra_model_agrees_equal(lhs, rhs)) {
            if (manager.record_internal_model_equality(lhs, rhs)) {
              ++result.mergeable_skipped;
            }
            continue;
          }
          const std::optional<std::vector<sat::Lit>> reason =
              euf.closure.explain_equal(representative, equal_class[index]);
          if (!reason.has_value()) {
            statistics.increment(":no.interface.euf-explanation-missing");
          }
          if (manager.lra_model_terms_mapped(lhs, rhs)) {
            ++result.model_disagreements;
          } else {
            ++result.unmapped_consequences;
          }
          ++result.mergeable_forced;
          append_unique_forced_equality(ExplainedInterfaceEquality{equality, std::move(reason)},
                                        lhs, rhs);
        }
      }
    }
  } else {
    euf.closure.pop(filter_checkpoint);
  }

  statistics.increment(":no.interface.lra-to-euf-equalities-euf-already-true", result.already_true);
  statistics.increment(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped",
                       result.mergeable_skipped);
  statistics.increment(":no.interface.lra-to-euf-equalities-euf-mergeable-forced",
                       result.mergeable_forced);
  statistics.increment(":no.interface.lra-to-euf-equalities-euf-forbidden-forced",
                       result.forbidden_forced);
  statistics.increment(":no.interface.lra-to-euf-internal-model-equalities",
                       result.mergeable_skipped);
  if (reconcile_shared_model) {
    statistics.increment(":no.shared-model.internal-euf-merges", result.mergeable_skipped);
    statistics.increment(":no.shared-model.euf-forbidden-equalities", result.forbidden_forced);
    statistics.increment(":no.shared-model.sat-materializations-avoided", result.mergeable_skipped);
    statistics.increment(":no.shared-model.euf-derived-consequences", result.euf_consequences);
    statistics.increment(":no.shared-model.euf-model-agree-internal", result.mergeable_skipped);
    statistics.increment(":no.shared-model.euf-model-disagreement-sat", result.model_disagreements);
    statistics.increment(":no.shared-model.euf-unmapped-sat", result.unmapped_consequences);
  }
  statistics.set(":no.interface.materialized-pairs", manager.summary().equality_registry.size());
  statistics.set(":no.interface.materialized-missing-pairs", manager.summary().missing_equalities);
  return result;
}

bool learn_shared_model_direct_conflict(const LraToEufEqualityFilterResult& result,
                                        sat::IpasirUpBackend& backend,
                                        util::Statistics& statistics) {
  if (!result.direct_conflict.has_value()) {
    return false;
  }
  backend.add_clause(result.direct_conflict.value());
  statistics.increment(kStatTheoryConflicts);
  record_theory_conflict(statistics);
  statistics.increment(":no.interface.combined-direct-conflicts");
  return true;
}

void append_unique_equalities_by_pair(const core::TermManager& terms,
                                      std::vector<core::TermId>& target,
                                      const std::vector<core::TermId>& source) {
  for (const core::TermId equality_id : source) {
    const core::Term& equality = terms.get(equality_id);
    if (equality.kind != core::TermKind::Equality || equality.children.size() != 2) {
      continue;
    }
    if (!equality_list_contains_pair(terms, target, equality.children.at(0),
                                     equality.children.at(1))) {
      target.push_back(equality_id);
    }
  }
}

std::vector<core::TermId> shared_model_internal_equality_terms(
    core::TermManager& terms, const TheoryCombinationManager& manager) {
  std::vector<core::TermId> equalities;
  if (!manager.shared_model().valid) {
    return equalities;
  }
  equalities.reserve(manager.shared_model().internal_equalities.size());
  for (const SharedModelEquality& equality : manager.shared_model().internal_equalities) {
    equalities.push_back(terms.mk_eq(equality.lhs, equality.rhs));
  }
  return equalities;
}

struct RealSeparationCandidate {
  core::TermId lhs;
  core::TermId rhs;
};

constexpr std::size_t kNoModelSeparationBacktrackingCandidateLimit = 18;

enum class RealModelSeparationStatus {
  Success,
  Inconsistent,
  ProbeLimit,
};

struct RealModelSeparationResult {
  RealModelSeparationStatus status = RealModelSeparationStatus::Inconsistent;
  std::vector<core::TermId> atoms;
};

RealModelSeparationStatus search_shared_real_model_separations(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<RealSeparationCandidate>& candidates, std::size_t next_candidate,
    std::vector<core::TermId>& accumulated, std::vector<core::TermId>& separations,
    const CheckSatOptions& options, util::Statistics& statistics, std::uint64_t& branches,
    std::uint64_t& backtracks) {
  if (next_candidate == candidates.size()) {
    return RealModelSeparationStatus::Success;
  }

  const RealSeparationCandidate& candidate = candidates[next_candidate];
  const core::TermId lhs_lt_rhs = terms.mk_lt(candidate.lhs, candidate.rhs);
  const core::TermId rhs_lt_lhs = terms.mk_lt(candidate.rhs, candidate.lhs);

  const auto try_orientation = [&](core::TermId atom) {
    ++branches;
    accumulated.push_back(atom);
    if (!consume_no_extra_atom_probe_budget(options, statistics, accumulated.size(), true)) {
      accumulated.pop_back();
      return RealModelSeparationStatus::ProbeLimit;
    }
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options, &statistics)) {
      separations.push_back(atom);
      const RealModelSeparationStatus status = search_shared_real_model_separations(
          terms, lra_atoms, lra_assignments, candidates, next_candidate + 1, accumulated,
          separations, options, statistics, branches, backtracks);
      if (status == RealModelSeparationStatus::Success) {
        return RealModelSeparationStatus::Success;
      }
      if (status == RealModelSeparationStatus::ProbeLimit) {
        accumulated.pop_back();
        return status;
      }
      separations.pop_back();
    }
    accumulated.pop_back();
    return RealModelSeparationStatus::Inconsistent;
  };

  const RealModelSeparationStatus lhs_status = try_orientation(lhs_lt_rhs);
  if (lhs_status == RealModelSeparationStatus::Success ||
      lhs_status == RealModelSeparationStatus::ProbeLimit) {
    return lhs_status;
  }
  const RealModelSeparationStatus rhs_status = try_orientation(rhs_lt_lhs);
  if (rhs_status == RealModelSeparationStatus::Success ||
      rhs_status == RealModelSeparationStatus::ProbeLimit) {
    return rhs_status;
  }

  ++backtracks;
  return RealModelSeparationStatus::Inconsistent;
}

RealModelSeparationResult greedy_shared_real_model_separation_atoms(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<RealSeparationCandidate>& candidates,
    const std::vector<core::TermId>& forced_equalities, const CheckSatOptions& options,
    util::Statistics& statistics) {
  std::vector<core::TermId> accumulated = forced_equalities;
  std::vector<core::TermId> separations;
  std::optional<RealModelValues> model_values;
  {
    std::vector<core::TermId> care_terms;
    care_terms.reserve(candidates.size() * 2U);
    for (const RealSeparationCandidate& candidate : candidates) {
      care_terms.push_back(candidate.lhs);
      care_terms.push_back(candidate.rhs);
    }
    std::sort(care_terms.begin(), care_terms.end());
    care_terms.erase(std::unique(care_terms.begin(), care_terms.end()), care_terms.end());

    std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
    augmented_lra_atoms.reserve(lra_atoms.size() + forced_equalities.size());
    std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
    augmented_lra_assignments.reserve(lra_assignments.size() + forced_equalities.size());
    for (const core::TermId forced_equality : forced_equalities) {
      augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, forced_equality});
      augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, forced_equality, false});
    }
    const PreparedLraTheory prepared(
        terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
        options.lra_sparse_leaving, lra_fixed_elimination_for_mixed(options), true,
        /*least_violated_leaving=*/false, lra_tuning_from_options(options.lra_tuning));
    theory::arithmetic::LraTableau tableau = prepared.base_tableau;
    bool consistent = true;
    for (std::size_t i = 0; i < augmented_lra_assignments.size(); ++i) {
      const AssignedTheoryAtom& atom = augmented_lra_assignments[i];
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
      if (!prepared_atom.is_arithmetic || atom.filtered) {
        continue;
      }
      const auto conflict = assert_lra_atom_bound(
          tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
          prepared_atom.asserts_lower_when_true, blocking_lit(atom));
      if (conflict.has_value()) {
        consistent = false;
        break;
      }
    }
    if (consistent && !tableau.check().has_value()) {
      model_values =
          real_model_values_from_lra(care_terms, prepared, tableau, augmented_lra_assignments);
    }
  }

  for (const RealSeparationCandidate& candidate : candidates) {
    const core::TermId lhs_lt_rhs = terms.mk_lt(candidate.lhs, candidate.rhs);
    if (model_values.has_value()) {
      const auto lhs_value = model_values->find(candidate.lhs);
      const auto rhs_value = model_values->find(candidate.rhs);
      if (lhs_value != model_values->end() && rhs_value != model_values->end()) {
        if (lhs_value->second < rhs_value->second) {
          accumulated.push_back(lhs_lt_rhs);
          separations.push_back(lhs_lt_rhs);
          statistics.increment(":no.interface.model-separation-model-oriented");
          continue;
        }
        if (rhs_value->second < lhs_value->second) {
          const core::TermId rhs_lt_lhs = terms.mk_lt(candidate.rhs, candidate.lhs);
          accumulated.push_back(rhs_lt_lhs);
          separations.push_back(rhs_lt_lhs);
          statistics.increment(":no.interface.model-separation-model-oriented");
          continue;
        }
      }
    }

    const core::TermId rhs_lt_lhs = terms.mk_lt(candidate.rhs, candidate.lhs);
    accumulated.push_back(lhs_lt_rhs);
    if (!consume_no_extra_atom_probe_budget(options, statistics, accumulated.size(), true)) {
      return RealModelSeparationResult{RealModelSeparationStatus::ProbeLimit, {}};
    }
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options, &statistics)) {
      separations.push_back(lhs_lt_rhs);
      continue;
    }
    accumulated.pop_back();

    accumulated.push_back(rhs_lt_lhs);
    if (!consume_no_extra_atom_probe_budget(options, statistics, accumulated.size(), true)) {
      return RealModelSeparationResult{RealModelSeparationStatus::ProbeLimit, {}};
    }
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options, &statistics)) {
      separations.push_back(rhs_lt_lhs);
      continue;
    }
    return RealModelSeparationResult{RealModelSeparationStatus::Inconsistent, {}};
  }
  return RealModelSeparationResult{RealModelSeparationStatus::Success, std::move(separations)};
}

RealModelSeparationResult shared_real_model_separation_atoms(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const FlatEufModelView& euf,
    const std::vector<core::TermId>& shared_terms,
    const std::vector<core::TermId>& forced_equalities, const CheckSatOptions& options,
    util::Statistics& statistics) {
  std::vector<RealSeparationCandidate> candidates;
  std::vector<core::TermId> accumulated = forced_equalities;
  std::uint64_t euf_mergeable_pairs = 0;
  std::uint64_t euf_forbidden_pairs = 0;
  for (std::size_t i = 0; i < shared_terms.size(); ++i) {
    for (std::size_t j = i + 1; j < shared_terms.size(); ++j) {
      const core::TermId lhs = shared_terms[i];
      const core::TermId rhs = shared_terms[j];
      if (flat_closure_equal_on_terms(euf, lhs, rhs) ||
          equality_list_contains_pair(terms, forced_equalities, lhs, rhs)) {
        continue;
      }
      if (!flat_closure_forbids_equality_on_terms(euf, lhs, rhs)) {
        ++euf_mergeable_pairs;
        continue;
      }
      ++euf_forbidden_pairs;
      candidates.push_back(RealSeparationCandidate{lhs, rhs});
    }
  }

  statistics.set(":no.interface.model-separation-candidates", candidates.size());
  statistics.set(":no.interface.model-separation-euf-mergeable-pairs", euf_mergeable_pairs);
  statistics.set(":no.interface.model-separation-euf-forbidden-pairs", euf_forbidden_pairs);
  statistics.set(":no.interface.model-separation-search-capped", 0);
  statistics.set(":no.interface.model-separation-search-branches", 0);
  statistics.set(":no.interface.model-separation-search-backtracks", 0);
  statistics.set(":no.interface.model-separation-model-oriented", 0);
  if (candidates.size() > kNoModelSeparationBacktrackingCandidateLimit) {
    statistics.set(":no.interface.model-separation-search-capped", 1);
    return greedy_shared_real_model_separation_atoms(terms, lra_atoms, lra_assignments, candidates,
                                                     forced_equalities, options, statistics);
  }

  std::vector<core::TermId> separations;
  std::uint64_t branches = 0;
  std::uint64_t backtracks = 0;
  const RealModelSeparationStatus status = search_shared_real_model_separations(
      terms, lra_atoms, lra_assignments, candidates, 0, accumulated, separations, options,
      statistics, branches, backtracks);
  if (status != RealModelSeparationStatus::Success) {
    statistics.set(":no.interface.model-separation-search-branches", branches);
    statistics.set(":no.interface.model-separation-search-backtracks", backtracks);
    return RealModelSeparationResult{status, {}};
  }
  statistics.set(":no.interface.model-separation-search-branches", branches);
  statistics.set(":no.interface.model-separation-search-backtracks", backtracks);
  return RealModelSeparationResult{RealModelSeparationStatus::Success, std::move(separations)};
}

RealModelSeparationResult targeted_shared_real_model_separation_atoms(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<TermPairKey>& unresolved_pairs, const CheckSatOptions& options,
    util::Statistics& statistics) {
  std::vector<RealSeparationCandidate> candidates;
  candidates.reserve(unresolved_pairs.size());
  for (const TermPairKey& pair : unresolved_pairs) {
    candidates.push_back(RealSeparationCandidate{pair.lhs, pair.rhs});
  }
  statistics.increment(":no.shared-model.targeted-separation-searches");
  statistics.increment(":no.shared-model.targeted-separation-pairs", candidates.size());

  if (candidates.size() > kNoModelSeparationBacktrackingCandidateLimit) {
    statistics.increment(":no.shared-model.targeted-separation-capped");
    return greedy_shared_real_model_separation_atoms(terms, lra_atoms, lra_assignments, candidates,
                                                     {}, options, statistics);
  }

  std::vector<core::TermId> accumulated;
  std::vector<core::TermId> separations;
  std::uint64_t branches = 0;
  std::uint64_t backtracks = 0;
  const RealModelSeparationStatus status = search_shared_real_model_separations(
      terms, lra_atoms, lra_assignments, candidates, 0, accumulated, separations, options,
      statistics, branches, backtracks);
  statistics.increment(":no.shared-model.targeted-separation-branches", branches);
  statistics.increment(":no.shared-model.targeted-separation-backtracks", backtracks);
  if (status != RealModelSeparationStatus::Success) {
    return RealModelSeparationResult{status, {}};
  }
  return RealModelSeparationResult{status, std::move(separations)};
}

struct LraModelState {
  PreparedLraTheory prepared;
  theory::arithmetic::LraTableau tableau;
  std::vector<AssignedTheoryAtom> assignments;
};

std::optional<LraModelState> build_lra_model_state(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& extra_true_atoms, const CheckSatOptions& options) {
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.reserve(lra_atoms.size() + extra_true_atoms.size());
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.reserve(lra_assignments.size() + extra_true_atoms.size());
  for (const core::TermId atom : extra_true_atoms) {
    augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, atom});
    augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, atom, false});
  }

  PreparedLraTheory prepared(
      terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, lra_fixed_elimination_for_mixed(options), true,
      /*least_violated_leaving=*/false, lra_tuning_from_options(options.lra_tuning));
  theory::arithmetic::LraTableau tableau = prepared.base_tableau;
  for (std::size_t i = 0; i < augmented_lra_assignments.size(); ++i) {
    const AssignedTheoryAtom& atom = augmented_lra_assignments[i];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
    if (!prepared_atom.is_arithmetic || atom.filtered) {
      continue;
    }
    const std::optional<sat::Clause> conflict = assert_lra_atom_bound(
        tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
        prepared_atom.asserts_lower_when_true, blocking_lit(atom));
    if (conflict.has_value()) {
      return std::nullopt;
    }
  }
  if (tableau.check().has_value()) {
    return std::nullopt;
  }
  return LraModelState{std::move(prepared), std::move(tableau),
                       std::move(augmented_lra_assignments)};
}

struct ModelEqualityArrangementState {
  explicit ModelEqualityArrangementState(TheoryCombinationManager& combination_manager)
      : manager_(combination_manager) {}

  TheoryCombinationManager& manager_;

  bool has_active_branch() const { return !manager_.shared_equality_decisions().active.empty(); }

  void publish_active_stats(util::Statistics& statistics) const {
    const std::size_t active_count = manager_.shared_equality_decisions().active.size();
    statistics.set(":no.interface.model-equality-active-assumptions", active_count);
    if (active_count > statistics.get(":no.interface.model-equality-max-active-assumptions")) {
      statistics.set(":no.interface.model-equality-max-active-assumptions", active_count);
    }
  }

  void enqueue_pending_branch(sat::Lit lit, util::Statistics& statistics) {
    SharedEqualityDecisionState& decisions = manager_.shared_equality_decisions();
    const int var = sat_var(lit);
    const auto contains_var = [var](const std::vector<sat::Lit>& literals) {
      return std::any_of(literals.begin(), literals.end(),
                         [var](sat::Lit current) { return sat_var(current) == var; });
    };
    if (contains_var(decisions.pending) || contains_var(decisions.active)) {
      statistics.increment(":no.interface.model-equality-duplicate-pending");
      return;
    }
    decisions.pending.push_back(lit);
    statistics.increment(":no.interface.model-equality-pending");
  }

  void activate_pending_branch(util::Statistics& statistics) {
    SharedEqualityDecisionState& decisions = manager_.shared_equality_decisions();
    if (decisions.pending.empty()) {
      return;
    }
    statistics.increment(":no.interface.model-equality-pending-batches");
    statistics.increment(":no.interface.model-equality-pending-batch-lits",
                         decisions.pending.size());
    statistics.set(
        ":no.interface.model-equality-max-pending-batch",
        std::max<std::uint64_t>(statistics.get(":no.interface.model-equality-max-pending-batch"),
                                decisions.pending.size()));
    for (const sat::Lit pending : decisions.pending) {
      const int pending_var = sat_var(pending);
      const bool already_active =
          std::any_of(decisions.active.begin(), decisions.active.end(),
                      [pending_var](sat::Lit lit) { return sat_var(lit) == pending_var; });
      if (already_active) {
        statistics.increment(":no.interface.model-equality-duplicate-pending");
      } else {
        decisions.active.push_back(pending);
        statistics.increment(":no.interface.model-equality-activated");
      }
    }
    decisions.pending.clear();
    publish_active_stats(statistics);
  }

  std::optional<std::uint64_t> assume_active_branches(sat::IpasirUpBackend& backend,
                                                      util::Statistics& statistics) const {
    const std::vector<sat::Lit>& active = manager_.shared_equality_decisions().active;
    if (active.empty()) {
      return std::nullopt;
    }
    const std::uint64_t propagation_start = statistics.get(":no.interface.equalities-propagated");
    for (const sat::Lit lit : active) {
      backend.assume(lit);
    }
    statistics.increment(":no.interface.model-equality-assumption-solve-calls");
    statistics.increment(":no.interface.model-equality-assumptions", active.size());
    publish_active_stats(statistics);
    return propagation_start;
  }

  void record_rejection(std::optional<std::uint64_t> propagation_start,
                        util::Statistics& statistics) {
    SharedEqualityDecisionState& decisions = manager_.shared_equality_decisions();
    if (propagation_start.has_value() &&
        statistics.get(":no.interface.equalities-propagated") <= propagation_start.value()) {
      ++decisions.unproductive_rejections;
    } else {
      decisions.unproductive_rejections = 0;
    }
  }

  void record_theory_conflict(std::optional<std::uint64_t> propagation_start,
                              util::Statistics& statistics) {
    if (!has_active_branch()) {
      return;
    }
    statistics.increment(":no.interface.model-equality-conflicts");
    record_rejection(propagation_start, statistics);
  }

  void learn_failed_assumption_clause(sat::IpasirUpBackend& backend,
                                      std::optional<std::uint64_t> propagation_start,
                                      util::Statistics& statistics) {
    SharedEqualityDecisionState& decisions = manager_.shared_equality_decisions();
    sat::Clause failed_assumption_clause;
    std::vector<sat::Lit> surviving_assumptions;
    surviving_assumptions.reserve(decisions.active.size());
    for (const sat::Lit lit : decisions.active) {
      if (backend.failed(lit)) {
        failed_assumption_clause.push_back(sat::Lit{-lit.value});
      } else {
        surviving_assumptions.push_back(lit);
      }
    }
    if (failed_assumption_clause.empty()) {
      for (const sat::Lit lit : decisions.active) {
        failed_assumption_clause.push_back(sat::Lit{-lit.value});
      }
      surviving_assumptions.clear();
    }
    backend.add_clause(failed_assumption_clause);
    statistics.increment(":no.interface.model-equality-failed-assumptions",
                         failed_assumption_clause.size());
    statistics.increment(":no.interface.model-equality-dropped-assumptions",
                         failed_assumption_clause.size());
    statistics.increment(":no.interface.model-equality-blocking-clauses");
    decisions.active = std::move(surviving_assumptions);
    statistics.set(":no.interface.model-equality-retained-assumptions", decisions.active.size());
    publish_active_stats(statistics);
    record_rejection(propagation_start, statistics);
  }
};

struct ModelEqualityCandidate {
  TermPairKey pair;
  std::size_t original_index = 0;
  int rank = 0;
  bool is_care_pair = false;
  bool both_euf_arguments = false;
};

struct ModelEqualityCandidateQueue {
  std::deque<ModelEqualityCandidate> candidates;

  void rebuild_from_care_pairs(const std::vector<TermPairKey>& care_pairs, std::size_t limit,
                               util::Statistics& statistics) {
    candidates.clear();
    statistics.increment(":no.interface.model-equality-queue-builds");
    for (std::size_t index = 0; index < care_pairs.size() && candidates.size() < limit; ++index) {
      candidates.push_back(ModelEqualityCandidate{care_pairs[index], index, 0, true, true});
    }
    statistics.increment(":no.interface.model-equality-queued", candidates.size());
    if (candidates.empty()) {
      statistics.increment(":no.interface.model-equality-queue-empty");
    }
  }

  std::optional<ModelEqualityCandidate> pop(util::Statistics& statistics) {
    if (candidates.empty()) {
      statistics.increment(":no.interface.model-equality-queue-empty");
      return std::nullopt;
    }
    ModelEqualityCandidate candidate = candidates.front();
    candidates.pop_front();
    statistics.increment(":no.interface.model-equality-queue-pops");
    return candidate;
  }
};

struct ModelEqualityBranchChoice {
  std::optional<sat::Lit> pending_branch;
};

std::optional<sat::Lit> materialize_shared_model_split(
    core::TermManager& terms, BooleanClausifier& clausifier, sat::IpasirUpBackend& backend,
    TheoryCombinationManager& manager, const TermPairKey& pair, util::Statistics& statistics) {
  const core::TermId equality = terms.mk_eq(pair.lhs, pair.rhs);
  std::optional<sat::Lit> lit = manager.interface_equality_lit(equality);
  bool created = false;
  if (!lit.has_value()) {
    lit = create_interface_equality_lit(terms, clausifier, manager, equality);
    created = lit.has_value();
  }
  if (!lit.has_value()) {
    statistics.increment(":no.shared-model.split-materialization-failures");
    return std::nullopt;
  }

  const int var = sat_var(lit.value());
  SharedEqualityDecisionState& decisions = manager.shared_equality_decisions();
  if (decisions.tried_vars.contains(var)) {
    statistics.increment(":no.shared-model.split-tried");
    return std::nullopt;
  }
  // Adding a fresh atom invalidates CaDiCaL's satisfied state; only query the
  // current model when the literal existed before this callback.
  if (!created && lit_value_in_backend(backend, lit.value()).has_value()) {
    statistics.increment(":no.shared-model.split-assigned");
    return std::nullopt;
  }

  decisions.tried_vars.insert(var);
  backend.phase(lit.value());
  statistics.increment(":no.shared-model.split-materializations");
  statistics.increment(":no.interface.model-equality-created", created ? 1 : 0);
  statistics.set(":no.interface.sat-equality-atoms",
                 registered_interface_equality_atom_count(manager));
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  return lit;
}

ModelEqualityBranchChoice choose_lra_model_equality_branch(
    core::TermManager& terms, BooleanClausifier& clausifier, sat::IpasirUpBackend& backend,
    TheoryCombinationManager& manager, const LraModelEqualPartition& partition,
    std::size_t full_pair_count, const EufCarePairDiagnostics& euf_care,
    const CheckSatOptions& options, ModelEqualityCandidateQueue& candidate_queue,
    util::Statistics& statistics) {
  SharedEqualityDecisionState& branch_state = manager.shared_equality_decisions();
  if (!options.nelson_oppen_model_equality_branching || partition.candidate_pair_count == 0 ||
      branch_state.tried_vars.size() >= options.nelson_oppen_model_equality_branch_budget) {
    return {};
  }
  if (options.nelson_oppen_model_equality_branch_policy == ModelEqualityBranchPolicy::Adaptive &&
      branch_state.unproductive_rejections >= 4) {
    if (!branch_state.adaptive_stopped) {
      statistics.increment(":no.interface.model-equality-adaptive-stops");
      branch_state.adaptive_stopped = true;
    }
    return {};
  }

  const bool care_queued_policy =
      options.nelson_oppen_model_equality_branch_policy == ModelEqualityBranchPolicy::CareQueued;
  const std::vector<TermPairKey> care_candidate_pairs =
      filter_lra_model_candidate_pairs(euf_care.term_pairs, partition, statistics);
  statistics.set(":no.interface.model-equality-full-pairs", full_pair_count);
  statistics.set(":no.interface.model-equality-filtered-pairs", partition.candidate_pair_count);
  if (options.nelson_oppen_model_equality_branch_policy == ModelEqualityBranchPolicy::First) {
    statistics.set(":no.interface.model-equality-care-pairs", partition.candidate_pair_count);
    statistics.set(":no.interface.model-equality-care-pruned-pairs",
                   full_pair_count - partition.candidate_pair_count);
  } else {
    statistics.set(":no.interface.model-equality-care-pairs", care_candidate_pairs.size());
    statistics.set(":no.interface.model-equality-care-pruned-pairs",
                   full_pair_count - care_candidate_pairs.size());
  }

  if (care_queued_policy) {
    const std::size_t remaining_budget =
        options.nelson_oppen_model_equality_branch_budget - branch_state.tried_vars.size();
    candidate_queue.rebuild_from_care_pairs(care_candidate_pairs, remaining_budget, statistics);
    if (candidate_queue.candidates.empty()) {
      return {};
    }
  }

  const auto try_candidate =
      [&](const ModelEqualityCandidate& candidate) -> std::optional<sat::Lit> {
    const TermPairKey& pair = candidate.pair;
    statistics.increment(":no.interface.model-equality-scanned");
    const core::TermId lhs = pair.lhs;
    const core::TermId rhs = pair.rhs;
    std::optional<core::TermId> equality;
    std::optional<sat::Lit> lit;
    if (care_queued_policy) {
      equality = terms.mk_eq(lhs, rhs);
      lit = manager.interface_equality_lit(equality.value());
    }
    statistics.increment(":no.interface.model-equality-lra-equal");
    statistics.increment(":no.interface.model-equality-candidates");
    if (!equality.has_value()) {
      equality = terms.mk_eq(lhs, rhs);
    }
    if (theory_atom_occurs_in_formula(clausifier, equality.value())) {
      return std::nullopt;
    }

    if (!lit.has_value()) {
      lit = manager.interface_equality_lit(equality.value());
    }
    if (lit.has_value()) {
      const int existing_var = sat_var(lit.value());
      if (branch_state.tried_vars.contains(existing_var)) {
        statistics.increment(":no.interface.model-equality-skipped-tried");
        if (care_queued_policy) {
          statistics.increment(":no.interface.model-equality-queue-skipped-tried");
        }
        return std::nullopt;
      }
      const std::optional<bool> value = lit_value_in_backend(backend, lit.value());
      if (value.has_value()) {
        statistics.increment(":no.interface.model-equality-skipped-assigned");
        if (care_queued_policy) {
          statistics.increment(":no.interface.model-equality-queue-skipped-assigned");
        }
        return std::nullopt;
      }
    }
    bool created = false;
    if (!lit.has_value()) {
      lit = create_interface_equality_lit(terms, clausifier, manager, equality.value());
      created = lit.has_value();
    }
    if (!lit.has_value()) {
      return std::nullopt;
    }
    const int var = sat_var(lit.value());
    branch_state.tried_vars.insert(var);
    backend.phase(lit.value());
    statistics.increment(":no.interface.model-equality-created", created ? 1 : 0);
    if (candidate.is_care_pair) {
      statistics.increment(":no.interface.model-equality-selected-care");
    } else {
      statistics.increment(":no.interface.model-equality-selected-full");
    }
    statistics.set(":no.interface.sat-equality-atoms",
                   registered_interface_equality_atom_count(manager));
    statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
    return lit;
  };

  if (care_queued_policy) {
    while (const std::optional<ModelEqualityCandidate> candidate =
               candidate_queue.pop(statistics)) {
      if (const std::optional<sat::Lit> lit = try_candidate(candidate.value()); lit.has_value()) {
        return ModelEqualityBranchChoice{.pending_branch = lit};
      }
    }
    return {};
  }

  std::size_t candidate_index = 0;
  const auto try_new_pair = [&](const TermPairKey& pair, int rank, bool is_care_pair,
                                bool both_euf_arguments) {
    return try_candidate(
        ModelEqualityCandidate{pair, candidate_index++, rank, is_care_pair, both_euf_arguments});
  };

  const bool first_policy =
      options.nelson_oppen_model_equality_branch_policy == ModelEqualityBranchPolicy::First;
  std::unordered_set<TermPairKey, TermPairKeyHash> care_pair_set;
  std::unordered_set<core::TermId> euf_argument_terms;
  if (!first_policy) {
    care_pair_set.insert(care_candidate_pairs.begin(), care_candidate_pairs.end());
    euf_argument_terms.insert(euf_care.argument_terms.begin(), euf_care.argument_terms.end());
    for (const TermPairKey& pair : care_candidate_pairs) {
      if (const std::optional<sat::Lit> lit = try_new_pair(pair, 0, true, true); lit.has_value()) {
        return ModelEqualityBranchChoice{.pending_branch = lit};
      }
    }
    for (std::size_t lhs = 0; lhs < euf_care.argument_terms.size(); ++lhs) {
      for (std::size_t rhs = lhs + 1; rhs < euf_care.argument_terms.size(); ++rhs) {
        const TermPairKey pair =
            canonical_term_pair(euf_care.argument_terms[lhs], euf_care.argument_terms[rhs]);
        if (care_pair_set.contains(pair) || !partition.may_be_equal(pair.lhs, pair.rhs)) {
          continue;
        }
        if (const std::optional<sat::Lit> lit = try_new_pair(pair, 1, false, true);
            lit.has_value()) {
          return ModelEqualityBranchChoice{.pending_branch = lit};
        }
      }
    }
  }

  LraModelPairCursor cursor(partition);
  while (const std::optional<TermPairKey> pair = cursor.next()) {
    statistics.increment(":no.interface.lra-model-filter-physical-pairs");
    if (!first_policy &&
        (care_pair_set.contains(pair.value()) ||
         (euf_argument_terms.contains(pair->lhs) && euf_argument_terms.contains(pair->rhs)))) {
      continue;
    }
    if (const std::optional<sat::Lit> lit = try_new_pair(pair.value(), 2, false, false);
        lit.has_value()) {
      return ModelEqualityBranchChoice{.pending_branch = lit};
    }
  }
  return {};
}

struct MixedTheoryPurificationResult {
  std::vector<core::TermId> assertions;
  // (application, proxy) for every arithmetic-sorted UF application that was
  // labelled, for both the original term and its purified rewriting. This is
  // the map model construction needs; see PurificationProxyConversion.
  std::vector<std::pair<core::TermId, core::TermId>> application_proxies;
  std::size_t arithmetic_uf_definitions = 0;
  std::size_t euf_real_term_definitions = 0;
  std::size_t reused_applications = 0;
  std::size_t reused_real_terms = 0;
  std::size_t memoized_formula_rewrites = 0;
  std::size_t memoized_arithmetic_rewrites = 0;
  std::size_t memoized_euf_rewrites = 0;
  bool skipped_no_sites = false;
};

bool arithmetic_term_needs_mixed_purification(
    const core::TermManager& terms, core::TermId term_id,
    std::unordered_map<core::TermId, bool>& arithmetic_memo);
bool euf_term_needs_mixed_purification(const core::TermManager& terms, core::TermId term_id,
                                       std::unordered_map<core::TermId, bool>& arithmetic_memo,
                                       std::unordered_map<core::TermId, bool>& euf_memo);

bool arithmetic_term_needs_mixed_purification(
    const core::TermManager& terms, core::TermId term_id,
    std::unordered_map<core::TermId, bool>& arithmetic_memo) {
  const auto found = arithmetic_memo.find(term_id);
  if (found != arithmetic_memo.end()) {
    return found->second;
  }
  const core::Term& term = terms.get(term_id);
  const bool arithmetic_sort =
      term.sort == terms.symbols().real_sort() || term.sort == terms.symbols().int_sort();
  bool result = term.kind == core::TermKind::Application && arithmetic_sort;
  if (!result && (term.kind == core::TermKind::Plus || term.kind == core::TermKind::Times)) {
    for (const core::TermId child : term.children) {
      if (arithmetic_term_needs_mixed_purification(terms, child, arithmetic_memo)) {
        result = true;
        break;
      }
    }
  }
  arithmetic_memo.emplace(term_id, result);
  return result;
}

bool euf_term_needs_mixed_purification(const core::TermManager& terms, core::TermId term_id,
                                       std::unordered_map<core::TermId, bool>& arithmetic_memo,
                                       std::unordered_map<core::TermId, bool>& euf_memo) {
  const auto found = euf_memo.find(term_id);
  if (found != euf_memo.end()) {
    return found->second;
  }
  const core::Term& term = terms.get(term_id);
  bool result = false;
  if (term.sort == terms.symbols().real_sort() || term.sort == terms.symbols().int_sort()) {
    result = term.kind != core::TermKind::Constant ||
             arithmetic_term_needs_mixed_purification(terms, term_id, arithmetic_memo);
  } else if (term.kind == core::TermKind::Application) {
    for (const core::TermId child : term.children) {
      if (euf_term_needs_mixed_purification(terms, child, arithmetic_memo, euf_memo)) {
        result = true;
        break;
      }
    }
  }
  euf_memo.emplace(term_id, result);
  return result;
}

bool formula_needs_mixed_purification(const core::TermManager& terms, core::TermId term_id,
                                      std::unordered_map<core::TermId, bool>& formula_memo,
                                      std::unordered_map<core::TermId, bool>& arithmetic_memo,
                                      std::unordered_map<core::TermId, bool>& euf_memo) {
  const auto found = formula_memo.find(term_id);
  if (found != formula_memo.end()) {
    return found->second;
  }
  const core::Term& term = terms.get(term_id);
  const std::vector<core::TermId>& children = term.children;
  bool result = false;
  switch (term.kind) {
    case core::TermKind::Le:
    // Lowered by the preprocessing leaf before this runs.
    case core::TermKind::Ite:
    case core::TermKind::IntDiv:
    case core::TermKind::IntMod:
    case core::TermKind::Abs:
    case core::TermKind::Lt:
      result = arithmetic_term_needs_mixed_purification(terms, children.at(0), arithmetic_memo) ||
               arithmetic_term_needs_mixed_purification(terms, children.at(1), arithmetic_memo);
      break;
    case core::TermKind::Equality: {
      const core::SortId equality_sort = terms.get(children.at(0)).sort;
      if (equality_sort == terms.symbols().real_sort() ||
          equality_sort == terms.symbols().int_sort()) {
        result = arithmetic_term_needs_mixed_purification(terms, children.at(0), arithmetic_memo) ||
                 arithmetic_term_needs_mixed_purification(terms, children.at(1), arithmetic_memo);
      } else {
        result =
            euf_term_needs_mixed_purification(terms, children.at(0), arithmetic_memo, euf_memo) ||
            euf_term_needs_mixed_purification(terms, children.at(1), arithmetic_memo, euf_memo);
      }
      break;
    }
    case core::TermKind::Application:
      if (term.sort == terms.symbols().bool_sort()) {
        result = euf_term_needs_mixed_purification(terms, term_id, arithmetic_memo, euf_memo);
      }
      break;
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      for (const core::TermId child : children) {
        if (formula_needs_mixed_purification(terms, child, formula_memo, arithmetic_memo,
                                             euf_memo)) {
          result = true;
          break;
        }
      }
      break;
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Rational:
    case core::TermKind::Plus:
    case core::TermKind::Times:
    // Quantified input never takes a mixed route; opaque if it ever did.
    case core::TermKind::BoundVariable:
    case core::TermKind::Forall:
    case core::TermKind::Exists:
    case core::TermKind::Pattern:
      break;
  }
  formula_memo.emplace(term_id, result);
  return result;
}

bool assertions_need_mixed_purification(const core::TermManager& terms,
                                        const std::vector<core::TermId>& assertions) {
  std::unordered_map<core::TermId, bool> formula_memo;
  std::unordered_map<core::TermId, bool> arithmetic_memo;
  std::unordered_map<core::TermId, bool> euf_memo;
  for (const core::TermId assertion : assertions) {
    if (formula_needs_mixed_purification(terms, assertion, formula_memo, arithmetic_memo,
                                         euf_memo)) {
      return true;
    }
  }
  return false;
}

struct MixedPurificationRewriteState {
  std::unordered_map<core::TermId, core::TermId> proxy_by_app;
  std::unordered_map<core::TermId, core::TermId> proxy_by_real_term;
  std::unordered_map<core::TermId, core::TermId> formula_rewrite_memo;
  std::unordered_map<core::TermId, core::TermId> arithmetic_rewrite_memo;
  std::unordered_map<core::TermId, core::TermId> euf_rewrite_memo;
  std::vector<core::TermId> app_definitions;
  std::vector<std::pair<core::TermId, core::TermId>> application_proxies;
  std::vector<core::TermId> lra_definition_atoms;
  std::size_t reused_applications = 0;
  std::size_t reused_real_terms = 0;
  std::size_t memoized_formula_rewrites = 0;
  std::size_t memoized_arithmetic_rewrites = 0;
  std::size_t memoized_euf_rewrites = 0;
};

// Mixed-theory purification needs two things from a session: the term arena
// and the ability to introduce a fresh proxy constant. Passing that pair
// rather than the Frontend keeps the tactic built on top of it independent of
// the session, so it can be carried inside an immutable Strategy.
struct PurificationScope {
  core::TermManager& terms;
  const TacticContext::FreshConstantFactory& fresh_constant;
};

core::TermId purify_mixed_formula(const PurificationScope& scope, core::TermId term_id,
                                  MixedPurificationRewriteState& state);

core::TermId purify_euf_term(const PurificationScope& scope, core::TermId term_id,
                             MixedPurificationRewriteState& state);

core::TermId purify_arithmetic_term(const PurificationScope& scope, core::TermId term_id,
                                    MixedPurificationRewriteState& state) {
  const auto memoized = state.arithmetic_rewrite_memo.find(term_id);
  if (memoized != state.arithmetic_rewrite_memo.end()) {
    ++state.memoized_arithmetic_rewrites;
    return memoized->second;
  }
  core::TermManager& terms = scope.terms;
  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const core::SortId sort = term.sort;
  const core::SymbolId symbol = term.symbol;
  const util::Rational rational_value = term.rational_value;
  core::TermId result = term_id;
  if (kind == core::TermKind::Application &&
      (sort == terms.symbols().real_sort() || sort == terms.symbols().int_sort())) {
    const auto found = state.proxy_by_app.find(term_id);
    if (found != state.proxy_by_app.end()) {
      ++state.reused_applications;
      result = found->second;
      state.arithmetic_rewrite_memo.emplace(term_id, result);
      return result;
    }
    const std::vector<core::TermId> children = term.children;
    const core::TermId proxy = scope.fresh_constant(sort, "no_proxy");
    state.proxy_by_app.emplace(term_id, proxy);
    std::vector<core::TermId> purified_children;
    purified_children.reserve(children.size());
    bool changed = false;
    for (const core::TermId child : children) {
      const core::TermId purified = purify_euf_term(scope, child, state);
      changed = changed || purified != child;
      purified_children.push_back(purified);
    }
    const core::TermId purified_application =
        changed ? terms.mk_app(symbol, std::move(purified_children)) : term_id;
    state.app_definitions.push_back(terms.mk_eq(proxy, purified_application));
    state.application_proxies.emplace_back(term_id, proxy);
    if (purified_application != term_id) {
      state.application_proxies.emplace_back(purified_application, proxy);
    }
    result = proxy;
    state.arithmetic_rewrite_memo.emplace(term_id, result);
    return result;
  }

  switch (kind) {
    case core::TermKind::Plus: {
      const std::vector<core::TermId> children = term.children;
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      bool changed = false;
      for (const core::TermId child : children) {
        const core::TermId rewritten = purify_arithmetic_term(scope, child, state);
        changed = changed || rewritten != child;
        rewritten_children.push_back(rewritten);
      }
      result = changed ? terms.mk_plus(std::move(rewritten_children)) : term_id;
      break;
    }
    case core::TermKind::Times: {
      const core::TermId original_child = term.children.at(0);
      const core::TermId child = purify_arithmetic_term(scope, original_child, state);
      result = child == original_child ? term_id : terms.mk_times(rational_value, child);
      break;
    }
    default:
      break;
  }
  state.arithmetic_rewrite_memo.emplace(term_id, result);
  return result;
}

core::TermId purify_euf_arithmetic_argument(const PurificationScope& scope, core::TermId term_id,
                                            MixedPurificationRewriteState& state) {
  core::TermManager& terms = scope.terms;
  const core::Term& term = terms.get(term_id);
  if (term.sort != terms.symbols().real_sort() && term.sort != terms.symbols().int_sort()) {
    return term_id;
  }

  const core::TermId arithmetic_term = purify_arithmetic_term(scope, term_id, state);
  const core::Term& rewritten = terms.get(arithmetic_term);
  if (rewritten.kind == core::TermKind::Constant) {
    return arithmetic_term;
  }

  const auto found = state.proxy_by_real_term.find(arithmetic_term);
  if (found != state.proxy_by_real_term.end()) {
    ++state.reused_real_terms;
    return found->second;
  }
  const core::TermId proxy = scope.fresh_constant(rewritten.sort, "no_real_arg");
  state.proxy_by_real_term.emplace(arithmetic_term, proxy);
  // The definition goes to LIA as two bounds, which means EUF never learns it.
  // That is fine for solving and not for model construction: without it the
  // congruence closure cannot see that f(t) and f(proxy) are the same
  // application, so whichever of the two carries the solver's value, the other
  // is left unvalued. Record the pair so model construction can restore the
  // equality the bounds took ownership of.
  state.application_proxies.emplace_back(arithmetic_term, proxy);
  if (term_id != arithmetic_term) {
    // Both spellings, as for applications: the arena keeps the pre-purification
    // term too, and it is the one an earlier substitution may have captured.
    state.application_proxies.emplace_back(term_id, proxy);
  }
  state.lra_definition_atoms.push_back(terms.mk_le(proxy, arithmetic_term));
  state.lra_definition_atoms.push_back(terms.mk_le(arithmetic_term, proxy));
  return proxy;
}

core::TermId purify_euf_term(const PurificationScope& scope, core::TermId term_id,
                             MixedPurificationRewriteState& state) {
  const auto memoized = state.euf_rewrite_memo.find(term_id);
  if (memoized != state.euf_rewrite_memo.end()) {
    ++state.memoized_euf_rewrites;
    return memoized->second;
  }
  core::TermManager& terms = scope.terms;
  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const core::SortId sort = term.sort;
  const core::SymbolId symbol = term.symbol;
  core::TermId result = term_id;
  if (sort == terms.symbols().real_sort() || sort == terms.symbols().int_sort()) {
    result = purify_euf_arithmetic_argument(scope, term_id, state);
    state.euf_rewrite_memo.emplace(term_id, result);
    return result;
  }

  if (kind != core::TermKind::Application) {
    state.euf_rewrite_memo.emplace(term_id, result);
    return result;
  }

  const std::vector<core::TermId> term_children = term.children;
  std::vector<core::TermId> children;
  children.reserve(term_children.size());
  bool changed = false;
  for (const core::TermId child : term_children) {
    const core::TermId rewritten = purify_euf_term(scope, child, state);
    changed = changed || rewritten != child;
    children.push_back(rewritten);
  }
  result = changed ? terms.mk_app(symbol, std::move(children)) : term_id;
  state.euf_rewrite_memo.emplace(term_id, result);
  return result;
}

core::TermId purify_mixed_formula(const PurificationScope& scope, core::TermId term_id,
                                  MixedPurificationRewriteState& state) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  core::TermManager& terms = scope.terms;
  std::vector<Visit> pending{{term_id, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (state.formula_rewrite_memo.contains(visit.term)) {
      ++state.memoized_formula_rewrites;
      continue;
    }

    const core::Term& term = terms.get(visit.term);
    const core::TermKind kind = term.kind;
    const bool rewrites_formula_children =
        kind == core::TermKind::Not || kind == core::TermKind::And || kind == core::TermKind::Or ||
        kind == core::TermKind::Implies;
    if (!visit.expanded && rewrites_formula_children) {
      pending.push_back(Visit{visit.term, true});
      for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
        pending.push_back(Visit{*child, false});
      }
      continue;
    }

    // Copy before purification helpers or TermManager constructors grow term storage.
    const core::SortId sort = term.sort;
    const std::vector<core::TermId> term_children = term.children;
    core::TermId result = visit.term;
    switch (kind) {
      case core::TermKind::Le: {
        const core::TermId lhs = purify_arithmetic_term(scope, term_children.at(0), state);
        const core::TermId rhs = purify_arithmetic_term(scope, term_children.at(1), state);
        result = lhs == term_children.at(0) && rhs == term_children.at(1) ? visit.term
                                                                          : terms.mk_le(lhs, rhs);
        break;
      }
      case core::TermKind::Lt: {
        const core::TermId lhs = purify_arithmetic_term(scope, term_children.at(0), state);
        const core::TermId rhs = purify_arithmetic_term(scope, term_children.at(1), state);
        result = lhs == term_children.at(0) && rhs == term_children.at(1) ? visit.term
                                                                          : terms.mk_lt(lhs, rhs);
        break;
      }
      case core::TermKind::Equality: {
        const core::SortId equality_sort = terms.get(term_children.at(0)).sort;
        if (equality_sort == terms.symbols().real_sort() ||
            equality_sort == terms.symbols().int_sort()) {
          const core::TermId lhs = purify_arithmetic_term(scope, term_children.at(0), state);
          const core::TermId rhs = purify_arithmetic_term(scope, term_children.at(1), state);
          result = lhs == term_children.at(0) && rhs == term_children.at(1) ? visit.term
                                                                            : terms.mk_eq(lhs, rhs);
          break;
        }
      }
        {
          const core::TermId lhs = purify_euf_term(scope, term_children.at(0), state);
          const core::TermId rhs = purify_euf_term(scope, term_children.at(1), state);
          result = lhs == term_children.at(0) && rhs == term_children.at(1) ? visit.term
                                                                            : terms.mk_eq(lhs, rhs);
          break;
        }
      case core::TermKind::Not: {
        const core::TermId child = state.formula_rewrite_memo.at(term_children.at(0));
        result = child == term_children.at(0) ? visit.term : terms.mk_not(child);
        break;
      }
      case core::TermKind::And:
      case core::TermKind::Or: {
        std::vector<core::TermId> children;
        children.reserve(term_children.size());
        bool changed = false;
        for (const core::TermId child : term_children) {
          const core::TermId rewritten = state.formula_rewrite_memo.at(child);
          changed = changed || rewritten != child;
          children.push_back(rewritten);
        }
        result = !changed ? visit.term
                          : (kind == core::TermKind::And ? terms.mk_and(std::move(children))
                                                         : terms.mk_or(std::move(children)));
        break;
      }
      case core::TermKind::Implies: {
        const core::TermId lhs = state.formula_rewrite_memo.at(term_children.at(0));
        const core::TermId rhs = state.formula_rewrite_memo.at(term_children.at(1));
        result = lhs == term_children.at(0) && rhs == term_children.at(1)
                     ? visit.term
                     : terms.mk_implies(lhs, rhs);
        break;
      }
      case core::TermKind::Application:
        if (sort == terms.symbols().bool_sort()) {
          result = purify_euf_term(scope, visit.term, state);
        }
        break;
      // Lowered by the preprocessing leaf before this runs; treated as an
      // opaque leaf if one ever reaches here, never silently descended into.
      case core::TermKind::Ite:
      case core::TermKind::IntDiv:
      case core::TermKind::IntMod:
      case core::TermKind::Abs:
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
      case core::TermKind::BoundVariable:
      case core::TermKind::Forall:
      case core::TermKind::Exists:
      case core::TermKind::Pattern:
        break;
    }
    state.formula_rewrite_memo.emplace(visit.term, result);
  }
  return state.formula_rewrite_memo.at(term_id);
}

MixedTheoryPurificationResult purify_mixed_arithmetic_applications(
    const PurificationScope& scope, const std::vector<core::TermId>& assertions) {
  if (!assertions_need_mixed_purification(scope.terms, assertions)) {
    return MixedTheoryPurificationResult{
        .assertions = assertions, .application_proxies = {}, .skipped_no_sites = true};
  }
  MixedPurificationRewriteState state;
  std::vector<core::TermId> rewritten_assertions;
  rewritten_assertions.reserve(assertions.size());
  state.app_definitions.reserve(assertions.size());
  state.lra_definition_atoms.reserve(assertions.size() * 2);
  for (const core::TermId assertion : assertions) {
    rewritten_assertions.push_back(purify_mixed_formula(scope, assertion, state));
  }
  rewritten_assertions.insert(rewritten_assertions.end(), state.app_definitions.begin(),
                              state.app_definitions.end());
  rewritten_assertions.insert(rewritten_assertions.end(), state.lra_definition_atoms.begin(),
                              state.lra_definition_atoms.end());
  return MixedTheoryPurificationResult{
      .assertions = std::move(rewritten_assertions),
      .application_proxies = std::move(state.application_proxies),
      .arithmetic_uf_definitions = state.app_definitions.size(),
      .euf_real_term_definitions = state.proxy_by_real_term.size(),
      .reused_applications = state.reused_applications,
      .reused_real_terms = state.reused_real_terms,
      .memoized_formula_rewrites = state.memoized_formula_rewrites,
      .memoized_arithmetic_rewrites = state.memoized_arithmetic_rewrites,
      .memoized_euf_rewrites = state.memoized_euf_rewrites,
      .skipped_no_sites = false};
}

std::optional<sat::Clause> check_lra_theory_in_place(const PreparedLraTheory& prepared,
                                                     const std::vector<AssignedTheoryAtom>& atoms,
                                                     theory::arithmetic::LraTableau& tableau) {
  if (atoms.size() != prepared.atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    const AssignedTheoryAtom& atom = atoms[i];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
    if (!prepared_atom.is_arithmetic || atom.filtered) {
      continue;
    }
    const auto conflict = assert_lra_atom_bound(
        tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
        prepared_atom.asserts_lower_when_true, blocking_lit(atom));
    if (conflict.has_value()) {
      return conflict;
    }
  }

  return tableau.check();
}

// Incremental QF_LRA theory check (Dutertre & de Moura Section 4): copies the
// precomputed base tableau and asserts the round's assigned bounds. Returning
// the feasible tableau lets the convex combination final check poll the
// explanation-producing equality rules from de Moura and Bjorner Section 5.1.
LraTheoryCheckResult check_incremental_lra_theory(const PreparedLraTheory& prepared,
                                                  const std::vector<AssignedTheoryAtom>& atoms) {
  theory::arithmetic::LraTableau tableau = prepared.base_tableau;
  std::optional<sat::Clause> conflict = check_lra_theory_in_place(prepared, atoms, tableau);
  return LraTheoryCheckResult{std::move(conflict), std::move(tableau)};
}

std::optional<sat::Clause> find_incremental_lra_theory_conflict(
    const PreparedLraTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms) {
  return check_incremental_lra_theory(prepared, atoms).conflict;
}

// Publishes the prepared tableau's structural shape: how many variables it
// has, how many of them define a row, and how many nonzero coefficients those
// rows hold in total. Structural, not search state -- rows are all added
// before the first push() (see LraTableau's class comment), so this is a
// property of the formula and of how the frontend chose to encode it.
//
// These exist because the tableau's shape was previously invisible: item H1's
// finding that 36-95% of rows on the lagging families were trivial `s = c*x`
// slack rows had to be produced with throwaway instrumentation, and the
// fill-in growth it also measured went unnoticed through eight roadmap items
// for want of exactly this. See docs/qf-lra-heuristics.md.
void publish_lra_tableau_shape_stats(const theory::arithmetic::LraTableau& tableau,
                                     util::Statistics& statistics) {
  std::size_t rows = 0;
  std::size_t nonzeros = 0;
  for (theory::arithmetic::LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    ++rows;
    nonzeros += tableau.row(var).size();
  }
  statistics.set(util::StatKey::LraTableauVars, tableau.variable_count());
  statistics.set(util::StatKey::LraRows, rows);
  statistics.set(util::StatKey::LraRowNonzeros, nonzeros);
}

#if PRIMO_HAVE_IPASIR
// Row-based/interval bound propagation (Dutertre & de Moura Section 4,
// generalized beyond scan_for_propagations' same-slack case): the implied
// upper (want_upper=true) or lower (want_upper=false) bound on `basic_var`
// from its row's terms' CURRENT bounds -- basic_var = sum(coeff_j * var_j),
// so basic_var's maximum is the sum of each term's own maximum contribution
// (var_j's upper bound if coeff_j is positive, since a bigger var_j means a
// bigger term then; var_j's lower bound if coeff_j is negative), and
// symmetrically for the minimum/lower case. Since item H1 asserts a
// single-variable atom's bound directly on the variable (rather than wrapping
// it in a slack), each term's bound is simply tableau.upper_bound/
// lower_bound(var) -- no indirection, and strictly more reaching than the
// previous single_variable_slack lookup, which could only see a term whose
// formula happened to contain a bare coefficient-1 atom over it.
// Returns nullopt if the row exceeds the size cap or a term lacks the
// needed-direction bound (an unbounded term makes the whole sum unbounded in
// that direction, not just weaker). The returned reasons are one asserting
// literal per row term actually used -- a genuine multi-literal antecedent,
// unlike same-variable propagation's single-literal one.
std::optional<std::pair<theory::arithmetic::DeltaRational, std::vector<sat::Lit>>>
compute_implied_bound(const theory::arithmetic::LraTableau& tableau,
                      theory::arithmetic::LraVarId basic_var, bool want_upper,
                      const LraRowPropagationLimits& limits) {
  const auto& row = tableau.row(basic_var);
  if (row.size() > limits.max_row_size) {
    return std::nullopt;
  }
  theory::arithmetic::DeltaRational total =
      theory::arithmetic::DeltaRational::from_rational(util::Rational(0));
  std::vector<sat::Lit> reasons;
  reasons.reserve(row.size());
  for (const auto& [var, coeff, var_slot] : row) {
    const bool need_upper_of_var = want_upper == (coeff.sign() > 0);
    const auto bound = need_upper_of_var ? tableau.upper_bound(var) : tableau.lower_bound(var);
    if (!bound.has_value()) {
      return std::nullopt;
    }
    total = total + bound->value.scaled(coeff);
    reasons.push_back(bound->reason);
  }
  return std::make_pair(total, std::move(reasons));
}

// Competitiveness-roadmap item 7's reverse direction: derive an implied
// bound on one *other* term of basic_var's row (target_var, with
// coefficient target_coeff in that row) from basic_var's own current
// bound plus the row's remaining terms' bounds -- the rearrangement
// `basic_var = target_coeff*target_var + Σ_other coeff_j*var_j`.
//
// Sign rule (verified algebraically, not guessed): treating basic_var as
// a term of coefficient +1 and each *other* row term as a term of
// coefficient -coeff_j (moving them to the other side) maps this onto
// compute_implied_bound's own existing sign convention:
//   want_upper_of_basic = want_upper_of_target == (target_coeff.sign() > 0)
//   need_upper_of_var_j = want_upper_of_basic != (coeff_j.sign() > 0)
// Missing any needed bound (basic_var's own or any other term's) makes this
// nullopt for this round, same all-or-nothing behavior as
// compute_implied_bound.
std::optional<std::pair<theory::arithmetic::DeltaRational, std::vector<sat::Lit>>>
compute_implied_child_bound(const theory::arithmetic::LraTableau& tableau,
                            theory::arithmetic::LraVarId basic_var,
                            theory::arithmetic::LraVarId target_var,
                            const util::Rational& target_coeff, bool want_upper_of_target,
                            const LraRowPropagationLimits& limits) {
  const auto& row = tableau.row(basic_var);
  if (row.size() > limits.bidirectional_max_row_size) {
    return std::nullopt;
  }
  const bool want_upper_of_basic = want_upper_of_target == (target_coeff.sign() > 0);
  const auto basic_bound =
      want_upper_of_basic ? tableau.upper_bound(basic_var) : tableau.lower_bound(basic_var);
  if (!basic_bound.has_value()) {
    return std::nullopt;
  }
  theory::arithmetic::DeltaRational total = basic_bound->value;
  std::vector<sat::Lit> reasons;
  reasons.reserve(row.size());
  reasons.push_back(basic_bound->reason);
  for (const auto& [var, coeff, var_slot] : row) {
    if (var == target_var) {
      continue;
    }
    const bool need_upper_of_var = want_upper_of_basic != (coeff.sign() > 0);
    const auto bound = need_upper_of_var ? tableau.upper_bound(var) : tableau.lower_bound(var);
    if (!bound.has_value()) {
      return std::nullopt;
    }
    total = total - bound->value.scaled(coeff);
    reasons.push_back(bound->reason);
  }
  const util::Rational inv = util::Rational(1) / target_coeff;
  return std::make_pair(total.scaled(inv), std::move(reasons));
}

// Trail-synced CaDiCaL external propagator for QF_LRA (Milestone 3a of the
// QF_LRA plan): mirrors OnlineEufPropagator structurally. cb_propagate()
// delivers same-slack propagations (Milestone 3b, scan_for_propagations)
// and, opt-in, row-based/interval propagations (competitiveness-roadmap
// item 2, scan_row_based_propagations) -- both gated behind
// propagation_enabled_/row_propagation_enabled_ respectively, off entirely
// falls back to conflict-detection-only, mirroring EUF's own
// EufPropagationMode::Conflict line.
//
// Owns its own LraTableau (copied once from PreparedLraTheory's base, not
// per SAT round), kept in sync with the CaDiCaL trail via
// notify_new_decision_level/notify_backtrack calling LraTableau::push()/
// pop() -- this is what actually exercises push/pop for cross-round reuse,
// unlike solve_qf_lra's other path (find_incremental_lra_theory_conflict),
// which copies the base tableau fresh every round.
class OnlineLraPropagator final : public sat::ExternalPropagator {
 public:
  OnlineLraPropagator(const PreparedLraTheory& prepared,
                      const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                      bool propagation_enabled, bool row_propagation_enabled,
                      bool bidirectional_row_propagation_enabled, bool theory_decisions_enabled,
                      bool model_phase_enabled, bool model_phase_backtrack_may_change_beta,
                      const LraRowPropagationLimits& row_propagation_limits,
                      sat::IpasirUpBackend& backend, util::Statistics& statistics,
                      bool count_query_conflicts = false,
                      std::optional<std::size_t> conflict_limit = std::nullopt)
      : theory_atoms_(theory_atoms),
        prepared_(prepared),
        tableau_(prepared.base_tableau),
        propagation_enabled_(propagation_enabled),
        row_propagation_enabled_(row_propagation_enabled),
        bidirectional_row_propagation_enabled_(bidirectional_row_propagation_enabled),
        theory_decisions_enabled_(theory_decisions_enabled),
        model_phase_enabled_(model_phase_enabled),
        model_phase_backtrack_may_change_beta_(model_phase_backtrack_may_change_beta),
        row_propagation_limits_(row_propagation_limits),
        backend_(backend),
        statistics_(statistics),
        count_query_conflicts_(count_query_conflicts),
        conflict_limit_(conflict_limit) {
    int max_theory_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      max_theory_var =
          std::max(max_theory_var, atom.lit.value > 0 ? atom.lit.value : -atom.lit.value);
    }
    observed_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    model_phase_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    model_phase_dirty_bound_vars_.assign(prepared_.base_tableau.variable_count(), 0);
    atom_index_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, theory_atoms_.size());
    for (std::size_t i = 0; i < theory_atoms_.size(); ++i) {
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[i];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      observed_vars_[static_cast<std::size_t>(var)] = 1;
      atom_index_by_var_[static_cast<std::size_t>(var)] = i;
    }
    refresh_model_phases();
  }

  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
  }

  void notify_new_decision_level() override { checkpoints_.push_back(tableau_.push()); }

  void notify_backtrack(std::size_t) override {
    throw std::logic_error("arithmetic backtrack requires the shared removal delta");
  }
  void notify_backtrack_delta(std::size_t new_level, const std::vector<int>& removed) override {
    if (new_level < checkpoints_.size()) {
      tableau_.pop(checkpoints_[new_level]);
      checkpoints_.resize(new_level);
    }
    // Retained propagations still own reasons after unrelated backtracking.
    for (const int literal : removed) {
      reasons_.erase(literal);
      reasons_.erase(-literal);
    }
    rebuild_true_interface_bounds();
    published_interface_pairs_.clear();
    conflict_.reset();
    // Without equality discovery, sound to drop per notify_assignment_lit's
    // verified invariant: a new decision level can never open while a
    // deferred check is still outstanding (cb_has_external_clause -- and
    // thus run_deferred_check_if_needed() -- is always consulted first),
    // so needs_check_ can never be true across a backtrack in a way that
    // would lose an actually-outstanding repair.
    // The experimental producer additionally queries the retained assignment
    // after a backtrack, including after an infeasible repair. Re-establish its
    // feasible-model precondition before using assignments as an equality filter.
    needs_check_ = equality_propagation_enabled_;
    if (equality_propagation_enabled_) {
      statistics_.increment(":lra.equality-propagation.backtrack-repairs");
    }
    external_clause_.clear();
    external_clause_pending_ = false;
    external_clause_index_ = 0;
    // pending_ entries are just candidates, not delivered propagations --
    // safe to drop unconditionally (unlike reasons_ above): at worst a
    // still-valid candidate gets rediscovered next time its slack changes.
    pending_.clear();
    pending_interface_equalities_.clear();
    equality_scan_dirty_ = equality_propagation_enabled_;
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    if (model_phase_backtrack_may_change_beta_) {
      mark_all_model_phases_dirty();
    }
  }

  // Validate the notification contract at a complete model; never backfill it.
  bool cb_check_found_model(const std::vector<int>&) override {
    if (conflict_budget_exhausted()) return true;
    for (const auto& atom : theory_atoms_) {
      if (!is_assigned_var(std::abs(atom.lit.value)))
        throw std::logic_error("arithmetic model contains an unnotified observed atom");
    }
    run_deferred_check_if_needed();
    return !conflict_.has_value();
  }

  // Competitiveness-roadmap item 8: called by CaDiCaL right before it would
  // otherwise make a generic VSIDS decision (Internal::ask_decision(),
  // contrib/src/cadical/src/external_propagate.cpp) -- supplies the next
  // not-yet-decided arithmetic atom's TRUE literal, advancing a persistent
  // rotating cursor across calls. A fixed, deterministic decision ORDER
  // over arithmetic atoms, not a per-candidate cost analysis.
  //
  // An earlier version of this (see git history around commit 17f8faf)
  // ranked candidates by a cheap per-atom infeasibility-cost estimate
  // instead of just taking the next one -- re-measured against this much
  // simpler alternative on the same 9 real-benchmark files (docs/
  // arithmetic.md's Milestone roadmap has the full trail), the cost-ranked
  // version was *worse* than plain VSIDS on 3 of them (solved regressed to
  // timed-out at the same budget) and worse than this round-robin version
  // on 8/9 -- sometimes dramatically (a 96% wall-clock regression on one
  // file). The ranking wasn't just expensive overhead; it was actively
  // steering search into worse territory. This version -- "the theory
  // decides, in a stable order, no per-candidate arithmetic at all" -- is
  // strictly better on this evidence, and the only one of the three that
  // solves the campaign's hardest file (sc-27) at all within a shared
  // budget. Returning 0 defers to VSIDS -- only happens once every
  // arithmetic atom is already decided.
  int cb_decide() override {
    if (conflict_budget_exhausted()) return 0;
    if (!theory_decisions_enabled_ || theory_atoms_.empty()) {
      return 0;
    }
    const std::size_t atom_count = theory_atoms_.size();
    for (std::size_t scanned = 0; scanned < atom_count; ++scanned) {
      const std::size_t atom_index = decide_cursor_;
      decide_cursor_ = (decide_cursor_ + 1) % atom_count;
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      if (!prepared_atom.is_arithmetic) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      statistics_.increment(kStatLraTheoryDecisions);
      return atom.lit.value;
    }
    return 0;
  }

  // Same-slack "deriving satisfied atoms" propagation (Dutertre & de
  // Moura, Section 4.2): pops literals queued by notify_assignment_lit's
  // propagation scan. Never propagates once a conflict is pending this
  // round (mirrors notify_assignment_lit's own early-return), and lazily
  // skips any queued entry whose var got assigned some other way in the
  // meantime (a decision, a different propagation) rather than proactively
  // pruning pending_ on every assignment.
  int cb_propagate() override {
    if (conflict_budget_exhausted()) return 0;
    if (equality_propagation_enabled_) {
      run_deferred_check_if_needed();
      scan_variable_equalities_if_needed();
    }
    if (conflict_.has_value()) {
      return 0;
    }
    while (!pending_.empty()) {
      PendingPropagation candidate = std::move(pending_.front());
      pending_.pop_front();
      const int var = candidate.lit > 0 ? candidate.lit : -candidate.lit;
      if (is_assigned_var(var)) {
        continue;
      }
      const int lit = candidate.lit;
      const bool row_based = candidate.row_based;
      const bool bidirectional = candidate.bidirectional;
      reasons_[lit] = std::move(candidate.reasons);
      statistics_.increment(kStatLraTheoryPropagations);
      if (candidate.variable_equality) {
        statistics_.increment(":lra.equality-propagation.propagations");
      }
      if (row_based) {
        statistics_.increment(kStatLraRowPropagations);
      }
      if (bidirectional) {
        statistics_.increment(kStatLraBidirectionalRowPropagations);
      }
      return lit;
    }
    return 0;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      // Returning 0 here streams an EMPTY reason clause, and CaDiCaL
      // documents that an external clause which is empty after simplification
      // sets `unsat` -- i.e. a missing reason would answer "unsatisfiable".
      // In practice CaDiCaL's own REQUIRE ("external reason clause must
      // contain the propagated literal") aborts first, and contracts are
      // compiled into our build, so this is loud rather than silent. Assert
      // anyway, so the failure names OUR broken invariant -- cb_propagate
      // records reasons_[lit] before returning lit, and notify_backtrack
      // erases only literals it actually unassigns -- rather than CaDiCaL's
      // symptom. Getting that erasure wrong was a real bug once (see
      // notify_backtrack's comment).
      assert(false && "CaDiCaL asked for a reason we never recorded");
      return 0;
    }
    if (reason_stream_active_lit_ != propagated_lit) {
      reason_stream_active_lit_ = propagated_lit;
      reason_stream_index_ = 0;
    }
    if (reason_stream_index_ == 0) {
      ++reason_stream_index_;
      return propagated_lit;
    }
    const std::size_t antecedent_index = static_cast<std::size_t>(reason_stream_index_ - 1);
    if (antecedent_index < found->second.size()) {
      ++reason_stream_index_;
      return found->second[antecedent_index].value;
    }
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    return 0;
  }

  // Authoritative once backend.solve() returns SAT, for the same reason
  // OnlineEufPropagator::incremental_conflict() is: cb_has_external_clause()
  // (which runs the deferred Check/Pivot repair -- see
  // run_deferred_check_if_needed()) is always asked, and any resulting
  // conflict resolved, before CaDiCaL's own main loop can reach a
  // decision or declare a model satisfied (see notify_assignment_lit's
  // comment for the verified invariant), so there is nothing left to
  // recompute once solve() actually returns SAT.
  std::optional<sat::Clause> conflict() const { return conflict_; }
  const theory::arithmetic::LraTableau& tableau() const { return tableau_; }

  // Experimental pure-QF_LRA consumer of Bjørner--Nachmanson Section 3.2.
  // Index only atoms already present; never manufacture an all-pairs frontier.
  void enable_variable_equality_propagation() {
    for (const auto& [polynomial, slack] : prepared_.slack_by_polynomial_) {
      const auto& terms = polynomial.sorted_terms;
      if (terms.size() != 2 || terms[0].second != util::Rational(1) ||
          terms[1].second != util::Rational(-1)) {
        continue;
      }
      const auto found = prepared_.atoms_by_bound_var.find(slack);
      if (found == prepared_.atoms_by_bound_var.end()) continue;
      for (const std::size_t index : found->second) {
        if (prepared_.atoms[index].relation == theory::arithmetic::LraRelation::Eq) continue;
        equality_targets_.push_back(
            {prepared_.var_ids_.at(terms[0].first), prepared_.var_ids_.at(terms[1].first), index});
      }
    }
    std::sort(equality_targets_.begin(), equality_targets_.end(),
              [](const EqualityTarget& a, const EqualityTarget& b) {
                return a.atom_index < b.atom_index;
              });
    statistics_.set(":lra.equality-propagation.targets", equality_targets_.size());
    if (equality_targets_.empty()) return;
    equality_propagation_enabled_ = true;
    for (const auto& [term, variable] : prepared_.var_ids_) {
      equality_variables_.push_back(variable);
    }
    std::sort(equality_variables_.begin(), equality_variables_.end());
    equality_scan_dirty_ = true;
  }

  // Interface-equality publishing is Nelson-Oppen machinery, and only
  // MixedTheoryPropagationDispatcher ever drains it (see
  // take_interface_equality_propagations). On the pure QF_LRA path nothing
  // consumes it, yet it was maintained anyway: observe_true_interface_bound
  // ran on every true assignment, and rebuild_true_interface_bounds() rescanned
  // EVERY theory atom on EVERY backtrack.
  //
  // That was the LassoRanker half of the division regression. On
  // dropbuf.t2.c_Iteration3_Loop_4-pieceTemplate a gprof profile put 62% of
  // runtime in notify_backtrack and 29% in 1.4 BILLION lookups into
  // true_lra_le_reasons_ -- about 150k lookups per backtrack, all discarded.
  // Gating it recovers the file: 200s timeout -> 12.2s, with the pivot and
  // conflict counts identical to the pre-regression commit. See
  // docs/nelson-oppen-qf-lra-regression.md.
  //
  // Defaults to false so the pure path pays nothing; the dispatcher turns it
  // on, which is precisely the path that consumes it.
  void set_interface_equalities_enabled(bool enabled) { interface_equalities_enabled_ = enabled; }

  struct InterfaceEqualityPropagation {
    TermPairKey pair;
    std::vector<sat::Lit> reasons;
  };

  std::vector<InterfaceEqualityPropagation> take_interface_equality_propagations() {
    std::vector<InterfaceEqualityPropagation> result;
    result.reserve(pending_interface_equalities_.size());
    while (!pending_interface_equalities_.empty()) {
      result.push_back(std::move(pending_interface_equalities_.front()));
      pending_interface_equalities_.pop_front();
    }
    return result;
  }

  bool cb_has_external_clause(bool& is_forgettable) override {
    is_forgettable = false;
    if (external_clause_pending_) return true;
    if (conflict_budget_exhausted()) return false;
    run_deferred_check_if_needed();
    scan_variable_equalities_if_needed();
    refresh_model_phases_if_ready();
    if (!conflict_.has_value()) return false;
    external_clause_ = conflict_.value();
    external_clause_index_ = 0;
    external_clause_pending_ = true;
    record_theory_conflict(statistics_);
    return true;
  }

  bool cb_has_external_clause() override {
    bool forgettable = false;
    return cb_has_external_clause(forgettable);
  }

  int cb_add_external_clause_lit() override {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_pending_ = false;
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  bool conflict_budget_exhausted() const {
    return theory_conflict_limit_reached(statistics_, conflict_limit_);
  }

  void record_query_conflict() {
    if (count_query_conflicts_) {
      statistics_.increment(kStatTheoryConflicts);
      statistics_.increment(kStatLraTheoryConflicts);
    }
  }

  bool is_observed_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < observed_vars_.size() &&
           observed_vars_[static_cast<std::size_t>(var)] != 0;
  }

  bool is_assigned_var(int var) const {
    return is_observed_var(var) && assignment_view().literal(var) != 0;
  }

  void publish_elimination_stats() {
    statistics_.set(util::StatKey::LraFixedVariables, tableau_.fixed_variable_count());
    statistics_.set(util::StatKey::LraEliminationPivots, tableau_.elimination_pivot_count());
  }

  void notify_assignment_lit(int lit) {
    if (conflict_.has_value()) {
      // Already found a conflict this round; CaDiCaL hasn't pulled it via
      // cb_has_external_clause yet, so further asserts would just be
      // wasted (and potentially confusing, since assert_lra_atom_bound
      // isn't meant to be called against an already-inconsistent tableau).
      return;
    }
    const int var = lit > 0 ? lit : -lit;
    if (!is_observed_var(var)) {
      return;
    }
    const std::size_t atom_index = atom_index_by_var_[static_cast<std::size_t>(var)];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
    if (!prepared_atom.is_arithmetic) {
      return;
    }
    equality_scan_dirty_ = equality_propagation_enabled_;
    const bool bound_var_was_basic = tableau_.is_basic(prepared_atom.bound_var);
    const theory::arithmetic::DeltaRational bound_var_assignment_before =
        tableau_.assignment(prepared_atom.bound_var);
    const std::size_t elimination_pivots_before = tableau_.elimination_pivot_count();
    const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
    const bool atom_value = atom.lit.value > 0 ? lit > 0 : lit < 0;
    const AssignedTheoryAtom assigned =
        assigned_theory_atom(PureTheoryFilterMode::Off, atom, atom_value);
    const sat::Lit reason = blocking_lit(assigned);
    const auto direct_conflict = assert_lra_atom_bound(
        tableau_, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom_value,
        prepared_atom.asserts_lower_when_true, reason, prepared_atom.integer_rounding);
    // Item H7's counters move inside assert_upper/assert_lower, not inside
    // check(), so they are published here rather than alongside the pivot
    // counters in run_deferred_check_if_needed(). They are cumulative
    // totals, so set() (not increment()) is what stays correct across the
    // repeated calls -- and this is the only place they can change.
    publish_elimination_stats();
    if (direct_conflict.has_value()) {
      conflict_ = direct_conflict;
      record_query_conflict();
      // needs_check_ may already be true from an earlier, non-conflicting
      // literal in this same batch -- clear it, or run_deferred_check_if_
      // needed() would later overwrite this genuine direct conflict with
      // a stale tableau_.check() result. (The offending bound itself was
      // never actually stored by assert_upper/assert_lower's own direct-
      // conflict branch, so check() alone wouldn't even rediscover it.)
      needs_check_ = false;
      return;
    }
    if (tableau_.elimination_pivot_count() != elimination_pivots_before) {
      mark_all_model_phases_dirty();
    } else if (!bound_var_was_basic &&
               tableau_.assignment(prepared_atom.bound_var) != bound_var_assignment_before) {
      mark_model_phase_bound_var_dirty(prepared_atom.bound_var);
      for (const theory::arithmetic::LraVarId affected_row :
           tableau_.occurs_in(prepared_atom.bound_var)) {
        mark_model_phase_bound_var_dirty(affected_row);
      }
    }
    if (propagation_enabled_) {
      scan_for_propagations(atom_index, prepared_atom.bound_var);
      if (row_propagation_enabled_) {
        scan_row_based_propagations(prepared_atom.bound_var);
      }
    }
    if (atom_value) {
      observe_true_interface_bound(atom_index, reason);
    }
    // Defer the expensive Check/Pivot repair rather than running it
    // eagerly after every single assignment (decisions *and* propagated
    // literals both land here): notify_assignment_lit can be called
    // tens of thousands of times more often than CaDiCaL actually needs
    // a verdict from us. Verified against CaDiCaL's own source
    // (contrib/src/cadical/src/{internal,external_propagate}.cpp): with
    // this propagator in eager mode (is_lazy left at its default
    // `false`), cb_has_external_clause() is asked exactly once per
    // quiescence point (once per decision plus once per conflict/
    // backjump, not once per literal), and cb_propagate() is always
    // fully drained *before* it's asked -- so same-slack/row-based
    // propagation above (which reads only bound state, never the
    // current assignment) is completely unaffected by deferring this.
    // See run_deferred_check_if_needed(), called from both
    // cb_has_external_clause() overloads.
    needs_check_ = true;
  }

  // The actual Check/Pivot repair, run lazily right before CaDiCaL is
  // told whether there's a conflict clause to add -- see
  // notify_assignment_lit's own comment for why this specific hook
  // point is both correct (propagation doesn't depend on it) and the
  // right frequency (once per quiescence point, not once per literal).
  void run_deferred_check_if_needed() {
    if (!needs_check_) {
      return;
    }
    needs_check_ = false;
    // Belt-and-suspenders: notify_assignment_lit's direct-conflict branch
    // already clears needs_check_ itself (see its own comment), but never
    // overwrite an already-discovered conflict with a fresh
    // tableau_.check() result regardless -- tableau_.check() only sees
    // whatever bounds actually made it into the tableau, and a rejected
    // direct-conflict bound never does.
    if (conflict_.has_value()) {
      return;
    }
    const std::size_t fallbacks_before = tableau_.bland_fallback_count();
    const std::size_t multi_row_conflicts_before = tableau_.soi_multi_row_conflict_count();
    const std::size_t minimize_calls_before = tableau_.soi_minimize_call_count();
    const std::size_t minimize_shrunk_before = tableau_.soi_minimize_shrunk_count();
    const std::size_t conflict_lits_before = tableau_.soi_conflict_lit_count();
    const std::size_t pivots_before = tableau_.pivot_count();
    const std::size_t row_updates_before = tableau_.row_update_count();
    const std::size_t term_ops_before = tableau_.row_term_op_count();
    statistics_.increment(kStatLraOnlineCheckCalls);
    conflict_ = tableau_.check();
    if (conflict_) record_query_conflict();
    const std::size_t pivot_delta = tableau_.pivot_count() - pivots_before;
    statistics_.increment(kStatLraBlandFallbacks,
                          tableau_.bland_fallback_count() - fallbacks_before);
    statistics_.increment(kStatLraSoiMultiRowConflicts,
                          tableau_.soi_multi_row_conflict_count() - multi_row_conflicts_before);
    statistics_.increment(kStatLraSoiMinimizeCalls,
                          tableau_.soi_minimize_call_count() - minimize_calls_before);
    statistics_.increment(kStatLraSoiMinimizeShrunk,
                          tableau_.soi_minimize_shrunk_count() - minimize_shrunk_before);
    statistics_.increment(kStatLraSoiConflictLits,
                          tableau_.soi_conflict_lit_count() - conflict_lits_before);
    statistics_.increment(kStatLraPivots, pivot_delta);
    statistics_.increment(kStatLraPivotRowUpdates,
                          tableau_.row_update_count() - row_updates_before);
    statistics_.increment(kStatLraPivotTermOps, tableau_.row_term_op_count() - term_ops_before);
    if (!conflict_.has_value() && pivot_delta != 0) {
      mark_all_model_phases_dirty();
    }
  }

  std::optional<sat::Lit> model_phase_literal(std::size_t atom_index) const {
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
    if (!prepared_atom.is_arithmetic ||
        prepared_atom.relation == theory::arithmetic::LraRelation::Eq) {
      return std::nullopt;
    }
    const BoundAssertion true_assertion = bound_assertion(
        prepared_atom.relation, prepared_atom.rhs, prepared_atom.asserts_lower_when_true,
        /*atom_value=*/true, prepared_atom.integer_rounding);
    const theory::arithmetic::DeltaRational& beta = tableau_.assignment(prepared_atom.bound_var);
    const bool atom_holds =
        true_assertion.is_upper ? beta <= true_assertion.value : beta >= true_assertion.value;
    const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
    return sat::Lit{atom_holds ? atom.lit.value : -atom.lit.value};
  }

  void refresh_model_phases_if_ready() {
    if (conflict_.has_value() || !model_phase_enabled_ ||
        (!model_phase_all_dirty_ && model_phase_dirty_bound_var_list_.empty())) {
      return;
    }
    refresh_model_phases();
  }

  void refresh_model_phases() {
    if (!model_phase_enabled_) {
      return;
    }
    statistics_.increment(kStatLraModelPhaseRefreshes);
    if (model_phase_all_dirty_) {
      for (std::size_t atom_index = 0; atom_index < theory_atoms_.size(); ++atom_index) {
        refresh_model_phase_atom(atom_index);
      }
    } else {
      for (const theory::arithmetic::LraVarId bound_var : model_phase_dirty_bound_var_list_) {
        const auto found = prepared_.atoms_by_bound_var.find(bound_var);
        if (found == prepared_.atoms_by_bound_var.end()) {
          continue;
        }
        for (const std::size_t atom_index : found->second) {
          refresh_model_phase_atom(atom_index);
        }
      }
    }
    clear_model_phase_dirty_vars();
  }

  void refresh_model_phase_atom(std::size_t atom_index) {
    const std::optional<sat::Lit> phase_lit = model_phase_literal(atom_index);
    if (!phase_lit.has_value()) {
      return;
    }
    const int var = phase_lit->value > 0 ? phase_lit->value : -phase_lit->value;
    if (model_phase_by_var_[static_cast<std::size_t>(var)] == phase_lit->value) {
      return;
    }
    model_phase_by_var_[static_cast<std::size_t>(var)] = phase_lit->value;
    backend_.phase(*phase_lit);
    statistics_.increment(kStatLraModelPhaseHints);
  }

  void mark_model_phase_bound_var_dirty(theory::arithmetic::LraVarId bound_var) {
    if (!model_phase_enabled_ || model_phase_all_dirty_ ||
        bound_var >= model_phase_dirty_bound_vars_.size() ||
        model_phase_dirty_bound_vars_[bound_var] != 0) {
      return;
    }
    model_phase_dirty_bound_vars_[bound_var] = 1;
    model_phase_dirty_bound_var_list_.push_back(bound_var);
  }

  void mark_all_model_phases_dirty() {
    if (!model_phase_enabled_) {
      return;
    }
    clear_model_phase_dirty_vars();
    model_phase_all_dirty_ = true;
  }

  void clear_model_phase_dirty_vars() {
    for (const theory::arithmetic::LraVarId bound_var : model_phase_dirty_bound_var_list_) {
      model_phase_dirty_bound_vars_[bound_var] = 0;
    }
    model_phase_dirty_bound_var_list_.clear();
    model_phase_all_dirty_ = false;
  }

  // Query only a feasible assignment, as required by the model-value filter.
  // A component's complete edge-premise union is a sound (possibly coarse)
  // explanation for any indexed pair in that component.
  void scan_variable_equalities_if_needed() {
    if (!equality_scan_dirty_ || conflict_.has_value() || equality_targets_.empty()) return;
    equality_scan_dirty_ = false;
    ScopedStatisticsTimer timer(statistics_, ":time.lra-equality-propagation-us");
    statistics_.increment(":lra.equality-propagation.scans");
    const auto query = tableau_.model_filtered_unit_equalities(equality_variables_);
    statistics_.increment(":lra.equality-propagation.equalities", query.equalities.size());
    statistics_.increment(":lra.equality-propagation.rows-inspected", query.stats.rows_inspected);
    statistics_.increment(":lra.equality-propagation.nonzeros-inspected",
                          query.stats.nonzeros_inspected);
    if (query.equalities.empty()) return;

    std::vector<theory::arithmetic::LraVarId> parent(tableau_.variable_count());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    const auto root = [&parent](theory::arithmetic::LraVarId var) {
      while (parent[var] != var) {
        parent[var] = parent[parent[var]];
        var = parent[var];
      }
      return var;
    };
    for (const auto& equality : query.equalities) {
      parent[root(equality.rhs)] = root(equality.lhs);
    }
    std::unordered_map<theory::arithmetic::LraVarId, std::vector<sat::Lit>> component_reasons;
    for (const auto& equality : query.equalities) {
      auto& reasons = component_reasons[root(equality.lhs)];
      reasons.insert(reasons.end(), equality.reasons.begin(), equality.reasons.end());
    }
    for (auto& [representative, reasons] : component_reasons) {
      std::sort(reasons.begin(), reasons.end(),
                [](sat::Lit a, sat::Lit b) { return a.value < b.value; });
      reasons.erase(std::unique(reasons.begin(), reasons.end(),
                                [](sat::Lit a, sat::Lit b) { return a.value == b.value; }),
                    reasons.end());
    }
    const theory::arithmetic::DeltaRational zero;
    for (const EqualityTarget& target : equality_targets_) {
      const auto representative = root(target.lhs);
      if (representative != root(target.rhs)) continue;
      const auto& prepared_atom = prepared_.atoms[target.atom_index];
      const BoundAssertion assertion = bound_assertion(
          prepared_atom.relation, prepared_atom.rhs, prepared_atom.asserts_lower_when_true,
          /*atom_value=*/true, prepared_atom.integer_rounding);
      const bool holds = assertion.is_upper ? zero <= assertion.value : zero >= assertion.value;
      const int atom_lit = theory_atoms_[target.atom_index].lit.value;
      const int lit = holds ? atom_lit : -atom_lit;
      const auto& reasons = component_reasons.at(representative);
      if (is_assigned_var(std::abs(lit))) {
        if (assignment_view().literal(std::abs(lit)) == lit) continue;
        conflict_ = reasons;
        conflict_->push_back(sat::Lit{lit});
        record_query_conflict();
        statistics_.increment(":lra.equality-propagation.conflicts");
        return;
      }
      pending_.push_back(PendingPropagation{lit, reasons, false, false, true});
    }
  }

  void observe_true_interface_bound(std::size_t atom_index, sat::Lit reason) {
    if (!interface_equalities_enabled_) {
      return;
    }
    const TermPairKey pair = prepared_.atoms[atom_index].non_strict_le_pair;
    if (pair.lhs == core::kInvalidTermId) {
      return;
    }
    true_lra_le_reasons_[pair] = reason;
    enqueue_lra_interface_equality_if_forced(pair);
  }

  void rebuild_true_interface_bounds() {
    if (!interface_equalities_enabled_) {
      return;
    }
    true_lra_le_reasons_.clear();
    for (std::size_t atom_index = 0; atom_index < theory_atoms_.size(); ++atom_index) {
      const TermPairKey pair = prepared_.atoms[atom_index].non_strict_le_pair;
      if (pair.lhs == core::kInvalidTermId) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        continue;
      }
      const int assigned_lit = assignment_view().literal(var);
      const bool atom_value = atom.lit.value > 0 ? assigned_lit > 0 : assigned_lit < 0;
      if (atom_value) {
        true_lra_le_reasons_[pair] = sat::Lit{-assigned_lit};
      }
    }
  }

  void enqueue_lra_interface_equality_if_forced(const TermPairKey& pair) {
    const auto forward = true_lra_le_reasons_.find(pair);
    if (forward == true_lra_le_reasons_.end()) {
      return;
    }
    const auto reverse = true_lra_le_reasons_.find(TermPairKey{pair.rhs, pair.lhs});
    if (reverse == true_lra_le_reasons_.end()) {
      return;
    }
    const TermPairKey canonical_pair = pair.lhs < pair.rhs ? pair : TermPairKey{pair.rhs, pair.lhs};
    if (!published_interface_pairs_.insert(canonical_pair).second) {
      return;
    }
    pending_interface_equalities_.push_back(
        InterfaceEqualityPropagation{pair, {forward->second, reverse->second}});
  }

  // Scans the other atoms sharing `slack` with the atom that was just
  // asserted (index `triggering_atom_index`, already applied to `tableau_`
  // with no direct conflict) for ones now entailed true/false by the
  // slack's current upper/lower bound (Dutertre & de Moura Section 4.2).
  // Only needs to look at this one slack: bounds only ever change via
  // explicit assert_upper/assert_lower (never via check()'s pivots, which
  // only touch assignments/row structure), so no other slack's bounds
  // could have changed as a side effect of this assertion.
  void scan_for_propagations(std::size_t triggering_atom_index,
                             theory::arithmetic::LraVarId bound_var) {
    const auto found = prepared_.atoms_by_bound_var.find(bound_var);
    if (found == prepared_.atoms_by_bound_var.end()) {
      return;
    }
    for (const std::size_t atom_index : found->second) {
      if (atom_index == triggering_atom_index) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      const auto entails_true =
          bound_entails(tableau_, prepared_atom.bound_var,
                        bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                        prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                                        prepared_atom.integer_rounding));
      if (entails_true.has_value()) {
        pending_.push_back(PendingPropagation{atom.lit.value, {*entails_true}, false});
        continue;
      }
      const auto entails_false =
          bound_entails(tableau_, prepared_atom.bound_var,
                        bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                        prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                                        prepared_atom.integer_rounding));
      if (entails_false.has_value()) {
        pending_.push_back(PendingPropagation{-atom.lit.value, {*entails_false}, false});
      }
    }
  }

  // Whether the implied bound `implied` on `basic_var` is tight enough to
  // entail the atom whose truth assignment would impose `assertion`. Shares
  // bound_assertion()'s direction convention, so a flipped atom's TRUE case
  // is checked against an implied *lower* bound and vice versa.
  std::optional<std::vector<sat::Lit>> implied_bound_entails(theory::arithmetic::LraVarId basic_var,
                                                             const BoundAssertion& assertion) {
    auto implied =
        compute_implied_bound(tableau_, basic_var, assertion.is_upper, row_propagation_limits_);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // Row-based/interval bound propagation (Dutertre & de Moura Section 4,
  // generalizing scan_for_propagations to a *derived* rather than
  // directly-asserted bound): `changed_var`'s bound was just tightened, so
  // any row currently mentioning it (tableau_.occurs_in) could now have a
  // tighter implied bound. For each such row, compute the implied
  // upper/lower via compute_implied_bound and check entailment for the atoms
  // bounding that row's own basic variable.
  //
  // Before item H1 this could only trigger when `changed_var` happened to be
  // a slack wrapping exactly one coefficient-1 variable, since bounds only
  // ever lived on slacks; now that single-variable atoms bound their variable
  // directly, any bound change is a candidate and the fanout cap below is
  // what keeps the cost bounded.
  void scan_row_based_propagations(theory::arithmetic::LraVarId changed_var) {
    const std::vector<theory::arithmetic::LraVarId>& affected_rows =
        tableau_.occurs_in(changed_var);
    if (affected_rows.empty() || affected_rows.size() > row_propagation_limits_.max_fanout) {
      return;
    }
    for (const theory::arithmetic::LraVarId basic_var : affected_rows) {
      const auto found = prepared_.atoms_by_bound_var.find(basic_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_bound_entails(
            basic_var, bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                       prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                                       prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true});
          continue;
        }
        reasons = implied_bound_entails(
            basic_var, bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                       prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                                       prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true});
        }
      }
      if (bidirectional_row_propagation_enabled_ &&
          affected_rows.size() <= row_propagation_limits_.bidirectional_max_fanout) {
        scan_row_child_propagations(basic_var, changed_var);
      }
    }
  }

  // Competitiveness-roadmap item 7 (the reverse direction of
  // scan_row_based_propagations): for each *other* term of basic_var's
  // row (skipping exclude_var, whose own bound is what just triggered
  // this scan -- nothing new to derive about it from itself), check
  // whether compute_implied_child_bound now entails one of that term's
  // own atoms. Gated by its own (smaller) size caps -- see their comment.
  void scan_row_child_propagations(theory::arithmetic::LraVarId basic_var,
                                   theory::arithmetic::LraVarId exclude_var) {
    const auto& row = tableau_.row(basic_var);
    if (row.size() > row_propagation_limits_.bidirectional_max_row_size) {
      return;
    }
    for (const auto& [target_var, target_coeff, target_var_slot] : row) {
      if (target_var == exclude_var) {
        continue;
      }
      const auto found = prepared_.atoms_by_bound_var.find(target_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                            prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true, true});
          continue;
        }
        reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                            prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true, true});
        }
      }
    }
  }

  // implied_bound_entails' counterpart for the reverse (child-tightening)
  // direction -- same convention, same all-or-nothing behaviour.
  std::optional<std::vector<sat::Lit>> implied_child_bound_entails(
      theory::arithmetic::LraVarId basic_var, theory::arithmetic::LraVarId target_var,
      const util::Rational& target_coeff, const BoundAssertion& assertion) {
    auto implied = compute_implied_child_bound(tableau_, basic_var, target_var, target_coeff,
                                               assertion.is_upper, row_propagation_limits_);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // A queued propagation candidate: the CaDiCaL literal to propagate, its
  // antecedent reason literals (one for same-slack propagation; up to a
  // row's term count for row-based propagation), and whether it came from
  // the row-based scan (for the LraRowPropagations breakdown stat).
  struct PendingPropagation {
    int lit;
    std::vector<sat::Lit> reasons;
    bool row_based;
    bool bidirectional = false;  // competitiveness-roadmap item 7
    bool variable_equality = false;
  };

  struct EqualityTarget {
    theory::arithmetic::LraVarId lhs;
    theory::arithmetic::LraVarId rhs;
    std::size_t atom_index;
  };

  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  const PreparedLraTheory& prepared_;
  theory::arithmetic::LraTableau tableau_;
  bool propagation_enabled_;
  bool row_propagation_enabled_;
  bool bidirectional_row_propagation_enabled_;
  // The caps enforced by scan_row_based_propagations and the two
  bool theory_decisions_enabled_;
  bool model_phase_enabled_;
  bool model_phase_backtrack_may_change_beta_;
  // compute_implied_*_bound helpers. Held rather than read from options at
  // each use so the propagator stays independent of CheckSatOptions.
  LraRowPropagationLimits row_propagation_limits_;
  sat::IpasirUpBackend& backend_;
  util::Statistics& statistics_;
  bool count_query_conflicts_ = false;
  std::optional<std::size_t> conflict_limit_;
  bool external_clause_pending_ = false;
  std::vector<char> observed_vars_;
  std::vector<int> model_phase_by_var_;
  std::vector<char> model_phase_dirty_bound_vars_;
  std::vector<theory::arithmetic::LraVarId> model_phase_dirty_bound_var_list_;
  std::vector<std::size_t> atom_index_by_var_;
  std::vector<std::size_t> checkpoints_;
  std::deque<PendingPropagation> pending_;
  std::deque<InterfaceEqualityPropagation> pending_interface_equalities_;
  bool interface_equalities_enabled_ = false;
  bool equality_propagation_enabled_ = false;
  bool equality_scan_dirty_ = false;
  std::vector<theory::arithmetic::LraVarId> equality_variables_;
  std::vector<EqualityTarget> equality_targets_;
  std::unordered_map<TermPairKey, sat::Lit, TermPairKeyHash> true_lra_le_reasons_;
  std::unordered_set<TermPairKey, TermPairKeyHash> published_interface_pairs_;
  std::unordered_map<int, std::vector<sat::Lit>> reasons_;
  int reason_stream_active_lit_ = 0;
  int reason_stream_index_ = 0;
  std::optional<sat::Clause> conflict_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
  // See notify_assignment_lit's comment: set instead of running
  // tableau_.check() eagerly per literal, resolved lazily by
  // run_deferred_check_if_needed() (called from cb_has_external_clause()).
  bool needs_check_ = false;
  bool model_phase_all_dirty_ = true;
  // cb_decide()'s rotating position (competitiveness-roadmap item 8):
  // gives cb_decide() a stable, persistent round-robin order over
  // arithmetic atoms across calls, rather than always restarting from
  // index 0. Not reset on backtrack -- it's just a decision-order cursor,
  // not trail state; a stale position after backtracking is harmless
  // (worst case, this decision's order is less well-chosen than usual).
  std::size_t decide_cursor_ = 0;
};

class MixedTheoryPropagationDispatcher final : public sat::ExternalPropagator {
 public:
  MixedTheoryPropagationDispatcher(core::TermManager& terms, BooleanClausifier& clausifier,
                                   sat::IpasirUpBackend& backend, TheoryCombinationManager& manager,
                                   OnlineEufPropagator* euf_propagator,
                                   OnlineLraPropagator* lra_propagator,
                                   const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
                                   bool publish_lra_interface_equalities,
                                   util::Statistics& statistics)
      : terms_(terms),
        clausifier_(clausifier),
        backend_(backend),
        manager_(manager),
        euf_propagator_(euf_propagator),
        lra_propagator_(lra_propagator),
        statistics_(statistics) {
    // The only consumer of interface-equality publishing; see
    // OnlineLraPropagator::set_interface_equalities_enabled.
    if (lra_propagator_ != nullptr && publish_lra_interface_equalities) {
      lra_propagator_->set_interface_equalities_enabled(true);
    }
    publish_lra_interface_equalities_ = publish_lra_interface_equalities;
    int max_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : lra_atoms) {
      max_var = std::max(max_var, sat_var(atom.lit));
    }
    for (const InterfaceEqualityRegistryEntry& entry : manager_.summary().equality_registry) {
      if (entry.lit.value != 0) {
        max_var = std::max(max_var, sat_var(entry.lit));
      }
    }
    interface_pair_by_var_.assign(static_cast<std::size_t>(max_var) + 1, TermPairKey{});
    observed_interface_var_.assign(static_cast<std::size_t>(max_var) + 1, 0);

    statistics_.set(":no.online.enabled", 1);
  }

  void observe_registered_interface_equalities(bool count_observation = true) {
    statistics_.increment(":no.interface.euf-live-registry-syncs");
    for (std::size_t index = 0; index < manager_.summary().equality_registry.size(); ++index) {
      // Observation can synchronously deliver assignments and extend this registry.
      const auto entry = manager_.summary().equality_registry[index];
      observe_interface_equality_entry(entry, count_observation);
    }
  }

  OnlineEufPropagator::ModelScopeAvailability euf_model_scope_availability() const {
    return euf_propagator_ != nullptr
               ? euf_propagator_->model_scope_availability()
               : OnlineEufPropagator::ModelScopeAvailability::NoIncrementalState;
  }

  std::optional<IncrementalFlatAssignmentState::ModelScope> begin_euf_model_scope() {
    return euf_propagator_ != nullptr ? euf_propagator_->begin_model_scope() : std::nullopt;
  }

  void bind_assignment_view(sat::TrailView view) override {
    sat::ExternalPropagator::bind_assignment_view(view);
    if (euf_propagator_) euf_propagator_->bind_assignment_view(view);
    if (lra_propagator_) lra_propagator_->bind_assignment_view(view);
  }
  bool assignment_notifications_deferred() const override {
    return euf_propagator_ && euf_propagator_->assignment_notifications_deferred();
  }

  void notify_assignment(const std::vector<int>& lits) override {
    if (euf_propagator_ != nullptr) {
      euf_propagator_->notify_assignment(lits);
    }
    if (lra_propagator_ != nullptr && publish_lra_interface_equalities_) {
      lra_propagator_->notify_assignment(lits);
      drain_lra_interface_equalities();
    } else if (lra_propagator_ != nullptr) {
      lra_propagator_->notify_assignment(lits);
    }
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
  }

  void notify_new_decision_level() override {
    if (euf_propagator_ != nullptr) {
      euf_propagator_->notify_new_decision_level();
    }
    if (lra_propagator_ != nullptr) {
      lra_propagator_->notify_new_decision_level();
    }
    manager_.push_level();
  }

  void notify_backtrack(std::size_t) override {
    throw std::logic_error("mixed backtrack requires the shared removal delta");
  }
  void notify_backtrack_delta(std::size_t new_level, const std::vector<int>& removed) override {
    if (euf_propagator_ != nullptr) {
      euf_propagator_->notify_backtrack_delta(new_level, removed);
    }
    if (lra_propagator_ != nullptr) {
      lra_propagator_->notify_backtrack_delta(new_level, removed);
    }
    manager_.pop_to_level(new_level);
    for (const int lit : removed) {
      reasons_.erase(lit);
      reasons_.erase(-lit);
      propagated_owner_.erase(lit);
      propagated_owner_.erase(-lit);
    }
    pending_.clear();
    pending_pairs_.clear();
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    statistics_.increment(":no.online.backtracks");
  }

  bool cb_check_found_model(const std::vector<int>& model) override {
    if (euf_propagator_ != nullptr && !euf_propagator_->cb_check_found_model(model)) {
      return false;
    }
    if (lra_propagator_ != nullptr && !lra_propagator_->cb_check_found_model(model)) {
      return false;
    }
    return true;
  }

  int cb_decide() override { return lra_propagator_ != nullptr ? lra_propagator_->cb_decide() : 0; }

  int cb_propagate() override {
    if (euf_propagator_ != nullptr) {
      const int lit = euf_propagator_->cb_propagate();
      if (lit != 0) {
        propagated_owner_[lit] = PropagationOwner::Euf;
        statistics_.increment(":no.online.euf-propagations");
        return lit;
      }
    }
    if (lra_propagator_ != nullptr) {
      const int lit = lra_propagator_->cb_propagate();
      if (lit != 0) {
        propagated_owner_[lit] = PropagationOwner::Lra;
        return lit;
      }
    }
    while (!pending_.empty()) {
      PendingEquality candidate = std::move(pending_.front());
      pending_.pop_front();
      pending_pairs_.erase(candidate.pair);
      std::optional<sat::Lit> equality_lit =
          materialize_observed_interface_equality_lit(candidate.pair);
      if (!equality_lit.has_value()) {
        statistics_.increment(":no.online.lra-bound-equality-missing-atoms");
        continue;
      }
      const int var = sat_var(equality_lit.value());
      if (assignment_view().literal(var) != 0) {
        statistics_.increment(":no.online.skipped-assigned");
        continue;
      }
      const int lit = equality_lit->value;
      reasons_[lit] = std::move(candidate.reasons);
      propagated_owner_[lit] = PropagationOwner::NelsonOppen;
      statistics_.increment(":no.online.propagations");
      statistics_.increment(":no.online.reason-lits", reasons_[lit].size());
      return lit;
    }
    return 0;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    const auto owner = propagated_owner_.find(propagated_lit);
    if (owner != propagated_owner_.end() && owner->second == PropagationOwner::Euf &&
        euf_propagator_ != nullptr) {
      return euf_propagator_->cb_add_reason_clause_lit(propagated_lit);
    }
    if (owner != propagated_owner_.end() && owner->second == PropagationOwner::Lra &&
        lra_propagator_ != nullptr) {
      return lra_propagator_->cb_add_reason_clause_lit(propagated_lit);
    }
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      return 0;
    }
    if (reason_stream_active_lit_ != propagated_lit) {
      reason_stream_active_lit_ = propagated_lit;
      reason_stream_index_ = 0;
    }
    if (reason_stream_index_ == 0) {
      ++reason_stream_index_;
      return propagated_lit;
    }
    const std::size_t antecedent_index = static_cast<std::size_t>(reason_stream_index_ - 1);
    if (antecedent_index < found->second.size()) {
      ++reason_stream_index_;
      return found->second[antecedent_index].value;
    }
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    return 0;
  }

  bool cb_has_external_clause(bool& is_forgettable) override {
    if (euf_propagator_ != nullptr && euf_propagator_->cb_has_external_clause(is_forgettable)) {
      active_external_owner_ = PropagationOwner::Euf;
      statistics_.increment(":no.online.euf-external-clauses");
      return true;
    }
    if (lra_propagator_ != nullptr && lra_propagator_->cb_has_external_clause(is_forgettable)) {
      active_external_owner_ = PropagationOwner::Lra;
      return true;
    }
    return false;
  }

  bool cb_has_external_clause() override {
    if (euf_propagator_ != nullptr && euf_propagator_->cb_has_external_clause()) {
      active_external_owner_ = PropagationOwner::Euf;
      statistics_.increment(":no.online.euf-external-clauses");
      return true;
    }
    if (lra_propagator_ != nullptr && lra_propagator_->cb_has_external_clause()) {
      active_external_owner_ = PropagationOwner::Lra;
      return true;
    }
    return false;
  }

  int cb_add_external_clause_lit() override {
    if (active_external_owner_ == PropagationOwner::Euf && euf_propagator_ != nullptr) {
      const int lit = euf_propagator_->cb_add_external_clause_lit();
      if (lit == 0) {
        active_external_owner_ = PropagationOwner::None;
      }
      return lit;
    }
    if (active_external_owner_ == PropagationOwner::Lra && lra_propagator_ != nullptr) {
      const int lit = lra_propagator_->cb_add_external_clause_lit();
      if (lit == 0) {
        active_external_owner_ = PropagationOwner::None;
      }
      return lit;
    }
    return 0;
  }

 private:
  enum class PropagationOwner {
    None,
    Euf,
    Lra,
    NelsonOppen,
  };

  struct PendingEquality {
    TermPairKey pair;
    std::vector<sat::Lit> reasons;
  };

  void notify_assignment_lit(int lit) {
    const int var = sat_var(sat::Lit{lit});
    if (static_cast<std::size_t>(var) >= interface_pair_by_var_.size()) return;
    statistics_.increment(":no.online.assignments");

    const TermPairKey interface_pair = interface_pair_by_var_[static_cast<std::size_t>(var)];
    if (interface_pair.lhs != core::kInvalidTermId) {
      manager_.set_interface_equality_value(
          interface_pair.lhs, interface_pair.rhs,
          lit > 0 ? InterfaceEqualityValue::Equal : InterfaceEqualityValue::Disequal);
    }
  }

  void drain_lra_interface_equalities() {
    if (lra_propagator_ == nullptr) {
      return;
    }
    std::vector<OnlineLraPropagator::InterfaceEqualityPropagation> candidates =
        lra_propagator_->take_interface_equality_propagations();
    statistics_.increment(":no.online.lra-published-equalities", candidates.size());
    for (OnlineLraPropagator::InterfaceEqualityPropagation& candidate : candidates) {
      enqueue_if_interface_equality_lit_available(candidate.pair, std::move(candidate.reasons));
    }
  }

  void enqueue_if_interface_equality_lit_available(const TermPairKey& pair,
                                                   std::vector<sat::Lit> reasons) {
    statistics_.increment(":no.online.lra-bound-equality-candidates");
    if (!manager_.ensure_interface_pair(pair.lhs, pair.rhs).has_value()) {
      statistics_.increment(":no.online.lra-bound-equality-missing-atoms");
      return;
    }
    const TermPairKey canonical_pair = canonical_term_pair(pair.lhs, pair.rhs);
    if (!pending_pairs_.insert(canonical_pair).second) {
      return;
    }
    pending_.push_back(PendingEquality{canonical_pair, std::move(reasons)});
  }

  std::optional<sat::Lit> materialize_observed_interface_equality_lit(const TermPairKey& pair) {
    if (const std::optional<InterfaceEqualityRegistryEntry> existing =
            manager_.interface_equality(pair.lhs, pair.rhs);
        existing.has_value() && existing->lit.value != 0) {
      observe_interface_equality_entry(existing.value());
      statistics_.increment(":no.online.lra-bound-equality-materialized");
      return existing->lit;
    }

    const core::TermId equality = terms_.mk_eq(pair.lhs, pair.rhs);
    if (!manager_.ensure_interface_pair(pair.lhs, pair.rhs).has_value()) {
      return std::nullopt;
    }
    const sat::Lit lit{backend_.new_observed_var()};
    clausifier_.ensure_generated_theory_atom_with_lit(equality, lit);
    if (!manager_.register_interface_equality_atom(equality, lit)) {
      return std::nullopt;
    }
    statistics_.increment(":no.online.lazy-equality-atoms-created");
    statistics_.increment(":no.online.lazy-equality-vars-observed");
    statistics_.increment(":no.online.lra-bound-equality-materialized");
    statistics_.set(":no.interface.sat-equality-atoms",
                    registered_interface_equality_atom_count(manager_));
    statistics_.set(":no.interface.materialized-pairs",
                    manager_.summary().equality_registry.size());
    statistics_.set(":no.interface.materialized-missing-pairs",
                    manager_.summary().missing_equalities);
    statistics_.set(kStatTheoryAtoms, clausifier_.theory_atoms().size());
    if (const std::optional<InterfaceEqualityRegistryEntry> entry =
            manager_.interface_equality(equality);
        entry.has_value()) {
      observe_interface_equality_entry(entry.value(), /*count_observation=*/false,
                                       /*already_observed=*/true);
    }
    return lit;
  }

  void ensure_var_capacity(int var) {
    if (var < 0) {
      return;
    }
    const std::size_t needed = static_cast<std::size_t>(var) + 1U;
    if (interface_pair_by_var_.size() < needed) {
      interface_pair_by_var_.resize(needed, TermPairKey{});
      observed_interface_var_.resize(needed, 0);
    }
  }

  void observe_interface_equality_entry(const InterfaceEqualityRegistryEntry& entry,
                                        bool count_observation = true,
                                        bool already_observed = false) {
    if (entry.lit.value == 0) {
      return;
    }
    if (euf_propagator_ != nullptr && entry.atom != core::kInvalidTermId &&
        !euf_propagator_->observe_generated_interface_equality(
            BooleanClausifier::TheoryAtom{entry.lit, entry.atom, false, false})) {
      statistics_.increment(":no.online.euf-generated-equality-observation-failures");
    }
    const int var = sat_var(entry.lit);
    ensure_var_capacity(var);
    interface_pair_by_var_[static_cast<std::size_t>(var)] = TermPairKey{entry.lhs, entry.rhs};
    if (observed_interface_var_[static_cast<std::size_t>(var)] == 0) {
      if (!already_observed) {
        backend_.add_observed_var(var);
      }
      observed_interface_var_[static_cast<std::size_t>(var)] = 1;
      if (count_observation) {
        statistics_.increment(":no.online.lazy-equality-vars-observed");
      }
    }
  }

  core::TermManager& terms_;
  BooleanClausifier& clausifier_;
  sat::IpasirUpBackend& backend_;
  TheoryCombinationManager& manager_;
  OnlineEufPropagator* euf_propagator_ = nullptr;
  OnlineLraPropagator* lra_propagator_ = nullptr;
  bool publish_lra_interface_equalities_ = false;
  util::Statistics& statistics_;
  std::vector<TermPairKey> interface_pair_by_var_;
  std::deque<PendingEquality> pending_;
  std::unordered_set<TermPairKey, TermPairKeyHash> pending_pairs_;
  std::vector<char> observed_interface_var_;
  std::unordered_map<int, std::vector<sat::Lit>> reasons_;
  std::unordered_map<int, PropagationOwner> propagated_owner_;
  PropagationOwner active_external_owner_ = PropagationOwner::None;
  int reason_stream_active_lit_ = 0;
  int reason_stream_index_ = 0;
};
#endif  // PRIMO_HAVE_IPASIR

CheckSatOutcome solve_qf_lra_impl(core::TermManager& terms,
                                  const std::vector<core::TermId>& assertions,
                                  const std::vector<core::TermId>& model_terms,
                                  const ModelConversionChain& conversions,
                                  const CheckSatOptions& options, util::Statistics& statistics) {
#if !PRIMO_HAVE_IPASIR
  if (options.lra_equality_propagation) {
    throw UnsupportedInput("--lra-equality-propagation requires IPASIR-UP");
  }
#endif
  statistics.increment(kStatSolverQfLra);
  sat::CadicalBackend backend(options.cadical_options);
  SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
  const ScopedTerminatorConnection terminator_connection(backend, terminator);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);

  const PreparedLraTheory prepared_theory(
      terms, clausifier.theory_atoms(), lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, options.lra_fixed_elimination, options.produce_model,
      options.lra_least_violated_leaving, lra_tuning_from_options(options.lra_tuning));
  publish_lra_tableau_shape_stats(prepared_theory.base_tableau, statistics);

#if PRIMO_HAVE_IPASIR
  // The trail-synced online propagator (Milestone 3a) is always preferred
  // when CaDiCaL's external-propagator API is available: it's strictly
  // better than the copy-per-round fallback below (real incremental reuse
  // via LraTableau::push/pop instead of reasserting everything from
  // scratch every SAT round), and correctness has already been verified
  // against it via the fuzz gate (see docs/run-log.json).
  OnlineLraPropagator online_propagator(
      prepared_theory, clausifier.theory_atoms(), options.lra_propagation,
      options.lra_row_propagation, options.lra_bidirectional_row_propagation,
      options.lra_theory_decisions, options.lra_model_phase.value_or(false),
      options.lra_pivoting_rule == LraPivotingRule::Soi || options.lra_fixed_elimination,
      options.lra_row_propagation_limits, backend, statistics, false, options.max_theory_conflicts);
  if (options.lra_equality_propagation) {
    online_propagator.enable_variable_equality_propagation();
  }
  const AssignmentConnection assignment_connection(backend, online_propagator);
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    backend.add_observed_var(var);
  }
#endif

  while (true) {
    if (auto stop = terminator.stop_outcome()) return *stop;
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
    publish_sat_stats(backend.statistics(), statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }
    statistics.increment(kStatLraTheoryChecks);
#if PRIMO_HAVE_IPASIR
    const sat::TrailView model_assignment = assignment_connection.view();
#else
    sat::Trail completed_assignment;
    load_assignment_snapshot(backend, clausifier.theory_atoms(), completed_assignment);
    const sat::TrailView model_assignment(completed_assignment);
#endif

#if PRIMO_HAVE_IPASIR
    // Authoritative already -- see OnlineLraPropagator::conflict()'s
    // comment. Always nullopt in practice at this point (a real conflict
    // would have blocked CaDiCaL from ever returning SAT), but re-checking
    // costs nothing and mirrors solve_qf_uf's own belt-and-suspenders style.
    const std::optional<sat::Clause> conflict_clause = online_propagator.conflict();
#else
    const std::vector<AssignedTheoryAtom> assigned_atoms =
        assigned_atoms_from_view(model_assignment, clausifier.theory_atoms());

    const std::optional<sat::Clause> conflict_clause =
        find_incremental_lra_theory_conflict(prepared_theory, assigned_atoms);
#endif
    if (!conflict_clause.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      if (!options.produce_model) {
        return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
      }
#if PRIMO_HAVE_IPASIR
      const std::vector<AssignedTheoryAtom> assigned_atoms =
          assigned_atoms_from_view(model_assignment, clausifier.theory_atoms());
      return CheckSatOutcome{
          CheckSatResult::Sat,
          "",
          statistics,
          "",
          InconclusiveReason::None,
          build_qf_lra_model(terms, model_terms, conversions, prepared_theory,
                             online_propagator.tableau(), assigned_atoms, statistics)};
#else
      theory::arithmetic::LraTableau model_tableau = prepared_theory.base_tableau;
      for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
        const AssignedTheoryAtom& atom = assigned_atoms[i];
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_theory.atoms[i];
        if (!prepared_atom.is_arithmetic || atom.filtered) {
          continue;
        }
        const auto conflict = assert_lra_atom_bound(
            model_tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs,
            atom.value, prepared_atom.asserts_lower_when_true, blocking_lit(atom));
        if (conflict.has_value()) {
          throw core::ModelError("LRA reconstruction found a bound conflict");
        }
      }
      if (model_tableau.check().has_value()) {
        throw core::ModelError("LRA reconstruction found a tableau conflict");
      }
      return CheckSatOutcome{CheckSatResult::Sat,
                             "",
                             statistics,
                             "",
                             InconclusiveReason::None,
                             build_qf_lra_model(terms, model_terms, conversions, prepared_theory,
                                                model_tableau, assigned_atoms, statistics)};
#endif
    }
    statistics.increment(kStatLraTheoryConflicts);
    record_theory_conflict(statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    if (conflict_clause->empty()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
    }
    backend.add_clause(conflict_clause.value());
    publish_sat_stats(backend.statistics(), statistics);
  }
}

// Narrows a round's coarse QF_LIA lemma -- the blocking literals of every
// arithmetic atom that fed the tableau -- to the derivation-based
// explanation the integer stages accumulated while proving this branch
// integer-infeasible (docs/open-leads.md lead 12d).
//
// Whether the narrowed clause is used is *checked*, not assumed: every
// literal in `explanation` must already appear in `coarse`. That single
// containment test is what keeps this incapable of turning a correct
// answer into a wrong one, and it covers both ways a derived explanation
// could be malformed:
//
//   * A literal that is not blocking for the current assignment would fail
//     to exclude it, and the SAT solver would hand back the very same
//     assignment forever. `coarse` is by construction guaranteed to
//     exclude it, so a subset of `coarse` is too.
//   * A leaked kDerivedBoundReason, or any other literal naming no atom of
//     this problem, is not in `coarse` either.
//
// An empty explanation also falls back rather than being emitted: the empty
// clause asserts the whole problem is unsat, which is far too strong a
// claim to arrive at by accident from an accumulation that is allowed to be
// a superset of the true reason.
sat::Clause generalized_lia_lemma(const sat::Clause& coarse,
                                  const theory::arithmetic::LiaExplanation& explanation,
                                  util::Statistics& statistics) {
  statistics.increment(util::StatKey::LiaCoarseLemmaLiterals, coarse.size());
  const auto fall_back = [&]() -> sat::Clause {
    statistics.increment(util::StatKey::LiaGeneralizationFallbacks);
    statistics.increment(util::StatKey::LiaGeneralizedLemmaLiterals, coarse.size());
    return coarse;
  };
  if (explanation.empty()) {
    return fall_back();
  }
  std::unordered_set<std::int32_t> coarse_lits;
  coarse_lits.reserve(coarse.size() * 2);
  for (const sat::Lit lit : coarse) {
    coarse_lits.insert(lit.value);
  }
  for (const sat::Lit lit : explanation.literals()) {
    if (coarse_lits.find(lit.value) == coarse_lits.end()) {
      return fall_back();
    }
  }
  statistics.increment(util::StatKey::LiaGeneralizedLemmas);
  statistics.increment(util::StatKey::LiaGeneralizedLemmaLiterals, explanation.literals().size());
  return explanation.literals();
}

void publish_lia_completion_stats(const theory::arithmetic::LiaCompletionResult& result,
                                  const LiaSettings& settings, util::Statistics& statistics) {
  statistics.increment(util::StatKey::LiaBranchNodes, result.branch_nodes);
  if (result.cube_called) {
    using CubeStatus = theory::arithmetic::LiaCubeResult::Status;
    statistics.increment(util::StatKey::LiaCubeCalls);
    statistics.increment(util::StatKey::LiaCubeMicroseconds, result.cube_microseconds);
    statistics.increment(util::StatKey::LiaCubeConstraints, result.cube.constraints);
    statistics.increment(util::StatKey::LiaCubeFixedCoordinates, result.cube.fixed_coordinates);
    statistics.increment(util::StatKey::LiaCubeDifferenceExemptions,
                         result.cube.difference_exemptions);
    statistics.increment(util::StatKey::LiaCubePivots, result.cube.pivots);
    if (result.cube.center_found) statistics.increment(util::StatKey::LiaCubeCenters);
    switch (result.cube.status) {
      case CubeStatus::Skipped:
        statistics.increment(util::StatKey::LiaCubeSkipped);
        break;
      case CubeStatus::Infeasible:
        statistics.increment(util::StatKey::LiaCubeInfeasible);
        break;
      case CubeStatus::LimitReached:
        statistics.increment(util::StatKey::LiaCubeLimits);
        break;
      case CubeStatus::CandidateRejected:
        statistics.increment(util::StatKey::LiaCubeRejected);
        break;
      case CubeStatus::Sat:
        statistics.increment(util::StatKey::LiaCubeModels);
        break;
    }
  }
  if (settings.patch_limit == 0) return;
  statistics.increment(util::StatKey::LiaPatchSweeps);
  statistics.increment(util::StatKey::LiaPatchedVariables, result.patched_variables);
  add_lia_patching_stats(statistics, result.patch_stats);
  if (result.patch_stats.fully_integral)
    statistics.increment(util::StatKey::LiaPatchRoundsFullyIntegral);
}

sat::Clause integer_conflict_clause(const PreparedLraTheory& prepared,
                                    std::span<const theory::AssignedTheoryLiteral> assigned,
                                    const theory::arithmetic::LiaExplanation& explanation,
                                    const LiaSettings& settings, util::Statistics& statistics) {
  sat::Clause coarse;
  for (std::size_t index = 0; index < assigned.size(); ++index) {
    if (prepared.atoms[index].is_arithmetic)
      coarse.push_back(
          sat::Lit{assigned[index].value ? -assigned[index].lit.value : assigned[index].lit.value});
  }
  // No asserted arithmetic atoms is an unconditional conflict. Do not attempt
  // explanation generalization, whose empty-explanation policy is a fallback.
  if (coarse.empty() || !settings.conflict_generalization) return coarse;
  return generalized_lia_lemma(coarse, explanation, statistics);
}

CheckSatOutcome solve_qf_lia_impl(core::TermManager& terms,
                                  const std::vector<core::TermId>& assertions,
                                  const std::vector<core::TermId>& model_terms,
                                  const CheckSatOptions& options, const LiaSettings& settings,
                                  util::Statistics& statistics,
                                  const ModelConversionChain& conversions);

CheckSatOutcome solve_qf_lia_engine_impl(core::TermManager& terms,
                                         const std::vector<core::TermId>& assertions,
                                         const std::vector<core::TermId>& model_terms,
                                         const CheckSatOptions& options,
                                         const LiaSettings& settings, util::Statistics& statistics,
                                         const ModelConversionChain& conversions) {
  const std::uint64_t promotions_before = util::Rational::promotion_count();
  const std::uint64_t big_operations_before = util::Rational::big_operation_count();
  CheckSatOutcome outcome =
      solve_qf_lia_impl(terms, assertions, model_terms, options, settings, statistics, conversions);
  const std::uint64_t promotions = util::Rational::promotion_count() - promotions_before;
  const std::uint64_t big_operations =
      util::Rational::big_operation_count() - big_operations_before;
  statistics.increment(util::StatKey::RationalPromotions, promotions);
  statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  outcome.statistics.increment(util::StatKey::RationalPromotions, promotions);
  outcome.statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  return outcome;
}

#if PRIMO_HAVE_IPASIR
bool is_euf_component_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    return true;
  }
  return (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
         term.sort == terms.symbols().bool_sort();
}

#endif

// Recipes own instrumentation policy. Empty keys disable timers entirely.
struct LiaComponentTiming {
  std::string_view check;
  std::string_view rational;
  std::string_view integer;
};

struct LiaComponentCheck {
  enum class Status {
    Sat,
    Unsat,
    ResourceLimit,
  };

  Status status = Status::ResourceLimit;
  sat::Clause conflict;
  std::optional<theory::arithmetic::LraTableau> model;
};

LiaComponentCheck complete_lia_integer_model(
    theory::arithmetic::LraTableau tableau, const PreparedLraTheory& prepared,
    std::span<const theory::AssignedTheoryLiteral> assigned_atoms,
    const std::vector<theory::arithmetic::LraVarId>& integer_vars, const LiaSettings& settings,
    util::Statistics& statistics, const LiaComponentTiming& timing) {
  ScopedStatisticsTimer integer_timer(statistics, timing.integer);
  std::optional<theory::arithmetic::LiaCubeProblem> cube_problem;
  if (settings.cube_mode != theory::arithmetic::LiaCubeMode::Off &&
      std::ranges::any_of(integer_vars, [&](theory::arithmetic::LraVarId variable) {
        return !theory::arithmetic::is_integer_valued(tableau.assignment(variable));
      })) {
    std::vector<theory::arithmetic::LraVarId> coordinates;
    coordinates.reserve(prepared.var_ids_.size());
    for (const auto& [term, variable] : prepared.var_ids_) coordinates.push_back(variable);
    cube_problem = theory::arithmetic::LiaCubeProblem::from_tableau(prepared.base_tableau,
                                                                    std::move(coordinates));
  }
  const auto completion = theory::arithmetic::complete_integer_assignment(
      tableau, integer_vars,
      {settings.patch_limit, settings.gomory_cut_limit, settings.branch_limit,
       settings.patch_integral_moves, settings.cube_mode, settings.cube_steps},
      cube_problem ? &*cube_problem : nullptr);
  publish_lia_completion_stats(completion, settings, statistics);
  if (completion.status == theory::arithmetic::LiaCompletionResult::Status::Sat) {
    return {.status = LiaComponentCheck::Status::Sat, .conflict = {}, .model = std::move(tableau)};
  }
  if (completion.status == theory::arithmetic::LiaCompletionResult::Status::ResourceLimit) {
    return {
        .status = LiaComponentCheck::Status::ResourceLimit, .conflict = {}, .model = std::nullopt};
  }
  return {.status = LiaComponentCheck::Status::Unsat,
          .conflict = integer_conflict_clause(prepared, assigned_atoms, completion.explanation,
                                              settings, statistics),
          .model = std::nullopt};
}

LraTheoryCheckResult check_lia_component_relaxation(
    const PreparedLraTheory& prepared,
    std::span<const theory::AssignedTheoryLiteral> assigned_atoms, util::Statistics& statistics,
    const LiaComponentTiming& timing) {
  theory::arithmetic::LraTableau tableau = prepared.base_tableau;
  {
    ScopedStatisticsTimer rational_timer(statistics, timing.rational);
    for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
      const theory::AssignedTheoryLiteral& atom = assigned_atoms[i];
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
      if (!prepared_atom.is_arithmetic) {
        continue;
      }
      const auto conflict = assert_lra_atom_bound(
          tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
          prepared_atom.asserts_lower_when_true,
          sat::Lit{atom.value ? -atom.lit.value : atom.lit.value}, prepared_atom.integer_rounding);
      if (conflict.has_value()) {
        return {conflict, std::move(tableau)};
      }
    }
    if (const std::optional<sat::Clause> conflict = tableau.check(); conflict.has_value()) {
      return {conflict, std::move(tableau)};
    }
  }
  return {std::nullopt, std::move(tableau)};
}

// See project_lra_shared_model: the revision must advance across a mutation,
// so it is supplied by the component rather than fixed here.
std::optional<theory::TheoryModelProjection> project_lia_integer_model(
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& tableau,
    const std::vector<core::TermId>& shared_terms, core::SortId shared_sort,
    std::uint64_t assignment_epoch, std::uint64_t revision = 1) {
  std::vector<theory::SharedModelClass> classes;
  classes.reserve(shared_terms.size());
  for (const core::TermId term : shared_terms) {
    const auto variable = prepared.variables_by_term.find(term);
    if (variable == prepared.variables_by_term.end()) {
      return std::nullopt;
    }
    const auto value = tableau.assignment(variable->second);
    auto equal_class =
        std::ranges::find_if(classes, [&](const theory::SharedModelClass& candidate) {
          const auto representative = prepared.variables_by_term.find(candidate.terms.front());
          return representative != prepared.variables_by_term.end() &&
                 tableau.assignment(representative->second) == value;
        });
    if (equal_class == classes.end()) {
      classes.push_back(theory::SharedModelClass{.terms = {term}});
    } else {
      equal_class->terms.push_back(term);
    }
  }
  return theory::TheoryModelProjection{
      .token = {.component = "lia", .assignment_epoch = assignment_epoch, .revision = revision},
      .partitions = {{.sort = shared_sort, .equality_classes = std::move(classes)}}};
}

// Migration-local arithmetic execution adapter. The QF_UFLRA callback above
// is frozen; this preserves its existing Primo operations while replacing the
// SAT backend dependency with a phase-output port. No new simplex algorithm.
#if PRIMO_HAVE_IPASIR
class ComponentLraExecution final {
 public:
  ComponentLraExecution(const PreparedLraTheory& prepared,
                        const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                        bool propagation_enabled, bool row_propagation_enabled,
                        bool bidirectional_row_propagation_enabled, bool theory_decisions_enabled,
                        bool model_phase_enabled, bool model_phase_backtrack_may_change_beta,
                        const LraRowPropagationLimits& row_propagation_limits,
                        std::function<void(sat::Lit)> publish_phase, util::Statistics& statistics)
      : theory_atoms_(theory_atoms),
        prepared_(prepared),
        tableau_(prepared.base_tableau),
        propagation_enabled_(propagation_enabled),
        row_propagation_enabled_(row_propagation_enabled),
        bidirectional_row_propagation_enabled_(bidirectional_row_propagation_enabled),
        theory_decisions_enabled_(theory_decisions_enabled),
        model_phase_enabled_(model_phase_enabled),
        model_phase_backtrack_may_change_beta_(model_phase_backtrack_may_change_beta),
        row_propagation_limits_(row_propagation_limits),
        publish_phase_(std::move(publish_phase)),
        statistics_(statistics) {
    int max_theory_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      max_theory_var =
          std::max(max_theory_var, atom.lit.value > 0 ? atom.lit.value : -atom.lit.value);
    }
    observed_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    model_phase_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    model_phase_dirty_bound_vars_.assign(prepared_.base_tableau.variable_count(), 0);
    atom_index_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, theory_atoms_.size());
    for (std::size_t i = 0; i < theory_atoms_.size(); ++i) {
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[i];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      observed_vars_[static_cast<std::size_t>(var)] = 1;
      atom_index_by_var_[static_cast<std::size_t>(var)] = i;
    }
    refresh_model_phases();
  }

  void bind_assignment_view(sat::TrailView view) { assignment_view_ = view; }
  sat::TrailView assignment_view() const { return assignment_view_; }
  void notify_assignment(const std::vector<int>& lits) {
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
  }

  void notify_new_decision_level() { checkpoints_.push_back(tableau_.push()); }

  void notify_backtrack_delta(std::size_t new_level, const std::vector<int>& removed) {
    if (new_level < checkpoints_.size()) {
      tableau_.pop(checkpoints_[new_level]);
      checkpoints_.resize(new_level);
    }
    // Retained propagations still own reasons after unrelated backtracking.
    for (const int literal : removed) {
      reasons_.erase(literal);
      reasons_.erase(-literal);
    }
    rebuild_true_interface_bounds();
    published_interface_pairs_.clear();
    conflict_.reset();
    // Sound to drop unconditionally per notify_assignment_lit's own
    // verified invariant: a new decision level can never open while a
    // deferred check is still outstanding (cb_has_external_clause -- and
    // thus run_deferred_check_if_needed() -- is always consulted first),
    // so needs_check_ can never be true across a backtrack in a way that
    // would lose an actually-outstanding repair.
    needs_check_ = false;
    external_clause_.clear();
    external_clause_index_ = 0;
    // pending_ entries are just candidates, not delivered propagations --
    // safe to drop unconditionally (unlike reasons_ above): at worst a
    // still-valid candidate gets rediscovered next time its slack changes.
    pending_.clear();
    pending_interface_equalities_.clear();
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    if (model_phase_backtrack_may_change_beta_) {
      mark_all_model_phases_dirty();
    }
  }

  // Unconditional `true` is sound, but only because this propagator runs in
  // EAGER mode, and that coupling is not local -- so it is stated here.
  //
  // CaDiCaL's search loop (contrib/src/cadical/src/internal.cpp) runs
  // `external_propagate()` BEFORE it tests `satisfied()`, and
  // `external_propagate()` calls `ask_external_clause()` -- hence
  // `cb_has_external_clause()`, hence run_deferred_check_if_needed() --
  // unconditionally whenever there is no conflict
  // (external_propagate.cpp, "if (!unsat && !conflict)"). So by the time
  // `external_check_solution()` asks us to approve a model, the theory check
  // has already run this iteration and produced no conflict clause. There is
  // nothing left to verify.
  //
  // That entire argument collapses if the propagator is ever registered lazy.
  // The SAT adapter owns that backend-specific registration detail; CaDiCaL's
  // adapter currently leaves ExternalPropagator::is_lazy at its eager default.
  bool cb_check_found_model(const std::vector<int>& /*model*/) { return true; }

  // Competitiveness-roadmap item 8: called by CaDiCaL right before it would
  // otherwise make a generic VSIDS decision (Internal::ask_decision(),
  // contrib/src/cadical/src/external_propagate.cpp) -- supplies the next
  // not-yet-decided arithmetic atom's TRUE literal, advancing a persistent
  // rotating cursor across calls. A fixed, deterministic decision ORDER
  // over arithmetic atoms, not a per-candidate cost analysis.
  //
  // An earlier version of this (see git history around commit 17f8faf)
  // ranked candidates by a cheap per-atom infeasibility-cost estimate
  // instead of just taking the next one -- re-measured against this much
  // simpler alternative on the same 9 real-benchmark files (docs/
  // arithmetic.md's Milestone roadmap has the full trail), the cost-ranked
  // version was *worse* than plain VSIDS on 3 of them (solved regressed to
  // timed-out at the same budget) and worse than this round-robin version
  // on 8/9 -- sometimes dramatically (a 96% wall-clock regression on one
  // file). The ranking wasn't just expensive overhead; it was actively
  // steering search into worse territory. This version -- "the theory
  // decides, in a stable order, no per-candidate arithmetic at all" -- is
  // strictly better on this evidence, and the only one of the three that
  // solves the campaign's hardest file (sc-27) at all within a shared
  // budget. Returning 0 defers to VSIDS -- only happens once every
  // arithmetic atom is already decided.
  int cb_decide() {
    if (!theory_decisions_enabled_ || theory_atoms_.empty()) {
      return 0;
    }
    const std::size_t atom_count = theory_atoms_.size();
    for (std::size_t scanned = 0; scanned < atom_count; ++scanned) {
      const std::size_t atom_index = decide_cursor_;
      decide_cursor_ = (decide_cursor_ + 1) % atom_count;
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      if (!prepared_atom.is_arithmetic) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      statistics_.increment(kStatLraTheoryDecisions);
      return atom.lit.value;
    }
    return 0;
  }

  // Same-slack "deriving satisfied atoms" propagation (Dutertre & de
  // Moura, Section 4.2): pops literals queued by notify_assignment_lit's
  // propagation scan. Never propagates once a conflict is pending this
  // round (mirrors notify_assignment_lit's own early-return), and lazily
  // skips any queued entry whose var got assigned some other way in the
  // meantime (a decision, a different propagation) rather than proactively
  // pruning pending_ on every assignment.
  int cb_propagate() {
    if (conflict_.has_value()) {
      return 0;
    }
    while (!pending_.empty()) {
      PendingPropagation candidate = std::move(pending_.front());
      pending_.pop_front();
      const int var = candidate.lit > 0 ? candidate.lit : -candidate.lit;
      if (is_assigned_var(var)) {
        continue;
      }
      const int lit = candidate.lit;
      const bool row_based = candidate.row_based;
      const bool bidirectional = candidate.bidirectional;
      reasons_[lit] = std::move(candidate.reasons);
      statistics_.increment(kStatLraTheoryPropagations);
      if (row_based) {
        statistics_.increment(kStatLraRowPropagations);
      }
      if (bidirectional) {
        statistics_.increment(kStatLraBidirectionalRowPropagations);
      }
      return lit;
    }
    return 0;
  }

  int cb_add_reason_clause_lit(int propagated_lit) {
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      // Returning 0 here streams an EMPTY reason clause, and CaDiCaL
      // documents that an external clause which is empty after simplification
      // sets `unsat` -- i.e. a missing reason would answer "unsatisfiable".
      // In practice CaDiCaL's own REQUIRE ("external reason clause must
      // contain the propagated literal") aborts first, and contracts are
      // compiled into our build, so this is loud rather than silent. Assert
      // anyway, so the failure names OUR broken invariant -- cb_propagate
      // records reasons_[lit] before returning lit, and notify_backtrack
      // erases only literals it actually unassigns -- rather than CaDiCaL's
      // symptom. Getting that erasure wrong was a real bug once (see
      // notify_backtrack's comment).
      assert(false && "CaDiCaL asked for a reason we never recorded");
      return 0;
    }
    if (reason_stream_active_lit_ != propagated_lit) {
      reason_stream_active_lit_ = propagated_lit;
      reason_stream_index_ = 0;
    }
    if (reason_stream_index_ == 0) {
      ++reason_stream_index_;
      return propagated_lit;
    }
    const std::size_t antecedent_index = static_cast<std::size_t>(reason_stream_index_ - 1);
    if (antecedent_index < found->second.size()) {
      ++reason_stream_index_;
      return found->second[antecedent_index].value;
    }
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    return 0;
  }

  // Authoritative once backend.solve() returns SAT, for the same reason
  // OnlineEufPropagator::incremental_conflict() is: cb_has_external_clause()
  // (which runs the deferred Check/Pivot repair -- see
  // run_deferred_check_if_needed()) is always asked, and any resulting
  // conflict resolved, before CaDiCaL's own main loop can reach a
  // decision or declare a model satisfied (see notify_assignment_lit's
  // comment for the verified invariant), so there is nothing left to
  // recompute once solve() actually returns SAT.
  std::optional<sat::Clause> conflict() const { return conflict_; }
  const theory::arithmetic::LraTableau& tableau() const { return tableau_; }

  // Interface-equality publishing is Nelson-Oppen machinery, and only
  // MixedTheoryPropagationDispatcher ever drains it (see
  // take_interface_equality_propagations). On the pure QF_LRA path nothing
  // consumes it, yet it was maintained anyway: observe_true_interface_bound
  // ran on every true assignment, and rebuild_true_interface_bounds() rescanned
  // EVERY theory atom on EVERY backtrack.
  //
  // That was the LassoRanker half of the division regression. On
  // dropbuf.t2.c_Iteration3_Loop_4-pieceTemplate a gprof profile put 62% of
  // runtime in notify_backtrack and 29% in 1.4 BILLION lookups into
  // true_lra_le_reasons_ -- about 150k lookups per backtrack, all discarded.
  // Gating it recovers the file: 200s timeout -> 12.2s, with the pivot and
  // conflict counts identical to the pre-regression commit. See
  // docs/nelson-oppen-qf-lra-regression.md.
  //
  // Defaults to false so the pure path pays nothing; the dispatcher turns it
  // on, which is precisely the path that consumes it.
  void set_interface_equalities_enabled(bool enabled) { interface_equalities_enabled_ = enabled; }

  struct InterfaceEqualityPropagation {
    TermPairKey pair;
    std::vector<sat::Lit> reasons;
  };

  std::vector<InterfaceEqualityPropagation> take_interface_equality_propagations() {
    std::vector<InterfaceEqualityPropagation> result;
    result.reserve(pending_interface_equalities_.size());
    while (!pending_interface_equalities_.empty()) {
      result.push_back(std::move(pending_interface_equalities_.front()));
      pending_interface_equalities_.pop_front();
    }
    return result;
  }

  bool cb_has_external_clause(bool& is_forgettable) {
    run_deferred_check_if_needed();
    refresh_model_phases_if_ready();
    if (!conflict_.has_value()) {
      return false;
    }
    external_clause_ = conflict_.value();
    external_clause_index_ = 0;
    is_forgettable = false;
    return true;
  }

  bool cb_has_external_clause() {
    run_deferred_check_if_needed();
    refresh_model_phases_if_ready();
    if (!conflict_.has_value()) {
      return false;
    }
    external_clause_ = conflict_.value();
    external_clause_index_ = 0;
    return true;
  }

  int cb_add_external_clause_lit() {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  bool is_observed_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < observed_vars_.size() &&
           observed_vars_[static_cast<std::size_t>(var)] != 0;
  }

  bool is_assigned_var(int var) const {
    return is_observed_var(var) && assignment_view().literal(var) != 0;
  }

  void publish_elimination_stats() {
    statistics_.set(util::StatKey::LraFixedVariables, tableau_.fixed_variable_count());
    statistics_.set(util::StatKey::LraEliminationPivots, tableau_.elimination_pivot_count());
  }

  void notify_assignment_lit(int lit) {
    if (conflict_.has_value()) {
      // Already found a conflict this round; CaDiCaL hasn't pulled it via
      // cb_has_external_clause yet, so further asserts would just be
      // wasted (and potentially confusing, since assert_lra_atom_bound
      // isn't meant to be called against an already-inconsistent tableau).
      return;
    }
    const int var = lit > 0 ? lit : -lit;
    if (!is_observed_var(var)) {
      return;
    }
    const std::size_t atom_index = atom_index_by_var_[static_cast<std::size_t>(var)];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
    if (!prepared_atom.is_arithmetic) {
      return;
    }
    const bool bound_var_was_basic = tableau_.is_basic(prepared_atom.bound_var);
    const theory::arithmetic::DeltaRational bound_var_assignment_before =
        tableau_.assignment(prepared_atom.bound_var);
    const std::size_t elimination_pivots_before = tableau_.elimination_pivot_count();
    const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
    const bool atom_value = atom.lit.value > 0 ? lit > 0 : lit < 0;
    const AssignedTheoryAtom assigned =
        assigned_theory_atom(PureTheoryFilterMode::Off, atom, atom_value);
    const sat::Lit reason = blocking_lit(assigned);
    const auto direct_conflict = assert_lra_atom_bound(
        tableau_, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom_value,
        prepared_atom.asserts_lower_when_true, reason, prepared_atom.integer_rounding);
    // Item H7's counters move inside assert_upper/assert_lower, not inside
    // check(), so they are published here rather than alongside the pivot
    // counters in run_deferred_check_if_needed(). They are cumulative
    // totals, so set() (not increment()) is what stays correct across the
    // repeated calls -- and this is the only place they can change.
    publish_elimination_stats();
    if (direct_conflict.has_value()) {
      conflict_ = direct_conflict;
      // needs_check_ may already be true from an earlier, non-conflicting
      // literal in this same batch -- clear it, or run_deferred_check_if_
      // needed() would later overwrite this genuine direct conflict with
      // a stale tableau_.check() result. (The offending bound itself was
      // never actually stored by assert_upper/assert_lower's own direct-
      // conflict branch, so check() alone wouldn't even rediscover it.)
      needs_check_ = false;
      return;
    }
    if (tableau_.elimination_pivot_count() != elimination_pivots_before) {
      mark_all_model_phases_dirty();
    } else if (!bound_var_was_basic &&
               tableau_.assignment(prepared_atom.bound_var) != bound_var_assignment_before) {
      mark_model_phase_bound_var_dirty(prepared_atom.bound_var);
      for (const theory::arithmetic::LraVarId affected_row :
           tableau_.occurs_in(prepared_atom.bound_var)) {
        mark_model_phase_bound_var_dirty(affected_row);
      }
    }
    if (propagation_enabled_) {
      scan_for_propagations(atom_index, prepared_atom.bound_var);
      if (row_propagation_enabled_) {
        scan_row_based_propagations(prepared_atom.bound_var);
      }
    }
    if (atom_value) {
      observe_true_interface_bound(atom_index, reason);
    }
    // Defer the expensive Check/Pivot repair rather than running it
    // eagerly after every single assignment (decisions *and* propagated
    // literals both land here): notify_assignment_lit can be called
    // tens of thousands of times more often than CaDiCaL actually needs
    // a verdict from us. Verified against CaDiCaL's own source
    // (contrib/src/cadical/src/{internal,external_propagate}.cpp): with
    // this propagator in eager mode (is_lazy left at its default
    // `false`), cb_has_external_clause() is asked exactly once per
    // quiescence point (once per decision plus once per conflict/
    // backjump, not once per literal), and cb_propagate() is always
    // fully drained *before* it's asked -- so same-slack/row-based
    // propagation above (which reads only bound state, never the
    // current assignment) is completely unaffected by deferring this.
    // See run_deferred_check_if_needed(), called from both
    // cb_has_external_clause() overloads.
    needs_check_ = true;
  }

  // The actual Check/Pivot repair, run lazily right before CaDiCaL is
  // told whether there's a conflict clause to add -- see
  // notify_assignment_lit's own comment for why this specific hook
  // point is both correct (propagation doesn't depend on it) and the
  // right frequency (once per quiescence point, not once per literal).
  void run_deferred_check_if_needed() {
    if (!needs_check_) {
      return;
    }
    needs_check_ = false;
    // Belt-and-suspenders: notify_assignment_lit's direct-conflict branch
    // already clears needs_check_ itself (see its own comment), but never
    // overwrite an already-discovered conflict with a fresh
    // tableau_.check() result regardless -- tableau_.check() only sees
    // whatever bounds actually made it into the tableau, and a rejected
    // direct-conflict bound never does.
    if (conflict_.has_value()) {
      return;
    }
    const std::size_t fallbacks_before = tableau_.bland_fallback_count();
    const std::size_t multi_row_conflicts_before = tableau_.soi_multi_row_conflict_count();
    const std::size_t minimize_calls_before = tableau_.soi_minimize_call_count();
    const std::size_t minimize_shrunk_before = tableau_.soi_minimize_shrunk_count();
    const std::size_t conflict_lits_before = tableau_.soi_conflict_lit_count();
    const std::size_t pivots_before = tableau_.pivot_count();
    const std::size_t row_updates_before = tableau_.row_update_count();
    const std::size_t term_ops_before = tableau_.row_term_op_count();
    statistics_.increment(kStatLraOnlineCheckCalls);
    conflict_ = tableau_.check();
    const std::size_t pivot_delta = tableau_.pivot_count() - pivots_before;
    statistics_.increment(kStatLraBlandFallbacks,
                          tableau_.bland_fallback_count() - fallbacks_before);
    statistics_.increment(kStatLraSoiMultiRowConflicts,
                          tableau_.soi_multi_row_conflict_count() - multi_row_conflicts_before);
    statistics_.increment(kStatLraSoiMinimizeCalls,
                          tableau_.soi_minimize_call_count() - minimize_calls_before);
    statistics_.increment(kStatLraSoiMinimizeShrunk,
                          tableau_.soi_minimize_shrunk_count() - minimize_shrunk_before);
    statistics_.increment(kStatLraSoiConflictLits,
                          tableau_.soi_conflict_lit_count() - conflict_lits_before);
    statistics_.increment(kStatLraPivots, pivot_delta);
    statistics_.increment(kStatLraPivotRowUpdates,
                          tableau_.row_update_count() - row_updates_before);
    statistics_.increment(kStatLraPivotTermOps, tableau_.row_term_op_count() - term_ops_before);
    if (!conflict_.has_value() && pivot_delta != 0) {
      mark_all_model_phases_dirty();
    }
  }

  std::optional<sat::Lit> model_phase_literal(std::size_t atom_index) const {
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
    if (!prepared_atom.is_arithmetic ||
        prepared_atom.relation == theory::arithmetic::LraRelation::Eq) {
      return std::nullopt;
    }
    const BoundAssertion true_assertion = bound_assertion(
        prepared_atom.relation, prepared_atom.rhs, prepared_atom.asserts_lower_when_true,
        /*atom_value=*/true, prepared_atom.integer_rounding);
    const theory::arithmetic::DeltaRational& beta = tableau_.assignment(prepared_atom.bound_var);
    const bool atom_holds =
        true_assertion.is_upper ? beta <= true_assertion.value : beta >= true_assertion.value;
    const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
    return sat::Lit{atom_holds ? atom.lit.value : -atom.lit.value};
  }

  void refresh_model_phases_if_ready() {
    if (conflict_.has_value() || !model_phase_enabled_ ||
        (!model_phase_all_dirty_ && model_phase_dirty_bound_var_list_.empty())) {
      return;
    }
    refresh_model_phases();
  }

  void refresh_model_phases() {
    if (!model_phase_enabled_) {
      return;
    }
    statistics_.increment(kStatLraModelPhaseRefreshes);
    if (model_phase_all_dirty_) {
      for (std::size_t atom_index = 0; atom_index < theory_atoms_.size(); ++atom_index) {
        refresh_model_phase_atom(atom_index);
      }
    } else {
      for (const theory::arithmetic::LraVarId bound_var : model_phase_dirty_bound_var_list_) {
        const auto found = prepared_.atoms_by_bound_var.find(bound_var);
        if (found == prepared_.atoms_by_bound_var.end()) {
          continue;
        }
        for (const std::size_t atom_index : found->second) {
          refresh_model_phase_atom(atom_index);
        }
      }
    }
    clear_model_phase_dirty_vars();
  }

  void refresh_model_phase_atom(std::size_t atom_index) {
    const std::optional<sat::Lit> phase_lit = model_phase_literal(atom_index);
    if (!phase_lit.has_value()) {
      return;
    }
    const int var = phase_lit->value > 0 ? phase_lit->value : -phase_lit->value;
    if (model_phase_by_var_[static_cast<std::size_t>(var)] == phase_lit->value) {
      return;
    }
    model_phase_by_var_[static_cast<std::size_t>(var)] = phase_lit->value;
    publish_phase_(*phase_lit);
    statistics_.increment(kStatLraModelPhaseHints);
  }

  void mark_model_phase_bound_var_dirty(theory::arithmetic::LraVarId bound_var) {
    if (!model_phase_enabled_ || model_phase_all_dirty_ ||
        bound_var >= model_phase_dirty_bound_vars_.size() ||
        model_phase_dirty_bound_vars_[bound_var] != 0) {
      return;
    }
    model_phase_dirty_bound_vars_[bound_var] = 1;
    model_phase_dirty_bound_var_list_.push_back(bound_var);
  }

  void mark_all_model_phases_dirty() {
    if (!model_phase_enabled_) {
      return;
    }
    clear_model_phase_dirty_vars();
    model_phase_all_dirty_ = true;
  }

  void clear_model_phase_dirty_vars() {
    for (const theory::arithmetic::LraVarId bound_var : model_phase_dirty_bound_var_list_) {
      model_phase_dirty_bound_vars_[bound_var] = 0;
    }
    model_phase_dirty_bound_var_list_.clear();
    model_phase_all_dirty_ = false;
  }

  void observe_true_interface_bound(std::size_t atom_index, sat::Lit reason) {
    if (!interface_equalities_enabled_) {
      return;
    }
    const TermPairKey pair = prepared_.atoms[atom_index].non_strict_le_pair;
    if (pair.lhs == core::kInvalidTermId) {
      return;
    }
    true_lra_le_reasons_[pair] = reason;
    enqueue_lra_interface_equality_if_forced(pair);
  }

  void rebuild_true_interface_bounds() {
    if (!interface_equalities_enabled_) {
      return;
    }
    true_lra_le_reasons_.clear();
    for (std::size_t atom_index = 0; atom_index < theory_atoms_.size(); ++atom_index) {
      const TermPairKey pair = prepared_.atoms[atom_index].non_strict_le_pair;
      if (pair.lhs == core::kInvalidTermId) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        continue;
      }
      const int assigned_lit = assignment_view().literal(var);
      const bool atom_value = atom.lit.value > 0 ? assigned_lit > 0 : assigned_lit < 0;
      if (atom_value) {
        true_lra_le_reasons_[pair] = sat::Lit{-assigned_lit};
      }
    }
  }

  void enqueue_lra_interface_equality_if_forced(const TermPairKey& pair) {
    const auto forward = true_lra_le_reasons_.find(pair);
    if (forward == true_lra_le_reasons_.end()) {
      return;
    }
    const auto reverse = true_lra_le_reasons_.find(TermPairKey{pair.rhs, pair.lhs});
    if (reverse == true_lra_le_reasons_.end()) {
      return;
    }
    const TermPairKey canonical_pair = pair.lhs < pair.rhs ? pair : TermPairKey{pair.rhs, pair.lhs};
    if (!published_interface_pairs_.insert(canonical_pair).second) {
      return;
    }
    pending_interface_equalities_.push_back(
        InterfaceEqualityPropagation{pair, {forward->second, reverse->second}});
  }

  // Scans the other atoms sharing `slack` with the atom that was just
  // asserted (index `triggering_atom_index`, already applied to `tableau_`
  // with no direct conflict) for ones now entailed true/false by the
  // slack's current upper/lower bound (Dutertre & de Moura Section 4.2).
  // Only needs to look at this one slack: bounds only ever change via
  // explicit assert_upper/assert_lower (never via check()'s pivots, which
  // only touch assignments/row structure), so no other slack's bounds
  // could have changed as a side effect of this assertion.
  void scan_for_propagations(std::size_t triggering_atom_index,
                             theory::arithmetic::LraVarId bound_var) {
    const auto found = prepared_.atoms_by_bound_var.find(bound_var);
    if (found == prepared_.atoms_by_bound_var.end()) {
      return;
    }
    for (const std::size_t atom_index : found->second) {
      if (atom_index == triggering_atom_index) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      const auto entails_true =
          bound_entails(tableau_, prepared_atom.bound_var,
                        bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                        prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                                        prepared_atom.integer_rounding));
      if (entails_true.has_value()) {
        pending_.push_back(PendingPropagation{atom.lit.value, {*entails_true}, false});
        continue;
      }
      const auto entails_false =
          bound_entails(tableau_, prepared_atom.bound_var,
                        bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                        prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                                        prepared_atom.integer_rounding));
      if (entails_false.has_value()) {
        pending_.push_back(PendingPropagation{-atom.lit.value, {*entails_false}, false});
      }
    }
  }

  // Whether the implied bound `implied` on `basic_var` is tight enough to
  // entail the atom whose truth assignment would impose `assertion`. Shares
  // bound_assertion()'s direction convention, so a flipped atom's TRUE case
  // is checked against an implied *lower* bound and vice versa.
  std::optional<std::vector<sat::Lit>> implied_bound_entails(theory::arithmetic::LraVarId basic_var,
                                                             const BoundAssertion& assertion) {
    auto implied =
        compute_implied_bound(tableau_, basic_var, assertion.is_upper, row_propagation_limits_);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // Row-based/interval bound propagation (Dutertre & de Moura Section 4,
  // generalizing scan_for_propagations to a *derived* rather than
  // directly-asserted bound): `changed_var`'s bound was just tightened, so
  // any row currently mentioning it (tableau_.occurs_in) could now have a
  // tighter implied bound. For each such row, compute the implied
  // upper/lower via compute_implied_bound and check entailment for the atoms
  // bounding that row's own basic variable.
  //
  // Before item H1 this could only trigger when `changed_var` happened to be
  // a slack wrapping exactly one coefficient-1 variable, since bounds only
  // ever lived on slacks; now that single-variable atoms bound their variable
  // directly, any bound change is a candidate and the fanout cap below is
  // what keeps the cost bounded.
  void scan_row_based_propagations(theory::arithmetic::LraVarId changed_var) {
    const std::vector<theory::arithmetic::LraVarId>& affected_rows =
        tableau_.occurs_in(changed_var);
    if (affected_rows.empty() || affected_rows.size() > row_propagation_limits_.max_fanout) {
      return;
    }
    for (const theory::arithmetic::LraVarId basic_var : affected_rows) {
      const auto found = prepared_.atoms_by_bound_var.find(basic_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_bound_entails(
            basic_var, bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                       prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                                       prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true});
          continue;
        }
        reasons = implied_bound_entails(
            basic_var, bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                       prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                                       prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true});
        }
      }
      if (bidirectional_row_propagation_enabled_ &&
          affected_rows.size() <= row_propagation_limits_.bidirectional_max_fanout) {
        scan_row_child_propagations(basic_var, changed_var);
      }
    }
  }

  // Competitiveness-roadmap item 7 (the reverse direction of
  // scan_row_based_propagations): for each *other* term of basic_var's
  // row (skipping exclude_var, whose own bound is what just triggered
  // this scan -- nothing new to derive about it from itself), check
  // whether compute_implied_child_bound now entails one of that term's
  // own atoms. Gated by its own (smaller) size caps -- see their comment.
  void scan_row_child_propagations(theory::arithmetic::LraVarId basic_var,
                                   theory::arithmetic::LraVarId exclude_var) {
    const auto& row = tableau_.row(basic_var);
    if (row.size() > row_propagation_limits_.bidirectional_max_row_size) {
      return;
    }
    for (const auto& [target_var, target_coeff, target_var_slot] : row) {
      if (target_var == exclude_var) {
        continue;
      }
      const auto found = prepared_.atoms_by_bound_var.find(target_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/true,
                            prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true, true});
          continue;
        }
        reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/false,
                            prepared_atom.integer_rounding));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true, true});
        }
      }
    }
  }

  // implied_bound_entails' counterpart for the reverse (child-tightening)
  // direction -- same convention, same all-or-nothing behaviour.
  std::optional<std::vector<sat::Lit>> implied_child_bound_entails(
      theory::arithmetic::LraVarId basic_var, theory::arithmetic::LraVarId target_var,
      const util::Rational& target_coeff, const BoundAssertion& assertion) {
    auto implied = compute_implied_child_bound(tableau_, basic_var, target_var, target_coeff,
                                               assertion.is_upper, row_propagation_limits_);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // A queued propagation candidate: the CaDiCaL literal to propagate, its
  // antecedent reason literals (one for same-slack propagation; up to a
  // row's term count for row-based propagation), and whether it came from
  // the row-based scan (for the LraRowPropagations breakdown stat).
  struct PendingPropagation {
    int lit;
    std::vector<sat::Lit> reasons;
    bool row_based;
    bool bidirectional = false;  // competitiveness-roadmap item 7
  };

  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  const PreparedLraTheory& prepared_;
  theory::arithmetic::LraTableau tableau_;
  bool propagation_enabled_;
  bool row_propagation_enabled_;
  bool bidirectional_row_propagation_enabled_;
  // The caps enforced by scan_row_based_propagations and the two
  bool theory_decisions_enabled_;
  bool model_phase_enabled_;
  bool model_phase_backtrack_may_change_beta_;
  // compute_implied_*_bound helpers. Held rather than read from options at
  // each use so the propagator stays independent of CheckSatOptions.
  LraRowPropagationLimits row_propagation_limits_;
  std::function<void(sat::Lit)> publish_phase_;
  util::Statistics& statistics_;
  std::vector<char> observed_vars_;
  std::vector<int> model_phase_by_var_;
  std::vector<char> model_phase_dirty_bound_vars_;
  std::vector<theory::arithmetic::LraVarId> model_phase_dirty_bound_var_list_;
  std::vector<std::size_t> atom_index_by_var_;
  std::vector<std::size_t> checkpoints_;
  std::deque<PendingPropagation> pending_;
  std::deque<InterfaceEqualityPropagation> pending_interface_equalities_;
  bool interface_equalities_enabled_ = false;
  std::unordered_map<TermPairKey, sat::Lit, TermPairKeyHash> true_lra_le_reasons_;
  std::unordered_set<TermPairKey, TermPairKeyHash> published_interface_pairs_;
  std::unordered_map<int, std::vector<sat::Lit>> reasons_;
  int reason_stream_active_lit_ = 0;
  int reason_stream_index_ = 0;
  std::optional<sat::Clause> conflict_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
  // See notify_assignment_lit's comment: set instead of running
  // tableau_.check() eagerly per literal, resolved lazily by
  // run_deferred_check_if_needed() (called from cb_has_external_clause()).
  bool needs_check_ = false;
  bool model_phase_all_dirty_ = true;
  // cb_decide()'s rotating position (competitiveness-roadmap item 8):
  // gives cb_decide() a stable, persistent round-robin order over
  // arithmetic atoms across calls, rather than always restarting from
  // index 0. Not reset on backtrack -- it's just a decision-order cursor,
  // not trail state; a stale position after backtracking is harmless
  // (worst case, this decision's order is less well-chosen than usual).
  std::size_t decide_cursor_ = 0;
  sat::TrailView assignment_view_;
};

#endif

class EufComponentAdapter final : public theory::TheorySolver {
 public:
  EufComponentAdapter(const core::TermManager& terms,
                      std::vector<BooleanClausifier::TheoryAtom> atoms, core::TermId true_term,
                      core::TermId false_term, core::SortId shared_sort,
                      std::vector<core::TermId> shared_terms, std::uint64_t assignment_epoch,
                      bool produce_model_projection, EufExplainSearchMode explain_search_mode,
                      bool proof_forest_diagnostics, util::Statistics& statistics,
                      std::string_view check_timer)
      : properties_{.id = "euf",
                    .shared_sorts = {shared_sort},
                    .stably_infinite = theory::StableInfiniteness::Yes,
                    .convexity = theory::Convexity::Convex,
                    .produces_model_projection = produce_model_projection,
                    .propagates_equalities = true,
                    .supplies_care_pairs = true},
        terms_(terms),
        atoms_(std::move(atoms)),
        prepared_(terms, atoms_, true_term, false_term, explain_search_mode,
                  proof_forest_diagnostics),
        shared_sort_(shared_sort),
        shared_terms_(std::move(shared_terms)),
        assignment_epoch_(assignment_epoch),
        produce_model_projection_(produce_model_projection),
        statistics_(statistics),
        check_timer_(check_timer) {}

  const theory::TheoryProperties& properties() const override { return properties_; }

  void register_interface_extensions() {
    for (const auto term : shared_terms_) {
      static_cast<void>(prepared_.flat.flatten(term));
    }
    prepared_.ensure_theory_atoms(terms_, {});
  }

  void set_assignment_epoch(std::uint64_t epoch) { assignment_epoch_ = epoch; }

  void append_atom(const BooleanClausifier::TheoryAtom& atom) {
    const std::vector<BooleanClausifier::TheoryAtom> appended{atom};
    if (prepared_.ensure_theory_atoms(terms_, appended) != 1) {
      throw std::logic_error("duplicate EUF component atom during incremental growth");
    }
    atoms_.push_back(atom);
    last_care_token_.reset();
    last_care_pairs_.clear();
  }

  theory::TheoryCheckResult check(std::span<const theory::AssignedTheoryLiteral> assignment,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    ScopedStatisticsTimer timer(statistics_, check_timer_);
    static_cast<void>(arrangement);
    if (assignment.size() != atoms_.size()) {
      throw std::logic_error("EUF component assignment size mismatch");
    }
    std::vector<AssignedTheoryAtom> assigned;
    assigned.reserve(assignment.size());
    for (std::size_t index = 0; index < assignment.size(); ++index) {
      assigned.push_back(
          assigned_theory_atom(PureTheoryFilterMode::Off, atoms_[index], assignment[index].value));
    }
    if (!produce_model_projection_) {
      const std::optional<sat::Clause> conflict =
          find_incremental_theory_conflict(prepared_, assigned, statistics_);
      if (conflict.has_value()) {
        theory::TheoryCheckResult result;
        result.status = theory::TheoryCheckStatus::LearnedClause;
        result.clause = std::move(conflict.value());
        return result;
      }
      theory::TheoryCheckResult result;
      result.status = theory::TheoryCheckStatus::Consistent;
      return result;
    }
    EufClosureCheckResult checked = check_incremental_euf_closure(prepared_, assigned, statistics_);
    if (checked.conflict.has_value()) {
      return theory::TheoryCheckResult{.status = theory::TheoryCheckStatus::LearnedClause,
                                       .clause = std::move(checked.conflict.value()),
                                       .model = {},
                                       .entailed_lit = {},
                                       .reasons = {},
                                       .decision = {},
                                       .diagnostic = {}};
    }
    FlatEufModelView model_view{prepared_.flat, checked.closure};
    const auto projection =
        project_euf_shared_model(model_view, shared_terms_, shared_sort_, assignment_epoch_);
    if (!projection.has_value()) {
      return theory::TheoryCheckResult{.status = theory::TheoryCheckStatus::Unknown,
                                       .clause = {},
                                       .model = {},
                                       .entailed_lit = {},
                                       .reasons = {},
                                       .decision = {},
                                       .diagnostic = "EUF model omitted a shared Int term"};
    }
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::Consistent;
    result.model = std::move(projection.value());
    const EufCarePairDiagnostics care = euf_care_pair_diagnostics(model_view, shared_terms_);
    publish_euf_care_pair_diagnostics(care, /*computed=*/true, statistics_);
    last_care_pairs_.clear();
    last_care_pairs_.reserve(care.term_pairs.size());
    for (const TermPairKey& pair : care.term_pairs) {
      last_care_pairs_.push_back(
          theory::InterfacePair{.lhs = pair.lhs, .rhs = pair.rhs, .sort = shared_sort_});
    }
    last_care_token_ = result.model.token;
    return result;
  }

  std::vector<theory::InterfacePair> care_pairs(
      const theory::TheoryModelToken& token) const override {
    return last_care_token_.has_value() && last_care_token_.value() == token
               ? last_care_pairs_
               : std::vector<theory::InterfacePair>{};
  }

 private:
  theory::TheoryProperties properties_;
  const core::TermManager& terms_;
  std::vector<BooleanClausifier::TheoryAtom> atoms_;
  PreparedFlatTheory prepared_;
  core::SortId shared_sort_ = core::kInvalidSortId;
  std::vector<core::TermId> shared_terms_;
  std::uint64_t assignment_epoch_ = 0;
  bool produce_model_projection_ = false;
  util::Statistics& statistics_;
  std::string_view check_timer_;
  std::optional<theory::TheoryModelToken> last_care_token_;
  std::vector<theory::InterfacePair> last_care_pairs_;
};

class LiaComponentAdapter final : public theory::TheorySolver {
 public:
  LiaComponentAdapter(const core::TermManager& terms,
                      std::vector<BooleanClausifier::TheoryAtom> atoms, core::SortId shared_sort,
                      std::vector<core::TermId> shared_terms, std::uint64_t assignment_epoch,
                      bool produce_model_projection, const CheckSatOptions& options,
                      LiaSettings settings, util::Statistics& statistics, LiaComponentTiming timing)
      : properties_{.id = "lia",
                    .shared_sorts = {shared_sort},
                    .stably_infinite = theory::StableInfiniteness::Yes,
                    .convexity = theory::Convexity::NonConvex,
                    .produces_model_projection = produce_model_projection,
                    .propagates_equalities = options.qf_uflia_tableau_equalities,
                    .supplies_care_pairs = false},
        terms_(terms),
        atoms_(std::move(atoms)),
        shared_sort_(shared_sort),
        shared_terms_(std::move(shared_terms)),
        assignment_epoch_(assignment_epoch),
        produce_model_projection_(produce_model_projection),
        propagate_equalities_(options.qf_uflia_tableau_equalities),
        equality_policy_(options.qf_uflia_equality_policy),
        equality_producer_(options.qf_uflia_lia_equality_producer),
        prepared_(terms_, atoms_, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
                  options.lra_sparse_leaving, options.lra_fixed_elimination,
                  /*collect_model_variables=*/true, options.lra_least_violated_leaving,
                  lra_tuning_from_options(options.lra_tuning),
                  IntegerPreparationOptions{.round_bounds = settings.round_integer_bounds,
                                            .primitive_rows = settings.primitive_int_rows}),
        settings_(settings),
        statistics_(statistics),
        timing_(timing) {
    integer_input_vars_.reserve(prepared_.variables_by_term.size());
    for (const auto& [term, variable] : prepared_.variables_by_term) {
      if (terms_.get(term).sort == terms_.symbols().int_sort()) {
        integer_input_vars_.push_back(variable);
      }
    }
    rebuild_integer_obligations();
    publish_integer_preparation_stats();
  }

  const theory::TheoryProperties& properties() const override { return properties_; }

  void register_interface_extensions() {
    for (const auto term : shared_terms_) {
      if (!prepared_.var_ids_.contains(term)) {
        const auto variable = prepared_.base_tableau.add_variable();
        prepared_.var_ids_.emplace(term, variable);
        prepared_.variables_by_term.emplace(term, variable);
        integer_input_vars_.push_back(variable);
        rebuild_integer_obligations();
      }
    }
  }

  void set_assignment_epoch(std::uint64_t epoch) { assignment_epoch_ = epoch; }

  void append_atom(const BooleanClausifier::TheoryAtom& atom) {
    atoms_.push_back(atom);
    const std::vector<theory::arithmetic::LraVarId> new_model_variables =
        prepared_.append_atom(atom);
    integer_input_vars_.insert(integer_input_vars_.end(), new_model_variables.begin(),
                               new_model_variables.end());
    rebuild_integer_obligations();
    publish_integer_preparation_stats();
    last_model_.reset();
  }

  theory::TheoryCheckResult check(std::span<const theory::AssignedTheoryLiteral> assignment,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    ScopedStatisticsTimer timer(statistics_, timing_.check);
    last_model_.reset();
    if (assignment.size() != atoms_.size()) {
      throw std::logic_error("LIA component assignment size mismatch");
    }
    auto rational = check_lia_component_relaxation(prepared_, assignment, statistics_, timing_);
    if (rational.conflict.has_value()) {
      return translate(
          {LiaComponentCheck::Status::Unsat, std::move(*rational.conflict), std::nullopt});
    }
    if (auto propagated = explained_equalities(rational.tableau, assignment, arrangement)) {
      return std::move(*propagated);
    }
    return translate(complete_lia_integer_model(std::move(rational.tableau), prepared_, assignment,
                                                integer_vars_, settings_, statistics_, timing_));
  }

  const PreparedLraTheory& prepared() const { return prepared_; }
  const theory::arithmetic::LraTableau& model() const { return last_model_.value(); }

  theory::TheoryCheckResult complete_integer_model(
      std::span<const theory::AssignedTheoryLiteral> assignment,
      const theory::arithmetic::LraTableau& rational_model,
      std::span<const theory::InterfaceRelation> arrangement) {
    ScopedStatisticsTimer timer(statistics_, timing_.check);
    last_model_.reset();
    if (assignment.size() != atoms_.size()) {
      throw std::logic_error("LIA component assignment size mismatch");
    }
    if (propagate_equalities_ && equality_producer_ == QfUfliaEqualityProducer::ModelUnit &&
        !shared_terms_.empty()) {
      // Section 3.2's model-value filter assumes a feasible assignment.
      // A sparse bounds-only pop need not restore the old assignment/basis;
      // repair an owned snapshot rather than relying on callback history.
      // This same snapshot is moved into completion if discovery returns none.
      auto feasible = [&] {
        ScopedStatisticsTimer repair_timer(statistics_, ":time.lia.equality-feasibility-us");
        return theory::arithmetic::LraTableau(rational_model);
      }();
      std::optional<sat::Clause> conflict;
      {
        ScopedStatisticsTimer repair_timer(statistics_, ":time.lia.equality-feasibility-us");
        const auto pivots_before = feasible.pivot_count();
        conflict = feasible.check();
        statistics_.increment(":lia.equality.model-unit.feasibility-checks");
        statistics_.increment(":lia.equality.model-unit.feasibility-pivots",
                              feasible.pivot_count() - pivots_before);
      }
      if (conflict) {
        return translate({LiaComponentCheck::Status::Unsat, std::move(*conflict), std::nullopt});
      }
      if (auto propagated = explained_equalities(feasible, assignment, arrangement)) {
        return std::move(*propagated);
      }
      statistics_.increment(":combination.online-components.integer-completions");
      return translate(complete_lia_integer_model(std::move(feasible), prepared_, assignment,
                                                  integer_vars_, settings_, statistics_, timing_));
    }
    if (auto propagated = explained_equalities(rational_model, assignment, arrangement)) {
      return std::move(*propagated);
    }
    statistics_.increment(":combination.online-components.integer-completions");
    LiaComponentCheck result = complete_lia_integer_model(
        rational_model, prepared_, assignment, integer_vars_, settings_, statistics_, timing_);
    return translate(std::move(result));
  }

 private:
  // Original integration design: docs/implementation/uflia-equality-propagation.md.
  // Only called on the rationally feasible tableau BEFORE completion can add
  // cut/split bounds with synthetic reasons. Reuse our existing explained rules.
  std::optional<theory::TheoryCheckResult> explained_equalities(
      const theory::arithmetic::LraTableau& tableau,
      std::span<const theory::AssignedTheoryLiteral> assignment,
      std::span<const theory::InterfaceRelation> arrangement) {
    if (!propagate_equalities_ || shared_terms_.empty()) return std::nullopt;
    ScopedStatisticsTimer timer(statistics_, ":time.lia.equality-scan-us");
    statistics_.increment(":lia.equality.scans");
    std::vector<theory::arithmetic::LraVarId> variables;
    std::unordered_map<theory::arithmetic::LraVarId, core::TermId> term_by_variable;
    for (const auto term : shared_terms_) {
      const auto found = prepared_.variables_by_term.find(term);
      if (found == prepared_.variables_by_term.end())
        throw std::logic_error("shared Int term missing from LIA preparation");
      variables.push_back(found->second);
      term_by_variable.emplace(found->second, term);
    }
    std::set<std::pair<core::TermId, core::TermId>> known_true;
    std::set<std::pair<core::TermId, core::TermId>> known_named;
    for (const auto& relation : arrangement) {
      if (equality_policy_ == QfUfliaEqualityPolicy::Names) {
        known_named.emplace(std::min(relation.lhs, relation.rhs),
                            std::max(relation.lhs, relation.rhs));
      }
      if (relation.value == theory::InterfaceRelationValue::Equal) {
        known_true.emplace(std::min(relation.lhs, relation.rhs),
                           std::max(relation.lhs, relation.rhs));
      }
    }
    std::unordered_set<int> active;
    for (const auto& atom : assignment)
      active.insert(atom.value ? atom.lit.value : -atom.lit.value);
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::EntailedEqualities;
    std::vector<theory::arithmetic::LraTableau::EntailedEquality> equalities;
    {
      ScopedStatisticsTimer discovery_timer(statistics_, ":time.lia.equality-discovery-us");
      if (equality_producer_ == QfUfliaEqualityProducer::ModelUnit) {
        // Bjorner/Nachmanson, CAV 2024 Section 3.2. Equal model values only
        // filter candidates; fixed/unit-row identities justify every proof.
        // The same rational entailments hold in the subset of integer models.
        auto query = tableau.model_filtered_unit_equalities(variables);
        statistics_.increment(":lia.equality.model-unit.selected");
        statistics_.increment(":lia.equality.model-unit.classes", query.stats.model_classes);
        statistics_.increment(":lia.equality.model-unit.class-terms", query.stats.class_terms);
        statistics_.increment(":lia.equality.model-unit.candidate-pairs",
                              query.stats.candidate_pairs);
        statistics_.increment(":lia.equality.model-unit.rows-inspected",
                              query.stats.rows_inspected);
        statistics_.increment(":lia.equality.model-unit.nonzeros-inspected",
                              query.stats.nonzeros_inspected);
        statistics_.increment(":lia.equality.model-unit.fixed-matches", query.stats.fixed_matches);
        statistics_.increment(":lia.equality.model-unit.one-row-matches",
                              query.stats.one_row_matches);
        statistics_.increment(":lia.equality.model-unit.two-row-matches",
                              query.stats.two_row_matches);
        statistics_.increment(":lia.equality.model-unit.nonunit-rejections",
                              query.stats.nonunit_rejections);
        statistics_.increment(":lia.equality.model-unit.nonfixed-row-rejections",
                              query.stats.nonfixed_row_rejections);
        statistics_.increment(":lia.equality.model-unit.reason-lits", query.stats.reason_literals);
        statistics_.increment(":lia.equality.legacy-scans-skipped");
        equalities = std::move(query.equalities);
      } else {
        statistics_.increment(":lia.equality.legacy-producer-selected");
        equalities = tableau.entailed_equalities(variables);
      }
    }
    for (const auto& equality : equalities) {
      statistics_.increment(":lia.equality.candidates");
      using Rule = theory::arithmetic::LraTableau::EntailedEqualityRule;
      if (equality_policy_ == QfUfliaEqualityPolicy::Fixed && equality.rule != Rule::FixedEq) {
        statistics_.increment(":lia.equality.filtered-rule");
        continue;
      }
      const auto lhs = term_by_variable.at(equality.lhs);
      const auto rhs = term_by_variable.at(equality.rhs);
      const std::pair pair{std::min(lhs, rhs), std::max(lhs, rhs)};
      if (equality_policy_ == QfUfliaEqualityPolicy::Names && known_named.contains(pair)) {
        statistics_.increment(":lia.equality.already-named");
        continue;
      }
      if (known_true.contains(pair)) {
        statistics_.increment(":lia.equality.already-true");
        continue;
      }
      theory::ExplainedEquality consequence;
      consequence.pair = {pair.first, pair.second, shared_sort_};
      for (const auto blocking : equality.reasons) {
        if (blocking.value == 0 || !active.contains(-blocking.value))
          throw std::logic_error("LIA equality has a derived or inactive bound reason");
        consequence.reasons.push_back(sat::Lit{-blocking.value});
      }
      std::sort(consequence.reasons.begin(), consequence.reasons.end(),
                [](sat::Lit a, sat::Lit b) { return a.value < b.value; });
      consequence.reasons.erase(
          std::unique(consequence.reasons.begin(), consequence.reasons.end(),
                      [](sat::Lit a, sat::Lit b) { return a.value == b.value; }),
          consequence.reasons.end());
      statistics_.increment(":lia.equality.reason-lits", consequence.reasons.size());
      switch (equality.rule) {
        case Rule::FixedEq:
          statistics_.increment(":lia.equality.fixed-eq");
          break;
        case Rule::EqRow:
          statistics_.increment(":lia.equality.eq-row");
          break;
        case Rule::EqOffsetRows:
          statistics_.increment(":lia.equality.eq-offset-rows");
          break;
        case Rule::EqRows:
          statistics_.increment(":lia.equality.eq-rows");
          break;
      }
      result.equalities.push_back(std::move(consequence));
    }
    if (result.equalities.empty()) return std::nullopt;
    // Original diagnostic design: uflia-equality-ablation.md. Discovery and
    // owning explanation translation above have no tableau/SAT side effects.
    if (equality_policy_ == QfUfliaEqualityPolicy::Scan) {
      statistics_.increment(":lia.equality.scan-only-proofs", result.equalities.size());
      return std::nullopt;
    }
    statistics_.increment(":lia.equality.proposed", result.equalities.size());
    return result;
  }

  // The integer obligations handed to completion. With integer_slacks set
  // the slacks the preparation proved integer come first, so branch-and-bound
  // (which splits the first fractional variable) can split a constraint
  // expression such as 2x + 3y before its variables; appended after the
  // inputs, a slack is integral whenever they are and would never be split.
  // Slacks are never in variables_by_term, so the list stays duplicate-free.
  void rebuild_integer_obligations() {
    integer_vars_.clear();
    if (settings_.integer_slacks) {
      integer_vars_.assign(prepared_.integer_slacks.begin(), prepared_.integer_slacks.end());
    }
    integer_vars_.insert(integer_vars_.end(), integer_input_vars_.begin(),
                         integer_input_vars_.end());
  }

  void publish_integer_preparation_stats() {
    statistics_.set(util::StatKey::LiaPrepIntegerSlacks, prepared_.integer_slacks.size());
    const auto& stats = prepared_.integer_stats;
    statistics_.set(util::StatKey::LiaPrepFractionalIntThresholds, stats.fractional_int_thresholds);
    statistics_.set(util::StatKey::LiaPrepNonIntegerIntSlackAtoms,
                    stats.non_integer_int_slack_atoms);
    statistics_.set(util::StatKey::LiaPrepNonIntegerIntSlacks, stats.non_integer_int_slacks);
  }
  theory::TheoryCheckResult translate(LiaComponentCheck result) {
    last_model_ = std::move(result.model);
    switch (result.status) {
      case LiaComponentCheck::Status::Sat:
        if (!produce_model_projection_) {
          theory::TheoryCheckResult check;
          check.status = theory::TheoryCheckStatus::Consistent;
          return check;
        }
        if (const auto projection = project_lia_integer_model(
                prepared_, last_model_.value(), shared_terms_, shared_sort_, assignment_epoch_);
            projection.has_value()) {
          theory::TheoryCheckResult check;
          check.status = theory::TheoryCheckStatus::Consistent;
          check.model = std::move(projection.value());
          return check;
        }
        return theory::TheoryCheckResult{.status = theory::TheoryCheckStatus::Unknown,
                                         .clause = {},
                                         .model = {},
                                         .entailed_lit = {},
                                         .reasons = {},
                                         .decision = {},
                                         .diagnostic = "LIA model omitted a shared Int term"};
      case LiaComponentCheck::Status::Unsat:
        return theory::TheoryCheckResult{.status = theory::TheoryCheckStatus::LearnedClause,
                                         .clause = std::move(result.conflict),
                                         .model = {},
                                         .entailed_lit = {},
                                         .reasons = {},
                                         .decision = {},
                                         .diagnostic = {}};
      case LiaComponentCheck::Status::ResourceLimit:
        return theory::TheoryCheckResult{.status = theory::TheoryCheckStatus::ResourceLimit,
                                         .clause = {},
                                         .model = {},
                                         .entailed_lit = {},
                                         .reasons = {},
                                         .decision = {},
                                         .diagnostic = "integer branch-and-bound node limit"};
    }
    throw std::logic_error("unreachable LIA component status");
  }

  theory::TheoryProperties properties_;
  const core::TermManager& terms_;
  std::vector<BooleanClausifier::TheoryAtom> atoms_;
  core::SortId shared_sort_ = core::kInvalidSortId;
  std::vector<core::TermId> shared_terms_;
  std::uint64_t assignment_epoch_ = 0;
  bool produce_model_projection_ = false;
  bool propagate_equalities_ = false;
  QfUfliaEqualityPolicy equality_policy_ = QfUfliaEqualityPolicy::All;
  QfUfliaEqualityProducer equality_producer_ = QfUfliaEqualityProducer::Legacy;
  PreparedLraTheory prepared_;
  std::vector<theory::arithmetic::LraVarId> integer_input_vars_;
  std::vector<theory::arithmetic::LraVarId> integer_vars_;
  // The resolved engine settings, held by value; the adapter does not read
  // CheckSatOptions for LIA settings.
  LiaSettings settings_;
  util::Statistics& statistics_;
  LiaComponentTiming timing_;
  std::optional<theory::arithmetic::LraTableau> last_model_;
};

// Component adapters own both full-model and online state. Their public
// boundary contains no SAT backend, tableau, or EUF closure. Legacy preparations
// remain implementation details until the frozen QF_UFLRA migration is allowed.
#if PRIMO_HAVE_IPASIR
class ComponentAdapterSupport : public theory::TheoryComponent {
 public:
  ComponentAdapterSupport(core::TermManager& terms, const CheckSatOptions& options,
                          util::Statistics& statistics, bool online, bool produce_model_projection)
      : terms_(terms),
        options_(options),
        statistics_(statistics),
        online_(online),
        produce_model_projection_(produce_model_projection) {}

  void bind_assignment_view(sat::TrailView view) override {
    theory::TheoryComponent::bind_assignment_view(view);
    assignment_.bind(view);
  }
  const theory::ComponentDescriptor& descriptor() const override { return descriptor_; }
  void register_interface(std::span<const theory::ComponentInterfaceScope> scopes) override {
    shared_.clear();
    for (const auto& scope : scopes) {
      if (scope.sort != terms_.symbols().int_sort()) {
        throw UnsupportedCombination("theory adapter supports shared Int only");
      }
      shared_ = scope.terms;
    }
    token_.reset();
  }

 protected:
  void append_atoms(std::span<const theory::ComponentAtom> atoms) {
    assignment_.register_atoms(atoms);
    for (const auto& atom : atoms) {
      atoms_.push_back({atom.lit, atom.term, atom.occurs_positive, atom.occurs_negative,
                        atom.relevant_at_both_polarities});
    }
    token_.reset();
  }
  void record_assignment(std::span<const theory::AssignedTheoryLiteral> delta) {
    assignment_.check_delta(delta);
    delta_literals_.clear();
    for (const auto& assigned : delta) {
      delta_literals_.push_back(assigned.value ? assigned.lit.value : -assigned.lit.value);
    }
    token_.reset();
  }
  theory::TheoryCheckResult stamp(theory::TheoryCheckResult result, std::uint64_t epoch) {
    if (result.status == theory::TheoryCheckStatus::Consistent) {
      result.model.token = {descriptor_.id, epoch, ++revision_};
      if (shared_.empty()) {
        result.model.partitions.clear();
      }
      token_ = result.model.token;
    }
    return result;
  }
  void require_token(const theory::TheoryModelToken& token) const {
    if (!token_.has_value() || *token_ != token) {
      throw core::ModelError("component export has a stale model token");
    }
  }
  template <typename Execution>
  static theory::TheoryCheckResult partial(Execution& execution) {
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::Consistent;
    bool forgettable = true;
    if (execution.cb_has_external_clause(forgettable)) {
      result.status = theory::TheoryCheckStatus::LearnedClause;
      while (const auto literal = execution.cb_add_external_clause_lit()) {
        result.clause.push_back(sat::Lit{literal});
      }
    }
    return result;
  }
  template <typename Execution>
  static std::optional<theory::TheoryCheckResult> consequence(Execution& execution) {
    const auto literal = execution.cb_propagate();
    if (literal == 0) {
      return std::nullopt;
    }
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::EntailedLiteral;
    result.entailed_lit = sat::Lit{literal};
    bool found_consequent = false;
    while (const auto reason = execution.cb_add_reason_clause_lit(literal)) {
      if (reason == literal) {
        found_consequent = true;
      } else {
        result.reasons.push_back(sat::Lit{-reason});
      }
    }
    if (!found_consequent) {
      throw std::logic_error("component implication omitted its consequent");
    }
    return result;
  }

  core::TermManager& terms_;
  CheckSatOptions options_;
  util::Statistics& statistics_;
  bool online_;
  bool produce_model_projection_;
  theory::ComponentDescriptor descriptor_;
  theory::ComponentAssignmentView assignment_;
  std::vector<BooleanClausifier::TheoryAtom> atoms_;
  std::vector<core::TermId> shared_;
  std::vector<int> delta_literals_;
  std::optional<theory::TheoryModelToken> token_;
  std::uint64_t revision_ = 0;
};

class EufTheoryComponent final : public ComponentAdapterSupport {
 public:
  EufTheoryComponent(core::TermManager& terms, const CheckSatOptions& options,
                     util::Statistics& statistics, bool online, bool produce_model_projection,
                     std::string_view check_timer)
      : ComponentAdapterSupport(terms, options, statistics, online, produce_model_projection),
        check_timer_(check_timer) {
    descriptor_.id = "euf";
    descriptor_.fragment = "uninterpreted symbols and equality; shared Int only";
    descriptor_.signature_families = {"uninterpreted-functions"};
    for (const auto& symbol : terms.symbols().symbols()) {
      if (symbol.kind != core::SymbolKind::Constant) {
        descriptor_.function_symbols.push_back(symbol.id);
      }
    }
    descriptor_.shared_sorts = {
        {terms.symbols().int_sort(), theory::StableInfiniteness::Yes, false}};
    descriptor_.convexity = theory::Convexity::Convex;
    descriptor_.capabilities = {
        .model_projection = true,
        .model_export = true,
        .model_mutation = false,
        .care_pairs = true,
        .incremental = true,
        .interface_extension = true,
        .check_on_full_model = online && options.euf_engine == EufEngine::Lazy,
        .equality_propagation = theory::EqualityPropagationStrength::Opportunistic};
  }
  void bind_assignment_view(sat::TrailView view) override {
    ComponentAdapterSupport::bind_assignment_view(view);
    if (execution_) execution_->bind_assignment_view(view);
  }
  bool owns_atom(core::TermId atom) const override { return is_euf_component_atom(terms_, atom); }
  std::vector<core::TermId> interface_variables(core::TermId atom) const override {
    std::unordered_set<core::TermId> variables;
    collect_constants_of_sort_in_term(terms_, atom, terms_.symbols().int_sort(), variables);
    std::vector<core::TermId> result(variables.begin(), variables.end());
    std::sort(result.begin(), result.end());
    return result;
  }
  core::TermId encode_equality(const theory::InterfacePair& pair) override {
    return terms_.mk_eq(pair.lhs, pair.rhs);
  }
  void register_atoms(std::span<const theory::ComponentAtom> atoms) override {
    execution_.reset();
    const auto previous = atoms_.size();
    append_atoms(atoms);
    if (!full_) {
      full_ = std::make_unique<EufComponentAdapter>(
          terms_, atoms_, terms_.mk_bool(true), terms_.mk_bool(false), terms_.symbols().int_sort(),
          shared_, 0, produce_model_projection_, options_.euf_explain_search,
          options_.euf_proof_forest_diagnostics, statistics_, check_timer_);
      full_->register_interface_extensions();
    } else {
      for (std::size_t index = previous; index < atoms_.size(); ++index) {
        full_->append_atom(atoms_[index]);
      }
    }
    reset();
  }
  void reset() override {
    token_.reset();
    execution_.reset();
    if (online_ && options_.euf_engine == EufEngine::Incremental) {
      online_options_ = options_;
      online_options_.euf_propagation =
          resolve_euf_propagation_mode(options_, atoms_.size(), statistics_);
      execution_ = std::make_unique<OnlineEufPropagator>(
          terms_, atoms_, terms_.mk_bool(true), terms_.mk_bool(false), online_options_,
          options_.euf_propagation == EufPropagationMode::Auto, statistics_, nullptr, false);
      execution_->bind_assignment_view(assignment_view());
    }
  }
  void push_level() override {
    if (execution_) execution_->notify_new_decision_level();
  }
  void pop_to_level(std::size_t) override {
    throw std::logic_error("EUF component backtrack requires the shared removal delta");
  }
  void backtrack(std::size_t level, const std::vector<int>& removed) override {
    token_.reset();
    if (execution_) execution_->notify_backtrack_delta(level, removed);
  }
  void assign(std::span<const theory::AssignedTheoryLiteral> delta) override {
    record_assignment(delta);
    if (execution_) execution_->notify_assignment(delta_literals_);
  }
  std::optional<theory::TheoryCheckResult> propagate() override {
    return execution_ ? consequence(*execution_) : std::nullopt;
  }
  theory::TheoryCheckResult check(theory::TheoryCheckEffort effort, std::uint64_t epoch,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    if (effort == theory::TheoryCheckEffort::Partial) {
      if (execution_) return partial(*execution_);
      if (online_ && options_.euf_engine == EufEngine::Lazy) {
        // check_on_full_model schedules this before quantifier generation.
        // Without incremental closure, rebuild EUF over the complete candidate.
        full_->set_assignment_epoch(epoch);
        return full_->check(assignment_.full_assignment(), arrangement);
      }
      theory::TheoryCheckResult result;
      result.status = theory::TheoryCheckStatus::Consistent;
      return result;
    }
    if (execution_) execution_->flush_incremental_stats();
    if (execution_ && !produce_model_projection_) {
      return stamp(partial(*execution_), epoch);
    }
    full_->set_assignment_epoch(epoch);
    auto result = full_->check(assignment_.full_assignment(), arrangement);
    if (result.status == theory::TheoryCheckStatus::Consistent) {
      care_ = full_->care_pairs(result.model.token);
    }
    return stamp(std::move(result), epoch);
  }
  std::vector<theory::InterfacePair> care_pairs(
      const theory::TheoryModelToken& token) const override {
    require_token(token);
    return care_;
  }
  theory::ComponentModelFragment export_model(
      const theory::TheoryModelToken& token) const override {
    require_token(token);
    theory::ComponentModelFragment result;
    result.token = token;
    for (const auto& literal : assignment_.full_assignment()) {
      result.facts.push_back({literal.atom, literal.value});
    }
    return result;
  }

 private:
  std::string_view check_timer_;
  std::unique_ptr<EufComponentAdapter> full_;
  // OnlineEufPropagator borrows these options and atoms_; both must outlive it.
  // Destroy execution before changing its borrowed preparation or configuration.
  CheckSatOptions online_options_;
  std::unique_ptr<OnlineEufPropagator> execution_;
  std::vector<theory::InterfacePair> care_;
};

class LiaTheoryComponent final : public ComponentAdapterSupport {
 public:
  LiaTheoryComponent(core::TermManager& terms, const CheckSatOptions& options, LiaSettings settings,
                     util::Statistics& statistics, bool online, bool produce_model_projection,
                     LiaComponentTiming timing, bool publish_arithmetic_statistics)
      : ComponentAdapterSupport(terms, options, statistics, online, produce_model_projection),
        settings_(settings),
        timing_(timing),
        publish_arithmetic_statistics_(publish_arithmetic_statistics) {
    descriptor_.id = "lia";
    descriptor_.fragment = "purified quantifier-free linear integer arithmetic";
    descriptor_.signature_families = {"integer-linear-arithmetic"};
    descriptor_.shared_sorts = {
        {terms.symbols().int_sort(), theory::StableInfiniteness::Yes, true}};
    descriptor_.convexity = theory::Convexity::NonConvex;
    descriptor_.capabilities = {
        .model_projection = true,
        .model_export = true,
        .model_mutation = false,
        .care_pairs = false,
        .incremental = true,
        .interface_extension = true,
        .equality_propagation = options.qf_uflia_tableau_equalities
                                    ? theory::EqualityPropagationStrength::Opportunistic
                                    : theory::EqualityPropagationStrength::None};
  }
  void bind_assignment_view(sat::TrailView view) override {
    ComponentAdapterSupport::bind_assignment_view(view);
    if (execution_) execution_->bind_assignment_view(view);
  }
  bool owns_atom(core::TermId atom) const override {
    const auto& node = terms_.get(atom);
    return (node.kind == core::TermKind::Le || node.kind == core::TermKind::Lt) &&
           terms_.get(node.children[0]).sort == terms_.symbols().int_sort() &&
           !contains_application_term(terms_, atom);
  }
  std::vector<core::TermId> interface_variables(core::TermId atom) const override {
    const auto normalized = theory::arithmetic::normalize_atom(terms_, atom);
    std::vector<core::TermId> result;
    for (const auto& [variable, coefficient] : normalized.polynomial.terms()) {
      if (!coefficient.is_zero()) result.push_back(variable);
    }
    std::sort(result.begin(), result.end());
    return result;
  }
  core::TermId encode_equality(const theory::InterfacePair& pair) override {
    const auto forward = terms_.mk_le(pair.lhs, pair.rhs);
    const auto reverse = terms_.mk_le(pair.rhs, pair.lhs);
    return terms_.mk_and({forward, reverse});
  }
  void register_atoms(std::span<const theory::ComponentAtom> atoms) override {
    execution_.reset();
    const auto previous = atoms_.size();
    append_atoms(atoms);
    if (!full_) {
      full_ = std::make_unique<LiaComponentAdapter>(terms_, atoms_, terms_.symbols().int_sort(),
                                                    shared_, 0, produce_model_projection_, options_,
                                                    settings_, statistics_, timing_);
      full_->register_interface_extensions();
      if (publish_arithmetic_statistics_)
        publish_lra_tableau_shape_stats(full_->prepared().base_tableau, statistics_);
    } else {
      for (std::size_t index = previous; index < atoms_.size(); ++index)
        full_->append_atom(atoms_[index]);
    }
    reset();
  }
  void reset() override {
    token_.reset();
    phases_.clear();
    if (online_ && full_) {
      execution_ = std::make_unique<ComponentLraExecution>(
          full_->prepared(), atoms_, options_.lra_propagation, options_.lra_row_propagation,
          options_.lra_bidirectional_row_propagation, options_.lra_theory_decisions,
          options_.lra_model_phase.value_or(false),
          options_.lra_pivoting_rule == LraPivotingRule::Soi || options_.lra_fixed_elimination,
          options_.lra_row_propagation_limits, [this](sat::Lit phase) { phases_.push_back(phase); },
          statistics_);
      execution_->bind_assignment_view(assignment_view());
    }
  }
  void push_level() override {
    if (execution_) execution_->notify_new_decision_level();
  }
  void pop_to_level(std::size_t) override {
    throw std::logic_error("arithmetic component backtrack requires the shared removal delta");
  }
  void backtrack(std::size_t level, const std::vector<int>& removed) override {
    token_.reset();
    if (execution_) execution_->notify_backtrack_delta(level, removed);
  }
  void assign(std::span<const theory::AssignedTheoryLiteral> delta) override {
    record_assignment(delta);
    if (execution_) execution_->notify_assignment(delta_literals_);
  }
  std::optional<theory::TheoryCheckResult> propagate() override {
    return execution_ ? consequence(*execution_) : std::nullopt;
  }
  sat::Lit suggest_literal() override {
    return execution_ ? sat::Lit{execution_->cb_decide()} : sat::Lit{};
  }
  std::vector<sat::Lit> phase_literals() override { return std::exchange(phases_, {}); }
  theory::TheoryCheckResult check(theory::TheoryCheckEffort effort, std::uint64_t epoch,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    if (effort == theory::TheoryCheckEffort::Partial) {
      if (execution_) return partial(*execution_);
      theory::TheoryCheckResult result;
      result.status = theory::TheoryCheckStatus::Consistent;
      return result;
    }
    token_.reset();
    if (publish_arithmetic_statistics_) statistics_.increment(kStatLraTheoryChecks);
    full_->set_assignment_epoch(epoch);
    theory::TheoryCheckResult result;
    if (execution_ && !produce_model_projection_) {
      result = full_->complete_integer_model(assignment_.full_assignment(), execution_->tableau(),
                                             arrangement);
    } else {
      result = full_->check(assignment_.full_assignment(), arrangement);
    }
    if (publish_arithmetic_statistics_ && result.status == theory::TheoryCheckStatus::LearnedClause)
      statistics_.increment(kStatLraTheoryConflicts);
    return stamp(std::move(result), epoch);
  }
  theory::ComponentModelFragment export_model(
      const theory::TheoryModelToken& token) const override {
    require_token(token);
    theory::ComponentModelFragment result;
    result.token = token;
    for (const auto& [term, variable] : full_->prepared().variables_by_term) {
      result.bindings.push_back(
          {term, {terms_.get(term).sort, full_->model().assignment(variable).rational}});
    }
    return result;
  }

 private:
  LiaSettings settings_;
  LiaComponentTiming timing_;
  bool publish_arithmetic_statistics_;
  std::unique_ptr<LiaComponentAdapter> full_;
  std::unique_ptr<ComponentLraExecution> execution_;
  std::vector<sat::Lit> phases_;
};

#endif

// A single arithmetic component uses the same full-check completion boundary
// as the combined recipe. Boolean constants belong to the shared SMT driver.
CheckSatOutcome solve_qf_lia_impl(core::TermManager& terms,
                                  const std::vector<core::TermId>& assertions,
                                  const std::vector<core::TermId>& model_terms,
                                  const CheckSatOptions& options, const LiaSettings& settings,
                                  util::Statistics& statistics,
                                  const ModelConversionChain& conversions) {
  static_cast<void>(model_terms);
  // Retain the historical arithmetic-route statistic key.
  statistics.increment(kStatSolverQfLra);
#if PRIMO_HAVE_IPASIR
  CombinationRecipe recipe;
  recipe.model_guided = false;
  recipe.online = true;
  recipe.forgettable_clauses = false;
  recipe.online_order = {0};
  recipe.timing_prefix = ":time.qf-lia.";
  bool published_atom_statistics = false;
  recipe.prepare_boolean = [&](BooleanClausifier& clausifier) {
    if (!published_atom_statistics) {
      publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
      published_atom_statistics = true;
    }
  };
  recipe.factories = {[&](bool) {
    return std::make_unique<LiaTheoryComponent>(terms, options, settings, statistics, true, false,
                                                LiaComponentTiming{}, true);
  }};
  return solve_component_recipe(terms, assertions, conversions, options, statistics, recipe);
#else
  static_cast<void>(terms);
  static_cast<void>(assertions);
  static_cast<void>(conversions);
  static_cast<void>(options);
  static_cast<void>(settings);
  return {CheckSatResult::Unsupported, "integer solving requires an IPASIR backend", statistics, "",
          InconclusiveReason::Unsupported};
#endif
}

CheckSatOutcome solve_qf_uflia_engine_impl(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::vector<core::TermId>& model_terms, const ModelConversionChain& conversions,
    const CheckSatOptions& options, const LiaSettings& settings, util::Statistics& statistics) {
  static_cast<void>(model_terms);
  statistics.increment(":solver.qf-uflia");
#if PRIMO_HAVE_IPASIR
  const bool online =
      options.qf_uflia_online_components && options.euf_engine == EufEngine::Incremental;
  CombinationRecipe recipe;
  recipe.model_guided = options.qf_uflia_model_combination;
  recipe.decision_budget = options.qf_uflia_model_decision_budget;
  recipe.learn_exported_equalities =
      !options.qf_uflia_tableau_equalities ||
      options.qf_uflia_equality_policy != QfUfliaEqualityPolicy::Names;
  recipe.online = online;
  recipe.online_order = {1, 0};
  recipe.timing_prefix = ":time.qf-uflia.";
  recipe.prepare_boolean = [&](BooleanClausifier& clausifier) {
    register_euf_boolean_arguments(clausifier, statistics);
  };
  recipe.factories = {
      [&](bool guided) {
        return std::make_unique<LiaTheoryComponent>(
            terms, options, settings, statistics, online, guided,
            LiaComponentTiming{":time.qf-uflia.lia-check-us", ":time.qf-uflia.lia-rational-us",
                               ":time.qf-uflia.lia-integer-us"},
            false);
      },
      [&](bool guided) {
        return std::make_unique<EufTheoryComponent>(terms, options, statistics, online, guided,
                                                    ":time.qf-uflia.euf-check-us");
      }};
  return solve_component_recipe(terms, assertions, conversions, options, statistics, recipe);
#else
  static_cast<void>(terms);
  static_cast<void>(assertions);
  static_cast<void>(conversions);
  static_cast<void>(options);
  static_cast<void>(settings);
  return {CheckSatResult::Unsupported, "theory combination requires an IPASIR backend", statistics,
          "", InconclusiveReason::Unsupported};
#endif
}

void record_disequality_propagation_opportunities(const core::TermManager& terms,
                                                  const PreparedFlatTheory& prepared,
                                                  const std::vector<AssignedTheoryAtom>& atoms,
                                                  util::Statistics& statistics) {
  if (atoms.size() != prepared.flat_atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  std::unordered_map<TermPairKey, bool, TermPairKeyHash> source_equalities;
  std::unordered_map<FlatPairKey, bool, FlatPairKeyHash> flat_equalities;
  source_equalities.reserve(atoms.size());
  flat_equalities.reserve(atoms.size());

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (atoms[i].filtered || !prepared.flat_atoms[i].equality) {
      continue;
    }
    const core::Term& term = terms.get(atoms[i].term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
      continue;
    }
    source_equalities.emplace(canonical_term_pair(term.children[0], term.children[1]),
                              atoms[i].value);
    flat_equalities.emplace(
        canonical_flat_pair(prepared.flat_atoms[i].lhs, prepared.flat_atoms[i].rhs),
        atoms[i].value);
  }

  std::unordered_map<theory::equality::FlatTermId, theory::equality::FlatDefinition>
      definition_by_result;
  definition_by_result.reserve(prepared.flat.definitions().size());
  for (const theory::equality::FlatDefinition& definition : prepared.flat.definitions()) {
    definition_by_result.emplace(definition.result, definition);
  }

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (atoms[i].filtered || !prepared.flat_atoms[i].equality || atoms[i].value) {
      continue;
    }

    const auto lhs_definition = definition_by_result.find(prepared.flat_atoms[i].lhs);
    const auto rhs_definition = definition_by_result.find(prepared.flat_atoms[i].rhs);
    if (lhs_definition == definition_by_result.end() ||
        rhs_definition == definition_by_result.end()) {
      continue;
    }
    statistics.increment(kStatEufDiseqPropAssignedAppDisequalities);

    const core::Term& source = terms.get(atoms[i].term);
    if (source.kind == core::TermKind::Equality && source.children.size() == 2) {
      const core::Term& lhs_source = terms.get(source.children[0]);
      const core::Term& rhs_source = terms.get(source.children[1]);
      if (lhs_source.kind == core::TermKind::Application &&
          rhs_source.kind == core::TermKind::Application &&
          lhs_source.symbol == rhs_source.symbol && lhs_source.children.size() == 1 &&
          rhs_source.children.size() == 1) {
        statistics.increment(kStatEufDiseqPropSourceUnaryCandidates);
        const auto found = source_equalities.find(
            canonical_term_pair(lhs_source.children[0], rhs_source.children[0]));
        if (found != source_equalities.end()) {
          statistics.increment(kStatEufDiseqPropSourceUnaryExistingArgAtom);
          statistics.increment(found->second ? kStatEufDiseqPropSourceUnaryArgAtomTrue
                                             : kStatEufDiseqPropSourceUnaryArgAtomFalse);
        }
      }
    }

    const theory::equality::FlatDefinition& lhs = lhs_definition->second;
    const theory::equality::FlatDefinition& rhs = rhs_definition->second;
    const auto argument_equality =
        flat_equalities.find(canonical_flat_pair(lhs.argument, rhs.argument));
    if (lhs.function == rhs.function) {
      statistics.increment(kStatEufDiseqPropFlatSameFunctionCandidates);
      if (argument_equality != flat_equalities.end()) {
        statistics.increment(kStatEufDiseqPropFlatSameFunctionExistingArgAtom);
        statistics.increment(argument_equality->second
                                 ? kStatEufDiseqPropFlatSameFunctionArgAtomTrue
                                 : kStatEufDiseqPropFlatSameFunctionArgAtomFalse);
      }
    }

    const auto function_equality =
        flat_equalities.find(canonical_flat_pair(lhs.function, rhs.function));
    if (function_equality != flat_equalities.end() && function_equality->second &&
        argument_equality != flat_equalities.end()) {
      statistics.increment(kStatEufDiseqPropGuardedFunctionGuardCandidates);
    }
    if (argument_equality != flat_equalities.end() && argument_equality->second &&
        function_equality != flat_equalities.end()) {
      statistics.increment(kStatEufDiseqPropGuardedArgumentGuardCandidates);
    }
  }
}

CheckSatOutcome solve_qf_uf_engine_impl(core::TermManager& terms,
                                        const std::vector<core::TermId>& assertions,
                                        const CheckSatOptions& options,
                                        util::Statistics& statistics,
                                        const ModelConversionChain& conversions) {
  statistics.increment(kStatSolverQfUf);
  sat::CadicalBackend backend(options.cadical_options);
  SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
  const ScopedTerminatorConnection terminator_connection(backend, terminator);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  // Bool's two-element domain, before the atom list is measured or consumed:
  // PreparedFlatTheory::align_assignments requires the atom count to be final
  // for the whole solve, and every heuristic below reads theory_atoms().
  register_euf_boolean_arguments(clausifier, statistics);
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.set(kStatPureTheoryFilterEnabled,
                 pure_theory_filter_enabled(options.pure_theory_filter) ? 1 : 0);
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  const EufPropagationMode euf_propagation =
      resolve_euf_propagation_mode(options, clausifier.theory_atoms().size(), statistics);
  CheckSatOptions resolved_options = options;
  resolved_options.euf_propagation = euf_propagation;
  TheoryPhaseMode theory_phase =
      resolve_theory_phase_mode(options.theory_phase, clausifier.theory_atoms(), statistics);
  if (options.theory_phase == TheoryPhaseMode::Auto && theory_phase == TheoryPhaseMode::Polarity) {
    statistics.increment(kStatSatPhaseAutoPolarity);
  }
  if ((options.theory_phase == TheoryPhaseMode::Negative ||
       options.theory_phase == TheoryPhaseMode::Auto) &&
      options.auto_basic_single_predicate_polarity && statistics.get(kStatAutoBasicSingle) != 0 &&
      statistics.get(kStatEufPredicateAtoms) != 0) {
    theory_phase = TheoryPhaseMode::Polarity;
    statistics.increment(kStatSatPhaseAutoBasicSinglePredicatePolarity);
  }
  if ((options.theory_phase == TheoryPhaseMode::Negative ||
       options.theory_phase == TheoryPhaseMode::Auto) &&
      options.auto_basic_multi_polarity && statistics.get(kStatAutoBasicMulti) != 0) {
    theory_phase = TheoryPhaseMode::Polarity;
    statistics.increment(kStatSatPhaseAutoBasicMultiPolarity);
  }
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    switch (theory_phase) {
      case TheoryPhaseMode::Negative:
        backend.phase(sat::Lit{-atom.lit.value});
        statistics.increment(kStatSatTheoryNegativePhase);
        break;
      case TheoryPhaseMode::Positive:
        backend.phase(atom.lit);
        break;
      case TheoryPhaseMode::Polarity:
        if (atom.occurs_negative && !atom.occurs_positive) {
          backend.phase(sat::Lit{-atom.lit.value});
          statistics.increment(kStatSatTheoryNegativePhase);
        } else if (atom.occurs_positive && !atom.occurs_negative) {
          backend.phase(atom.lit);
        }
        break;
      case TheoryPhaseMode::None:
        break;
      case TheoryPhaseMode::Auto:
        break;
    }
  }
  publish_sat_stats(backend.statistics(), statistics);

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  sat::Trail completed_assignment;
  std::unordered_set<std::string> learned_model_propagations;
  std::optional<PreparedFlatTheory> prepared_flat_theory;
  if (options.euf_engine == EufEngine::Incremental) {
    prepared_flat_theory.emplace(terms, clausifier.theory_atoms(), true_term, false_term,
                                 options.euf_explain_search, options.euf_proof_forest_diagnostics);
  }

#if PRIMO_HAVE_IPASIR
  std::optional<OnlineEufPropagator> online_propagator;
  std::optional<AssignmentConnection> assignment_connection;
  if (euf_propagation == EufPropagationMode::Online ||
      euf_propagation == EufPropagationMode::Conflict) {
    online_propagator.emplace(terms, clausifier.theory_atoms(), true_term, false_term,
                              resolved_options, options.euf_propagation == EufPropagationMode::Auto,
                              statistics);
    assignment_connection.emplace(backend, *online_propagator);
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      backend.add_observed_var(var);
    }
  }
#else
  if (euf_propagation == EufPropagationMode::Online ||
      euf_propagation == EufPropagationMode::Conflict) {
    return CheckSatOutcome{CheckSatResult::Unsupported,
                           "online EUF conflict/propagation requires CaDiCaL external propagators",
                           statistics, ""};
  }
#endif

  while (true) {
    if (auto stop = terminator.stop_outcome()) return *stop;
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
#if PRIMO_HAVE_IPASIR
    if (online_propagator.has_value()) {
      online_propagator->flush_incremental_stats();
    }
#endif
    publish_sat_stats(backend.statistics(), statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }
    statistics.increment(kStatTheoryChecks);

    sat::TrailView model_assignment;
#if PRIMO_HAVE_IPASIR
    if (assignment_connection) {
      model_assignment = assignment_connection->view();
    } else
#endif
    {
      load_assignment_snapshot(backend, clausifier.theory_atoms(), completed_assignment);
      model_assignment = sat::TrailView(completed_assignment);
    }
    const auto assigned_atoms = assigned_atoms_from_view(
        model_assignment, clausifier.theory_atoms(), options.pure_theory_filter);
    for (const auto& assigned : assigned_atoms) count_filtered_assignment(assigned, statistics);
    if (resolved_options.euf_diseq_propagation_diagnostics && prepared_flat_theory.has_value()) {
      record_disequality_propagation_opportunities(terms, prepared_flat_theory.value(),
                                                   assigned_atoms, statistics);
    }
    if (resolved_options.euf_dynamic_ackermann_shadow) {
      std::vector<std::optional<bool>> atom_values;
      atom_values.reserve(assigned_atoms.size());
      for (const AssignedTheoryAtom& atom : assigned_atoms) {
        atom_values.push_back(atom.value);
      }
      record_dynamic_ackermann_shadow(terms, clausifier.theory_atoms(), atom_values, statistics);
    }

    if (euf_propagation == EufPropagationMode::Model) {
      statistics.increment(kStatEufPropagationRounds);
      ModelPropagationResult propagation =
          learn_model_propagations(terms, assigned_atoms, true_term, false_term, resolved_options,
                                   learned_model_propagations, statistics);
      statistics.increment(kStatEufPropagationCandidates, propagation.candidates);
      statistics.increment(kStatEufPropagationDelivered, propagation.clauses.size());
      statistics.increment(kStatEufPropagationReasonLits, propagation.reason_lits);
      statistics.increment(kStatEufPropagationBudget, propagation.candidate_budget);
      statistics.increment(kStatEufPropagationDeliveryBudget, propagation.delivery_budget);
      if (propagation.budget_cutoff) {
        statistics.increment(kStatEufPropagationBudgetCutoffs);
      }
      if (propagation.hit_rate_cutoff) {
        statistics.increment(kStatEufPropagationHitRateCutoffs);
      }
      for (const sat::Clause& clause : propagation.clauses) {
        backend.add_clause(clause);
      }
      publish_sat_stats(backend.statistics(), statistics);
    }

    std::optional<sat::Clause> conflict_clause;
#if PRIMO_HAVE_IPASIR
    if (options.euf_engine == EufEngine::Incremental && online_propagator.has_value()) {
      // The online/conflict SAT-callback propagator already owns a persistent
      // flat closure kept in sync with the trail; reuse its verdict instead of
      // rebuilding an equivalent closure from the full assignment below.
      conflict_clause = online_propagator->incremental_conflict();
    } else
#endif
        if (options.euf_engine == EufEngine::Incremental) {
      conflict_clause = find_incremental_theory_conflict(prepared_flat_theory.value(),
                                                         assigned_atoms, statistics);
    } else {
      conflict_clause = find_theory_conflict(terms, assigned_atoms, true_term, false_term);
    }
    if (!conflict_clause.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      if (!options.produce_model) {
        return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
      }
      std::vector<TheoryModelAssignment> model_assignments;
      model_assignments.reserve(assigned_atoms.size());
      for (const AssignedTheoryAtom& atom : assigned_atoms) {
        model_assignments.push_back(TheoryModelAssignment{atom.term, atom.value, atom.filtered});
      }
      CheckSatOutcome outcome{CheckSatResult::Sat, "", statistics, ""};
      outcome.typed_model = build_typed_model(terms, assertions, model_assignments, {}, conversions,
                                              nullptr, &statistics);
      return outcome;
    }
    statistics.increment(kStatTheoryConflicts);
    record_theory_conflict(statistics);
    if (terminator.conflict_limit_reached()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unknown, "theory conflict limit reached", statistics,
                             "", InconclusiveReason::ResourceLimit};
    }
    if (conflict_clause->empty()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
    }
    backend.add_clause(conflict_clause.value());
    publish_sat_stats(backend.statistics(), statistics);
  }
}

// Reports util::Rational's two-tier-representation counters (see that class's
// comment and docs/qf-lra-heuristics.md's H2) as :rational.promotions and
// :rational.big-operations. They are process-wide running totals, so this
// records the delta across one solve. Wrapping rather than publishing inline
// because solve_qf_lra_impl returns from a dozen places and CheckSatOutcome
// carries a *copy* of the statistics taken at the return statement -- an RAII
// guard would update the original after that copy was already made.
//
// Diagnostic only, but not optional: without them, "is the machine-word fast
// path actually being taken on this benchmark, and would a different small-form
// width take it more often" is a guess rather than a measurement.
CheckSatOutcome solve_qf_lra_engine_impl(core::TermManager& terms,
                                         const std::vector<core::TermId>& assertions,
                                         const std::vector<core::TermId>& model_terms,
                                         const ModelConversionChain& conversions,
                                         const CheckSatOptions& options,
                                         util::Statistics& statistics) {
  const std::uint64_t promotions_before = util::Rational::promotion_count();
  const std::uint64_t big_operations_before = util::Rational::big_operation_count();
  CheckSatOutcome outcome =
      solve_qf_lra_impl(terms, assertions, model_terms, conversions, options, statistics);
  const std::uint64_t promotions = util::Rational::promotion_count() - promotions_before;
  const std::uint64_t big_operations =
      util::Rational::big_operation_count() - big_operations_before;
  statistics.increment(util::StatKey::RationalPromotions, promotions);
  statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  outcome.statistics.increment(util::StatKey::RationalPromotions, promotions);
  outcome.statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  return outcome;
}

// How compute_forced_model_equalities ended. Each terminal value names the
// situation; the caller owns the outcome it builds and the statistics it
// counts, which is the only thing the final-arrangement and model
// reconstruction paths disagree about.
enum class ForcedModelEqualityStatus {
  Success,
  // A learned conflict or a forced interface equality restarted the round.
  Recheck,
  EqualityScanProbeLimit,
  SeparationProbeLimit,
  SeparationFailed,
};

// The LRA state the equality scan reads. Pointers, because the caller only
// establishes them for the checks that need them.
struct LraCheckInputs {
  const std::vector<BooleanClausifier::TheoryAtom>& atoms;
  const PreparedLraTheory* prepared = nullptr;
  const theory::arithmetic::LraTableau* tableau = nullptr;
  const std::vector<AssignedTheoryAtom>& assignments;
};

struct ForcedModelEqualityInputs {
  const FlatEufModelView& euf_model;
  const std::vector<core::TermId>& shared_real_terms;
  // The atoms and assignments the separation works over, which are the round's
  // own, not the ones the equality scan checks.
  const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms;
  const std::vector<AssignedTheoryAtom>& lra_assignments;
  LraCheckInputs check;
  const std::optional<LraModelEqualPartition>& lra_model_partition;
  bool candidate_final_scan_enabled = false;
  const std::vector<core::TermId>& candidate_lra_implied_equalities;
  bool candidate_had_internal_model_equalities = false;
  bool lra_implied_equalities_for_model_complete = false;
  const std::vector<core::TermId>& lra_implied_equalities_for_model;
  std::optional<std::uint64_t> propagation_start;
  // Model reconstruction keeps the LRA-implied equalities for its model
  // assignments, so it deduplicates the internal equalities against those
  // alone; the final arrangement folds everything into one list and
  // deduplicates against the EUF-implied equalities too.
  bool keep_lra_equalities_separate = false;
};

struct ForcedModelEqualityResult {
  ForcedModelEqualityStatus status = ForcedModelEqualityStatus::Success;
  std::vector<core::TermId> euf_equalities;
  std::vector<core::TermId> lra_equalities;
  std::vector<core::TermId> forced_equalities;
  RealModelSeparationResult separation;
};

// The shared-Real equalities EUF and LRA force on a candidate model, and the
// atoms that separate the rest. Two call sites ran this sequence -- the final
// arrangement under !produce_model and model reconstruction under
// produce_model -- as independent copies of the same scan, separation,
// candidate-scan fallback and retry. Keeping equality classification in one
// place is what this is for: two hand-maintained copies of it produced the
// soundness bug that the mergeable-skip LRA-to-EUF experiment had to revert.
ForcedModelEqualityResult compute_forced_model_equalities(
    core::TermManager& terms, BooleanClausifier& clausifier, sat::CadicalBackend& backend,
    TheoryCombinationManager& no_manager, ModelEqualityArrangementState& arrangement,
    const ForcedModelEqualityInputs& inputs, const CheckSatOptions& options,
    util::Statistics& statistics) {
  ForcedModelEqualityResult result;
  const std::vector<ExplainedInterfaceEquality> explained_euf_equalities =
      euf_implied_real_equalities(terms, inputs.euf_model, inputs.shared_real_terms, statistics);
  result.euf_equalities = interface_equality_terms(explained_euf_equalities);
  bool deferred_candidate_lra_implied_equalities_for_model = false;
  // Only the precomputed complete set is deduplicated against the EUF-implied
  // equalities when the two lists are folded together.
  bool complete_source = false;
  if (inputs.candidate_final_scan_enabled) {
    result.lra_equalities = inputs.candidate_lra_implied_equalities;
    deferred_candidate_lra_implied_equalities_for_model = true;
  } else if (inputs.lra_implied_equalities_for_model_complete) {
    complete_source = true;
    result.lra_equalities = inputs.lra_implied_equalities_for_model;
  } else {
    const LraImpliedRealEqualitiesResult scan = lra_implied_real_equalities_for_partition(
        terms, inputs.check.atoms, *inputs.check.prepared, *inputs.check.tableau,
        inputs.check.assignments, inputs.lra_model_partition.value(), inputs.euf_model, no_manager,
        options, statistics);
    if (scan.probe_limit_hit) {
      result.status = ForcedModelEqualityStatus::EqualityScanProbeLimit;
      return result;
    }
    const std::vector<core::TermId> equality_terms = interface_equality_terms(scan.equalities);
    result.lra_equalities.insert(result.lra_equalities.end(), equality_terms.begin(),
                                 equality_terms.end());
  }

  const std::vector<core::TermId> internal_equalities =
      shared_model_internal_equality_terms(terms, no_manager);
  if (inputs.keep_lra_equalities_separate) {
    append_unique_equalities_by_pair(terms, result.lra_equalities, internal_equalities);
    result.forced_equalities = result.euf_equalities;
    result.forced_equalities.insert(result.forced_equalities.end(), result.lra_equalities.begin(),
                                    result.lra_equalities.end());
  } else {
    result.forced_equalities = result.euf_equalities;
    if (complete_source) {
      append_unique_equalities_by_pair(terms, result.forced_equalities, result.lra_equalities);
    } else {
      result.forced_equalities.insert(result.forced_equalities.end(), result.lra_equalities.begin(),
                                      result.lra_equalities.end());
    }
    append_unique_equalities_by_pair(terms, result.forced_equalities, internal_equalities);
  }

  result.separation = shared_real_model_separation_atoms(
      terms, inputs.lra_atoms, inputs.lra_assignments, inputs.euf_model, inputs.shared_real_terms,
      result.forced_equalities, options, statistics);
  if (result.separation.status == RealModelSeparationStatus::Inconsistent &&
      deferred_candidate_lra_implied_equalities_for_model) {
    // The candidate scan's equalities did not survive separation; redo the
    // exact scan and use what it forces instead.
    statistics.increment(":no.interface.candidate-final-scan-fallbacks");
    if (options.qf_uflra_model_reconciliation) {
      statistics.increment(":no.shared-model.exact-fallbacks");
    }
    if (inputs.candidate_had_internal_model_equalities) {
      statistics.increment(":no.interface.lra-to-euf-skip-model-reconstruction-fallbacks");
    }
    const LraImpliedRealEqualitiesResult scan = lra_implied_real_equalities_for_partition(
        terms, inputs.check.atoms, *inputs.check.prepared, *inputs.check.tableau,
        inputs.check.assignments, inputs.lra_model_partition.value(), inputs.euf_model, no_manager,
        options, statistics);
    if (scan.probe_limit_hit) {
      result.status = ForcedModelEqualityStatus::EqualityScanProbeLimit;
      return result;
    }
    statistics.increment(":no.interface.candidate-final-scan-fallback-equalities",
                         scan.equalities.size());
    const LraToEufEqualityFilterResult filtered_equalities =
        filter_lra_implied_equalities_for_euf(terms, no_manager, inputs.euf_model, scan.equalities,
                                              options.qf_uflra_model_reconciliation, statistics);
    if (learn_shared_model_direct_conflict(filtered_equalities, backend, statistics)) {
      arrangement.record_theory_conflict(inputs.propagation_start, statistics);
      result.status = ForcedModelEqualityStatus::Recheck;
      return result;
    }
    const InterfacePropagationResult propagation = force_implied_interface_equalities(
        terms, clausifier, backend, no_manager, filtered_equalities.forced_equalities,
        inputs.check.assignments, InterfaceEqualityClassification::LraImplied, statistics);
    publish_interface_propagation_result(propagation, ":no.interface.lra-to-euf", statistics);
    if (propagation.triggered_recheck()) {
      if (arrangement.has_active_branch()) {
        arrangement.record_rejection(std::nullopt, statistics);
      }
      result.status = ForcedModelEqualityStatus::Recheck;
      return result;
    }
    if (inputs.keep_lra_equalities_separate) {
      result.lra_equalities = filtered_equalities.model_equalities;
      append_unique_equalities_by_pair(terms, result.forced_equalities, result.lra_equalities);
    } else {
      append_unique_equalities_by_pair(terms, result.forced_equalities,
                                       filtered_equalities.model_equalities);
    }
    result.separation = shared_real_model_separation_atoms(
        terms, inputs.lra_atoms, inputs.lra_assignments, inputs.euf_model, inputs.shared_real_terms,
        result.forced_equalities, options, statistics);
  }
  if (result.separation.status == RealModelSeparationStatus::ProbeLimit) {
    result.status = ForcedModelEqualityStatus::SeparationProbeLimit;
    return result;
  }
  if (result.separation.status != RealModelSeparationStatus::Success) {
    result.status = ForcedModelEqualityStatus::SeparationFailed;
    return result;
  }
  return result;
}

CheckSatOutcome solve_qf_uflra_engine_impl(core::TermManager& terms,
                                           const std::vector<core::TermId>& assertions,
                                           const std::vector<core::TermId>& model_terms,
                                           const ModelConversionChain& conversions,
                                           const CheckSatOptions& options,
                                           util::Statistics& statistics) {
  statistics.increment(":solver.qf-uflra");
  const bool mixed_fixed_elimination = lra_fixed_elimination_for_mixed(options);
  const bool candidate_final_scan_enabled = options.qf_uflra_candidate_final_scan;
  const bool model_mutation_enabled =
      options.nelson_oppen_model_equality_mutation &&
      (options.nelson_oppen_model_equality_branching || options.qf_uflra_model_reconciliation);
  sat::CadicalBackend backend(options.cadical_options);
  SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
  const ScopedTerminatorConnection terminator_connection(backend, terminator);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  register_euf_boolean_arguments(clausifier, statistics);

  std::vector<BooleanClausifier::TheoryAtom> euf_atoms;
  std::vector<BooleanClausifier::TheoryAtom> lra_atoms;
  euf_atoms.reserve(clausifier.theory_atoms().size());
  lra_atoms.reserve(clausifier.theory_atoms().size());
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    require_supported_mixed_theory_atom(terms, atom);
    if (is_euf_theory_atom(terms, atom.term)) {
      euf_atoms.push_back(atom);
    } else if (is_lra_theory_atom(terms, atom.term)) {
      lra_atoms.push_back(atom);
    } else {
      throw UnsupportedInput("unsupported mixed-theory atom");
    }
  }
  const std::vector<core::TermId> shared_real_terms =
      shared_real_constants(terms, euf_atoms, lra_atoms);
  const std::vector<core::TermId> euf_model_real_terms_before_interface_filter =
      euf_real_model_terms_in_atoms(terms, euf_atoms);
  const std::size_t shared_real_pair_count = unordered_pair_count(shared_real_terms.size());
  TheoryCombinationManager no_manager(terms);
  no_manager.rebuild_from_interface_terms(shared_real_terms);
  for (const BooleanClausifier::TheoryAtom& atom : lra_atoms) {
    const core::Term& term = terms.get(atom.term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2 ||
        !no_manager.ensure_interface_pair(term.children.at(0), term.children.at(1)).has_value()) {
      continue;
    }
    no_manager.register_interface_equality_atom(atom.term, atom.lit);
  }
  ModelEqualityArrangementState model_equality_arrangement(no_manager);
  ModelEqualityCandidateQueue model_equality_candidate_queue;
  bool adaptive_legacy_bootstrap_active = true;
  bool adaptive_model_unit_attempted = false;
  bool adaptive_legacy_equality_producer = false;
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.set(":no.mixed.euf-atoms", euf_atoms.size());
  statistics.set(":no.mixed.lra-atoms", lra_atoms.size());
  statistics.set(":no.interface.shared-real-terms", shared_real_terms.size());
  statistics.set(":no.interface.model-real-terms-before-interface-filter",
                 euf_model_real_terms_before_interface_filter.size());
  statistics.set(":no.interface.model-real-terms-filtered",
                 euf_model_real_terms_before_interface_filter.size() - shared_real_terms.size());
  statistics.set(":no.interface.model-euf-real-classes", 0);
  statistics.set(":no.interface.model-euf-real-anchored-classes", 0);
  statistics.set(":no.interface.model-euf-real-fresh-classes", 0);
  statistics.set(":no.interface.model-euf-real-unmapped-terms", 0);
  statistics.set(":no.interface.model-euf-real-value-conflicts", 0);
  statistics.set(":no.online.shared-real-pairs", no_manager.summary().candidate_pairs);
  statistics.set(":no.interface.materialized-pairs", no_manager.summary().equality_registry.size());
  statistics.set(":no.interface.materialized-missing-pairs",
                 no_manager.summary().missing_equalities);
  statistics.set(":no.interface.sat-equality-atoms",
                 registered_interface_equality_atom_count(no_manager));
  statistics.set(":no.interface.sat-equalities-true", 0);
  statistics.set(":no.interface.sat-equalities-false", 0);
  statistics.set(":no.interface.sat-equalities-forced", 0);
  statistics.set(":no.interface.equalities-propagated", 0);
  statistics.set(":no.interface.euf-to-lra-equalities", 0);
  statistics.set(":no.interface.lra-to-euf-equalities", 0);
  statistics.set(":no.interface.equality-candidates", 0);
  statistics.set(":no.interface.equality-existing-atoms", 0);
  statistics.set(":no.interface.equality-missing-atoms", 0);
  statistics.set(":no.interface.equality-created-atoms", 0);
  statistics.set(":no.interface.equality-already-true", 0);
  statistics.set(":no.interface.equality-assigned-false", 0);
  statistics.set(":no.interface.equality-clauses", 0);
  statistics.set(":no.interface.equality-reason-lits", 0);
  statistics.set(":no.interface.equality-local-reasons", 0);
  statistics.set(":no.interface.equality-local-reason-lits", 0);
  statistics.set(":no.interface.equality-fallback-reasons", 0);
  statistics.set(":no.interface.euf-explanation-missing", 0);
  statistics.set(":no.interface.convex-rounds", 0);
  statistics.set(":no.interface.exact-only",
                 options.nelson_oppen_propagation == NelsonOppenPropagationMode::Exact ? 1 : 0);
  statistics.set(":no.interface.euf-spanning-equalities", 0);
  statistics.set(":no.interface.lra-opportunistic-enabled",
                 options.nelson_oppen_propagation != NelsonOppenPropagationMode::Exact ? 1 : 0);
  statistics.set(":no.interface.lra-opportunistic-skipped-rounds", 0);
  statistics.set(":no.interface.lra-opportunistic-candidates", 0);
  statistics.set(":no.interface.lra-opportunistic-fixed-eq", 0);
  statistics.set(":no.interface.lra-opportunistic-eq-row", 0);
  statistics.set(":no.interface.lra-opportunistic-eq-offset-rows", 0);
  statistics.set(":no.interface.lra-opportunistic-eq-rows", 0);
  statistics.set(":no.interface.lra-opportunistic-reason-lits", 0);
  statistics.set(":no.interface.lra-opportunistic-unmapped-terms", 0);
  statistics.set(":no.interface.lra-opportunistic-unmapped-equalities", 0);
  statistics.set(":no.interface.lra-opportunistic-existing-atoms", 0);
  statistics.set(":no.interface.lra-opportunistic-missing-atoms", 0);
  statistics.set(":no.interface.lra-opportunistic-created-atoms", 0);
  statistics.set(":no.interface.lra-opportunistic-clauses", 0);
  statistics.set(":no.interface.lra-opportunistic-rechecks", 0);
  statistics.set(":no.interface.lra-fixed-elimination-enabled", mixed_fixed_elimination ? 1 : 0);
  statistics.set(":no.interface.lra-exact-fallback-pairs", 0);
  statistics.set(":no.interface.lra-exact-fallback-skipped-pairs", 0);
  statistics.set(":no.interface.lra-exact-fallback-equalities", 0);
  statistics.set(":no.interface.lra-model-filter-builds", 0);
  statistics.set(":no.interface.lra-model-filter-full-pairs", 0);
  statistics.set(":no.interface.lra-model-filter-candidate-pairs", 0);
  statistics.set(":no.interface.lra-model-filter-pruned-pairs", 0);
  statistics.set(":no.interface.lra-model-filter-unmapped-terms", 0);
  statistics.set(":no.interface.lra-model-filter-conservative-pairs", 0);
  statistics.set(":no.interface.lra-model-mutation-enabled", model_mutation_enabled ? 1 : 0);
  statistics.set(
      ":no.interface.lra-model-mutation-scope-all",
      options.nelson_oppen_model_equality_mutation_scope == ModelEqualityMutationScope::All ? 1
                                                                                            : 0);
  statistics.set(
      ":no.interface.lra-model-mutation-scope-care",
      options.nelson_oppen_model_equality_mutation_scope == ModelEqualityMutationScope::Care ? 1
                                                                                             : 0);
  statistics.set(":no.interface.lra-model-mutation-calls", 0);
  statistics.set(":no.interface.lra-model-mutation-updates", 0);
  statistics.set(":no.interface.lra-model-mutation-attempted-updates", 0);
  statistics.set(":no.interface.lra-model-mutation-full-variables", 0);
  statistics.set(":no.interface.lra-model-mutation-scoped-variables", 0);
  statistics.set(":no.interface.lra-model-mutation-empty-scopes", 0);
  statistics.set(":no.interface.lra-model-mutation-rejected-global-pair-regressions", 0);
  statistics.set(":no.interface.lra-model-mutation-value-classes-before", 0);
  statistics.set(":no.interface.lra-model-mutation-value-classes-after", 0);
  statistics.set(":no.interface.lra-model-mutation-candidate-pairs-before", 0);
  statistics.set(":no.interface.lra-model-mutation-candidate-pairs-after", 0);
  statistics.set(":no.interface.lra-model-mutation-pruned-pairs", 0);
  statistics.set(":no.interface.equality-rechecks", 0);
  statistics.set(":no.interface.euf-to-lra-candidates", 0);
  statistics.set(":no.interface.euf-to-lra-existing-atoms", 0);
  statistics.set(":no.interface.euf-to-lra-missing-atoms", 0);
  statistics.set(":no.interface.euf-to-lra-created-atoms", 0);
  statistics.set(":no.interface.euf-to-lra-clauses", 0);
  statistics.set(":no.interface.euf-to-lra-rechecks", 0);
  statistics.set(":no.interface.lra-to-euf-candidates", 0);
  statistics.set(":no.interface.lra-to-euf-existing-atoms", 0);
  statistics.set(":no.interface.lra-to-euf-missing-atoms", 0);
  statistics.set(":no.interface.lra-to-euf-created-atoms", 0);
  statistics.set(":no.interface.lra-to-euf-clauses", 0);
  statistics.set(":no.interface.lra-to-euf-rechecks", 0);
  statistics.set(":no.interface.lra-to-euf-pair-dedup-lookups", 0);
  statistics.set(":no.interface.lra-to-euf-pair-dedup-duplicates", 0);
  statistics.set(":no.interface.lra-to-euf-equalities-euf-already-true", 0);
  statistics.set(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped", 0);
  statistics.set(":no.interface.lra-to-euf-equalities-euf-mergeable-forced", 0);
  statistics.set(":no.interface.lra-to-euf-equalities-euf-forbidden-forced", 0);
  statistics.set(":no.interface.lra-to-euf-internal-model-equalities", 0);
  statistics.set(":no.interface.lra-to-euf-skip-model-reconstruction-fallbacks", 0);
  statistics.set(":no.shared-model.enabled", options.qf_uflra_model_reconciliation ? 1 : 0);
  statistics.set(":combination.model.enabled",
                 options.experimental_generic_model_combination ? 1 : 0);
  statistics.set(":no.shared-model.rounds", 0);
  statistics.set(":no.shared-model.iterations", 0);
  statistics.set(":no.shared-model.lra-value-classes", 0);
  statistics.set(":no.shared-model.lra-spanning-equalities", 0);
  statistics.set(":no.shared-model.internal-euf-merges", 0);
  statistics.set(":no.shared-model.euf-forbidden-equalities", 0);
  statistics.set(":no.shared-model.lra-separations", 0);
  statistics.set(":no.shared-model.reconstructed-models", 0);
  statistics.set(":no.shared-model.converged-rounds", 0);
  statistics.set(":no.shared-model.exact-fallbacks", 0);
  statistics.set(":no.shared-model.validation-failures", 0);
  statistics.set(":no.shared-model.concrete-delta-order-pairs", 0);
  statistics.set(":no.shared-model.concrete-delta-order-bounds", 0);
  statistics.set(":no.shared-model.concrete-delta-order-tightenings", 0);
  statistics.set(":no.shared-model.sat-materializations-avoided", 0);
  statistics.set(":no.shared-model.congruence-fallbacks", 0);
  statistics.set(":no.shared-model.congruence-summary-checks", 0);
  statistics.set(":no.shared-model.congruence-summary-terms", 0);
  statistics.set(":no.shared-model.forbidden-equalities", 0);
  statistics.set(":no.shared-model.forbidden-before", 0);
  statistics.set(":no.shared-model.forbidden-after", 0);
  statistics.set(":no.shared-model.unresolved-forbidden", 0);
  statistics.set(":no.shared-model.compatibility-checks", 0);
  statistics.set(":no.shared-model.compatibility-edges", 0);
  statistics.set(":no.shared-model.compatibility-already-equal", 0);
  statistics.set(":no.shared-model.compatibility-merges", 0);
  statistics.set(":no.shared-model.compatibility-splits", 0);
  statistics.set(":no.shared-model.compatibility-successes", 0);
  statistics.set(":no.shared-model.compatibility-unmapped", 0);
  statistics.set(":no.shared-model.compatibility-projections", 0);
  statistics.set(":no.shared-model.compatibility-projection-terms", 0);
  statistics.set(":no.shared-model.compatibility-initial-disagreements", 0);
  statistics.set(":no.shared-model.compatibility-consequence-disagreements", 0);
  statistics.set(":no.shared-model.split-materializations", 0);
  statistics.set(":no.shared-model.split-materialization-failures", 0);
  statistics.set(":no.shared-model.split-tried", 0);
  statistics.set(":no.shared-model.split-assigned", 0);
  statistics.set(":no.shared-model.split-frontier-batches", 0);
  statistics.set(":no.shared-model.split-frontier-pairs", 0);
  statistics.set(":no.shared-model.split-frontier-duplicates", 0);
  statistics.set(":no.shared-model.split-frontier-max", 0);
  statistics.set(":no.shared-model.targeted-separation-searches", 0);
  statistics.set(":no.shared-model.targeted-separation-pairs", 0);
  statistics.set(":no.shared-model.targeted-separation-capped", 0);
  statistics.set(":no.shared-model.targeted-separation-branches", 0);
  statistics.set(":no.shared-model.targeted-separation-backtracks", 0);
  statistics.set(":no.shared-model.targeted-separation-successes", 0);
  statistics.set(":no.shared-model.targeted-separation-failures", 0);
  statistics.set(":no.interface.conflicts", 0);
  statistics.set(":no.interface.euf-conflicts", 0);
  statistics.set(":no.interface.lra-conflicts", 0);
  statistics.set(":no.interface.model-equality-candidates", 0);
  statistics.set(":no.interface.model-equality-created", 0);
  statistics.set(":no.interface.model-equality-pending", 0);
  statistics.set(":no.interface.model-equality-activated", 0);
  statistics.set(":no.interface.model-equality-duplicate-pending", 0);
  statistics.set(":no.interface.model-equality-pending-batches", 0);
  statistics.set(":no.interface.model-equality-pending-batch-lits", 0);
  statistics.set(":no.interface.model-equality-max-pending-batch", 0);
  statistics.set(":no.interface.model-equality-assumptions", 0);
  statistics.set(":no.interface.model-equality-assumption-solve-calls", 0);
  statistics.set(":no.interface.model-equality-failed-assumptions", 0);
  statistics.set(":no.interface.model-equality-blocking-clauses", 0);
  statistics.set(":no.interface.model-equality-dropped-assumptions", 0);
  statistics.set(":no.interface.model-equality-retained-assumptions", 0);
  statistics.set(":no.interface.model-equality-conflicts", 0);
  statistics.set(":no.interface.model-equality-full-pairs", 0);
  statistics.set(":no.interface.model-equality-filtered-pairs", 0);
  statistics.set(":no.interface.model-equality-care-pairs", 0);
  statistics.set(":no.interface.model-equality-care-pruned-pairs", 0);
  statistics.set(":no.interface.model-equality-scanned", 0);
  statistics.set(":no.interface.model-equality-lra-equal", 0);
  statistics.set(":no.interface.model-equality-mutation-checks", 0);
  statistics.set(":no.interface.model-equality-mutated", 0);
  statistics.set(":no.interface.model-equality-selected-care", 0);
  statistics.set(":no.interface.model-equality-selected-full", 0);
  statistics.set(":no.interface.model-equality-skipped-assigned", 0);
  statistics.set(":no.interface.model-equality-skipped-tried", 0);
  statistics.set(":no.interface.model-equality-queue-builds", 0);
  statistics.set(":no.interface.model-equality-queued", 0);
  statistics.set(":no.interface.model-equality-queue-pops", 0);
  statistics.set(":no.interface.model-equality-queue-skipped-tried", 0);
  statistics.set(":no.interface.model-equality-queue-skipped-assigned", 0);
  statistics.set(":no.interface.model-equality-queue-empty", 0);
  statistics.set(":no.interface.model-equality-adaptive-stops", 0);
  statistics.set(":no.interface.model-equality-active-assumptions", 0);
  statistics.set(":no.interface.model-equality-max-active-assumptions", 0);
  statistics.set(":no.interface.arrangement-checks", 0);
  statistics.set(":no.interface.arrangement-failures", 0);
  statistics.set(":no.interface.arrangement-separations", 0);
  statistics.set(":no.interface.model-separation-candidates", 0);
  statistics.set(":no.interface.model-separation-euf-mergeable-pairs", 0);
  statistics.set(":no.interface.model-separation-euf-forbidden-pairs", 0);
  statistics.set(":no.interface.model-separation-search-capped", 0);
  statistics.set(":no.interface.model-separation-search-branches", 0);
  statistics.set(":no.interface.model-separation-search-backtracks", 0);
  statistics.set(":no.interface.model-separation-model-oriented", 0);
  statistics.set(":no.interface.arrangement-lra-extra-atom-probes", 0);
  statistics.set(":no.interface.arrangement-lra-extra-atoms-probed", 0);
  statistics.set(":no.interface.lra-extra-atom-probes", 0);
  statistics.set(":no.interface.lra-extra-atoms-probed", 0);
  statistics.set(":no.interface.lra-extra-atom-probe-limit-hit", 0);
  statistics.set(":no.interface.lra-probe-reuse-pairs", 0);
  statistics.set(":no.interface.lra-probe-reuse-orientations", 0);
  statistics.set(":no.interface.lra-probe-reuse-assignment-bounds", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-copies", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-fallbacks", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-difference-fallbacks", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-basis-cost-fallbacks", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-max-difference-terms", 0);
  statistics.set(":no.interface.lra-probe-reuse-checked-tableau-max-pivot-term-ops", 0);
  statistics.set(":no.interface.lra-probe-reuse-mapping-fallbacks", 0);
  statistics.set(":no.interface.lra-probe-reuse-context-conflicts", 0);
  statistics.set(":no.interface.euf-care-checks", 0);
  statistics.set(":no.interface.euf-care-full-pairs", 0);
  statistics.set(":no.interface.euf-care-pairs", 0);
  statistics.set(":no.interface.euf-care-pruned-pairs", 0);
  statistics.set(":no.interface.euf-care-argument-definitions", 0);
  statistics.set(":no.interface.euf-care-function-classes", 0);
  statistics.set(":no.interface.euf-care-definition-pairs-full", 0);
  statistics.set(":no.interface.euf-care-definition-pairs-within-classes", 0);
  statistics.set(":no.interface.euf-care-argument-pairs-checked", 0);
  statistics.set(":no.interface.euf-care-definition-pairs-pruned", 0);
  statistics.set(":no.interface.euf-care-duplicate-argument-pairs-pruned", 0);
  statistics.set(":no.interface.euf-care-cache-hits", 0);
  statistics.set(":no.interface.euf-care-cache-misses", 0);
  statistics.set(":no.interface.euf-implied-class-projections", 0);
  statistics.set(":no.interface.euf-implied-class-projection-terms", 0);
  statistics.set(":no.interface.candidate-final-scan-enabled",
                 candidate_final_scan_enabled ? 1 : 0);
  statistics.set(":no.interface.candidate-final-scan-pairs", 0);
  statistics.set(":no.interface.candidate-final-scan-skipped-pairs", 0);
  statistics.set(":no.interface.candidate-final-scan-equalities", 0);
  statistics.set(":no.interface.candidate-final-scan-fallbacks", 0);
  statistics.set(":no.interface.candidate-final-scan-fallback-equalities", 0);
  statistics.set(":no.interface.lra-prepared-reused", 0);
  statistics.set(":no.interface.lra-prepared-augmented", 0);
  statistics.set(":no.interface.lra-model-epoch-prepared-hits", 0);
  statistics.set(":no.interface.lra-model-epoch-prepared-misses", 0);
  statistics.set(":no.interface.lra-model-epoch-check-hits", 0);
  statistics.set(":no.interface.lra-model-epoch-check-misses", 0);
  statistics.set(":no.interface.lra-model-epoch-invalidations", 0);
  statistics.set(":no.interface.lra-model-epoch-tableau-reuses", 0);
  statistics.set(":no.interface.euf-prepared-reused", 0);
  statistics.set(":no.interface.euf-prepared-augmented", 0);
  statistics.set(":no.interface.euf-prepared-atoms", 0);
  statistics.set(":no.interface.euf-prepared-definitions", 0);
  statistics.set(":no.interface.lra-online-enabled", 0);
  statistics.set(":no.interface.euf-online-enabled", 0);
  statistics.set(":no.interface.euf-live-model-attempts", 0);
  statistics.set(":no.interface.euf-live-model-reuses", 0);
  statistics.set(":no.interface.euf-live-model-fallbacks", 0);
  statistics.set(":no.interface.euf-live-model-incomplete", 0);
  statistics.set(":no.interface.euf-live-model-registration-invalid", 0);
  statistics.set(":no.interface.euf-live-deferred-callbacks", 0);
  statistics.set(":no.interface.euf-final-closure-rebuilds", 0);
  statistics.set(":no.interface.euf-live-registry-syncs", 0);
  statistics.set(":no.interface.lra-online-final-checks", 0);
  statistics.set(":no.online.enabled", 0);
  statistics.set(":no.online.precreated-equality-atoms", 0);
  statistics.set(":no.online.paired-bound-precreate-candidates", 0);
  statistics.set(":no.online.assignments", 0);
  statistics.set(":no.online.backtracks", 0);
  statistics.set(":no.online.lra-published-equalities", 0);
  statistics.set(":no.online.lra-bound-equality-candidates", 0);
  statistics.set(":no.online.lra-bound-equality-missing-atoms", 0);
  statistics.set(":no.online.lra-bound-equality-materialized", 0);
  statistics.set(":no.online.lazy-equality-atoms-created", 0);
  statistics.set(":no.online.lazy-equality-vars-observed", 0);
  statistics.set(":no.online.propagations", 0);
  statistics.set(":no.online.reason-lits", 0);
  statistics.set(":no.online.euf-propagations", 0);
  statistics.set(":no.online.euf-external-clauses", 0);
  statistics.set(":no.online.euf-generated-equalities-registered", 0);
  statistics.set(":no.online.euf-generated-equality-registration-failures", 0);
  statistics.set(":no.online.euf-generated-equality-observation-failures", 0);
  statistics.set(":no.online.skipped-assigned", 0);
  // Historical online names retained for output compatibility. Reconciliation
  // also increments the skip counter when a shared candidate model converges.
  statistics.set(":no.online.final-lra-equality-scan-skipped", 0);
  statistics.set(":no.online.final-lra-equality-scan-capped", 0);
  statistics.set(":no.online.final-lra-equality-scan-fallbacks", 0);
  statistics.set(":no.interface.lra-conflict-lits", 0);
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);

  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    if (options.theory_phase == TheoryPhaseMode::Negative ||
        options.theory_phase == TheoryPhaseMode::Auto) {
      backend.phase(sat::Lit{-atom.lit.value});
      statistics.increment(kStatSatTheoryNegativePhase);
    } else if (options.theory_phase == TheoryPhaseMode::Positive) {
      backend.phase(atom.lit);
    } else if (options.theory_phase == TheoryPhaseMode::Polarity) {
      if (atom.occurs_negative && !atom.occurs_positive) {
        backend.phase(sat::Lit{-atom.lit.value});
        statistics.increment(kStatSatTheoryNegativePhase);
      } else if (atom.occurs_positive && !atom.occurs_negative) {
        backend.phase(atom.lit);
      }
    }
  }

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  const PreparedLraTheory base_prepared_lra_theory(
      terms, lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, mixed_fixed_elimination,
      options.produce_model || !shared_real_terms.empty(), options.lra_least_violated_leaving,
      lra_tuning_from_options(options.lra_tuning));
  publish_lra_tableau_shape_stats(base_prepared_lra_theory.base_tableau, statistics);
  std::optional<PreparedFlatTheory> prepared_euf_theory;
  if (!euf_atoms.empty()) {
    prepared_euf_theory.emplace(terms, euf_atoms, true_term, false_term, options.euf_explain_search,
                                options.euf_proof_forest_diagnostics);
    statistics.set(":no.interface.euf-prepared-atoms", prepared_euf_theory->flat_atoms.size());
    statistics.set(":no.interface.euf-prepared-definitions",
                   prepared_euf_theory->installed_definition_count);
  }
#if PRIMO_HAVE_IPASIR
  CheckSatOptions mixed_euf_options = options;
  if (options.qf_uflra_online_euf) {
    mixed_euf_options.euf_propagation =
        resolve_euf_propagation_mode(options, euf_atoms.size(), statistics);
  }
  std::optional<OnlineEufPropagator> online_euf_propagator;
  std::optional<OnlineLraPropagator> online_lra_propagator;
  std::optional<MixedTheoryPropagationDispatcher> mixed_online_dispatcher;
  std::optional<AssignmentConnection> assignment_connection;
  if (!lra_atoms.empty() && !options.nelson_oppen_model_equality_branching) {
    online_lra_propagator.emplace(
        base_prepared_lra_theory, lra_atoms, options.lra_propagation, options.lra_row_propagation,
        options.lra_bidirectional_row_propagation, options.lra_theory_decisions,
        options.lra_model_phase.value_or(false),
        options.lra_pivoting_rule == LraPivotingRule::Soi || mixed_fixed_elimination,
        options.lra_row_propagation_limits, backend, statistics, false,
        options.max_theory_conflicts);
    statistics.set(":no.interface.lra-online-enabled", 1);
  }
  const bool publish_lra_interface_equalities =
      options.nelson_oppen_propagation == NelsonOppenPropagationMode::Online;
  const bool use_mixed_dispatcher = publish_lra_interface_equalities || options.qf_uflra_online_euf;
  if (use_mixed_dispatcher) {
    if (options.qf_uflra_online_euf && !euf_atoms.empty()) {
      online_euf_propagator.emplace(terms, euf_atoms, true_term, false_term, mixed_euf_options,
                                    options.euf_propagation == EufPropagationMode::Auto,
                                    statistics);
    }
    mixed_online_dispatcher.emplace(
        terms, clausifier, backend, no_manager,
        online_euf_propagator ? &online_euf_propagator.value() : nullptr,
        online_lra_propagator ? &online_lra_propagator.value() : nullptr, lra_atoms,
        publish_lra_interface_equalities, statistics);
    assignment_connection.emplace(backend, *mixed_online_dispatcher);
    mixed_online_dispatcher->observe_registered_interface_equalities(
        /*count_observation=*/false);
    if (online_euf_propagator) {
      for (const BooleanClausifier::TheoryAtom& atom : euf_atoms) {
        backend.add_observed_var(sat_var(atom.lit));
      }
      statistics.set(":no.interface.euf-online-enabled", 1);
    }
  } else if (online_lra_propagator) {
    assignment_connection.emplace(backend, *online_lra_propagator);
  }
  if (online_lra_propagator) {
    for (const BooleanClausifier::TheoryAtom& atom : lra_atoms) {
      backend.add_observed_var(sat_var(atom.lit));
    }
  }
#endif

  sat::Trail completed_assignment;
#if PRIMO_HAVE_IPASIR
  std::size_t observed_atom_count = 0;
#endif
  MixedLraModelEpoch lra_model_epoch;
  MixedEufCareEpoch euf_care_epoch;
  ModelBasedCombinationCoordinator generic_model_coordinator;
  std::uint64_t generic_assignment_epoch = 0;
  while (true) {
    statistics.increment(kStatSatSolveCalls);
#if PRIMO_HAVE_IPASIR
    if (mixed_online_dispatcher.has_value()) {
      assignment_connection->flush();
      statistics.set(":no.interface.euf-live-deferred-callbacks",
                     assignment_connection->deferred_notifications());
      mixed_online_dispatcher->observe_registered_interface_equalities();
    }
    if (assignment_connection) {
      // Lazy children and generated interface atoms share the same owner too.
      // Copy the ID before observation: callbacks can grow the atom registry.
      while (observed_atom_count < clausifier.theory_atoms().size()) {
        const int var = std::abs(clausifier.theory_atoms()[observed_atom_count++].lit.value);
        backend.add_observed_var(var);
      }
    }
#endif
    model_equality_arrangement.activate_pending_branch(statistics);
    const std::optional<std::uint64_t> active_model_equality_propagation_start =
        model_equality_arrangement.assume_active_branches(backend, statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    const sat::SatResult sat_result = backend.solve();
    publish_sat_stats(backend.statistics(), statistics);
    if (auto stop = terminator.stop_outcome()) return *stop;
    if (sat_result == sat::SatResult::Unsat && model_equality_arrangement.has_active_branch()) {
      model_equality_arrangement.learn_failed_assumption_clause(
          backend, active_model_equality_propagation_start, statistics);
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }
    ++generic_assignment_epoch;
    no_manager.invalidate_shared_model();
    if (options.qf_uflra_model_reconciliation) {
      no_manager.begin_shared_model_round();
      statistics.increment(":no.shared-model.rounds");
    }
    no_manager.clear_interface_equality_classifications();
    sat::TrailView model_assignment;
#if PRIMO_HAVE_IPASIR
    if (assignment_connection) {
      model_assignment = assignment_connection->view();
    } else
#endif
    {
      load_assignment_snapshot(backend, clausifier.theory_atoms(), completed_assignment);
      model_assignment = sat::TrailView(completed_assignment);
    }
    no_manager.bind_assignment_view(model_assignment);
    const std::vector<InterfaceEqualityRegistryEntry> true_interface_equalities =
        no_manager.interface_equalities_with_assignment(InterfaceEqualityAssignment::True);
    const std::vector<InterfaceEqualityRegistryEntry> false_interface_equalities =
        no_manager.interface_equalities_with_assignment(InterfaceEqualityAssignment::False);
    std::vector<TermPairKey> false_interface_pairs;
    false_interface_pairs.reserve(false_interface_equalities.size());
    for (const InterfaceEqualityRegistryEntry& equality : false_interface_equalities) {
      false_interface_pairs.push_back(canonical_term_pair(equality.lhs, equality.rhs));
    }
    statistics.set(":no.interface.sat-equalities-true", true_interface_equalities.size());
    statistics.set(":no.interface.sat-equalities-false", false_interface_equalities.size());

    std::optional<sat::Clause> euf_conflict;
    std::optional<EufClosureCheckResult> euf_check;
#if PRIMO_HAVE_IPASIR
    std::optional<IncrementalFlatAssignmentState::ModelScope> live_euf_model_scope;
#endif
    std::optional<FlatEufModelView> euf_model;
    const auto replay_changed_assignment = [&]() {
#if PRIMO_HAVE_IPASIR
      if (assignment_connection && assignment_connection->has_pending()) {
        euf_model.reset();
        live_euf_model_scope.reset();
        assignment_connection->flush();
        statistics.set(":no.interface.euf-live-deferred-callbacks",
                       assignment_connection->deferred_notifications());
        return true;
      }
#endif
      return false;
    };
    std::vector<AssignedTheoryAtom> euf_assignments;
    // Only copy euf_atoms when this round actually has an interface equality
    // to layer in; append_*_interface_equalities need a mutable vector to
    // push onto, but most rounds append nothing, and every downstream
    // consumer below (ensure_theory_atoms, align_assignments) only reads
    // through a const reference, so a plain alias to euf_atoms is exact.
    const bool euf_atoms_need_interface_equalities =
        !true_interface_equalities.empty() || !false_interface_equalities.empty();
    std::vector<BooleanClausifier::TheoryAtom> euf_atoms_for_check_storage;
    if (euf_atoms_need_interface_equalities) {
      euf_atoms_for_check_storage = euf_atoms;
    }
    const std::vector<BooleanClausifier::TheoryAtom>& euf_atoms_for_check =
        euf_atoms_need_interface_equalities ? euf_atoms_for_check_storage : euf_atoms;
    std::vector<AssignedTheoryAtom> euf_assignments_for_check;
    EufCarePairDiagnostics euf_care;
    if (!euf_atoms.empty()) {
      statistics.increment(kStatTheoryChecks);
      euf_assignments =
          assigned_atoms_from_view(model_assignment, euf_atoms, options.pure_theory_filter);
      for (const AssignedTheoryAtom& atom : euf_assignments) {
        count_filtered_assignment(atom, statistics);
      }
      euf_assignments_for_check = euf_assignments;
      if (euf_atoms_need_interface_equalities) {
        append_true_interface_equalities(true_interface_equalities, euf_atoms_for_check_storage,
                                         euf_assignments_for_check);
        append_interface_equalities(false_interface_equalities, false, euf_atoms_for_check_storage,
                                    euf_assignments_for_check);
      }
#if PRIMO_HAVE_IPASIR
      if (options.qf_uflra_online_euf && mixed_online_dispatcher.has_value()) {
        statistics.increment(":no.interface.euf-live-model-attempts");
        const OnlineEufPropagator::ModelScopeAvailability availability =
            mixed_online_dispatcher->euf_model_scope_availability();
        live_euf_model_scope = mixed_online_dispatcher->begin_euf_model_scope();
        if (live_euf_model_scope.has_value()) {
          euf_model.emplace(live_euf_model_scope->view());
          statistics.increment(":no.interface.euf-live-model-reuses");
        } else {
          statistics.increment(":no.interface.euf-live-model-fallbacks");
          if (availability == OnlineEufPropagator::ModelScopeAvailability::IncompleteAssignment) {
            statistics.increment(":no.interface.euf-live-model-incomplete");
          } else if (availability ==
                     OnlineEufPropagator::ModelScopeAvailability::InvalidRegistration) {
            statistics.increment(":no.interface.euf-live-model-registration-invalid");
          }
        }
      }
#endif
      if (!euf_model.has_value()) {
        const std::size_t appended_atoms =
            prepared_euf_theory->ensure_theory_atoms(terms, euf_atoms_for_check);
        euf_assignments_for_check =
            prepared_euf_theory->align_assignments(euf_atoms_for_check, euf_assignments_for_check);
        if (appended_atoms == 0) {
          statistics.increment(":no.interface.euf-prepared-reused");
        } else {
          statistics.increment(":no.interface.euf-prepared-augmented");
          statistics.set(":no.interface.euf-prepared-atoms",
                         prepared_euf_theory->flat_atoms.size());
          statistics.set(":no.interface.euf-prepared-definitions",
                         prepared_euf_theory->installed_definition_count);
        }
        euf_check = check_incremental_euf_closure(prepared_euf_theory.value(),
                                                  euf_assignments_for_check, statistics);
        euf_model.emplace(FlatEufModelView{prepared_euf_theory->flat, euf_check->closure});
        euf_conflict = euf_check->conflict;
        statistics.increment(":no.interface.euf-final-closure-rebuilds");
      }
      if (options.nelson_oppen_model_equality_branching || candidate_final_scan_enabled ||
          (model_mutation_enabled && options.nelson_oppen_model_equality_mutation_scope ==
                                         ModelEqualityMutationScope::Care)) {
        bool computed_care = false;
        if (euf_care_epoch.valid && same_theory_assignments(euf_care_epoch.checked_assignments,
                                                            euf_assignments_for_check)) {
          euf_care = euf_care_epoch.diagnostics;
          statistics.increment(":no.interface.euf-care-cache-hits");
        } else {
          euf_care = euf_care_pair_diagnostics(euf_model.value(), shared_real_terms);
          euf_care_epoch.checked_assignments = euf_assignments_for_check;
          euf_care_epoch.diagnostics = euf_care;
          euf_care_epoch.valid = true;
          computed_care = true;
          statistics.increment(":no.interface.euf-care-cache-misses");
        }
        publish_euf_care_pair_diagnostics(euf_care, computed_care, statistics);
      }
    }
    if (euf_conflict.has_value()) {
      statistics.increment(kStatTheoryConflicts);
      record_theory_conflict(statistics);
      record_interface_conflict(euf_conflict, no_manager, ":no.interface.euf-conflicts",
                                statistics);
      model_equality_arrangement.record_theory_conflict(active_model_equality_propagation_start,
                                                        statistics);
      if (terminator.conflict_limit_reached()) {
        publish_sat_stats(backend.statistics(), statistics);
        return CheckSatOutcome{CheckSatResult::Unknown, "theory conflict limit reached", statistics,
                               "", InconclusiveReason::ResourceLimit};
      }
      if (euf_conflict->empty()) {
        publish_sat_stats(backend.statistics(), statistics);
        return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
      }
      backend.add_clause(euf_conflict.value());
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    statistics.increment(kStatLraTheoryChecks);
    // Same reasoning as euf_atoms_for_check above: LRA only ever appends true
    // interface equalities, so skip the copy when there are none this round.
    const bool lra_atoms_need_interface_equalities = !true_interface_equalities.empty();
    std::vector<BooleanClausifier::TheoryAtom> lra_atoms_for_check_storage;
    if (lra_atoms_need_interface_equalities) {
      lra_atoms_for_check_storage = lra_atoms;
    }
    const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms_for_check =
        lra_atoms_need_interface_equalities ? lra_atoms_for_check_storage : lra_atoms;
    std::vector<AssignedTheoryAtom> lra_assignments =
        assigned_atoms_from_view(model_assignment, lra_atoms);
    std::vector<AssignedTheoryAtom> lra_assignments_for_check = lra_assignments;
    if (lra_atoms_need_interface_equalities) {
      append_true_interface_equalities(true_interface_equalities, lra_atoms_for_check_storage,
                                       lra_assignments_for_check);
    }
    std::optional<sat::Clause> lra_conflict;
    const PreparedLraTheory* prepared_lra_theory_for_check = nullptr;
    const theory::arithmetic::LraTableau* checked_lra_tableau = nullptr;
    std::optional<LraTheoryCheckResult> round_lra_check;
#if PRIMO_HAVE_IPASIR
    if (true_interface_equalities.empty() && online_lra_propagator.has_value()) {
      statistics.increment(":no.interface.lra-prepared-reused");
      statistics.increment(":no.interface.lra-online-final-checks");
      lra_conflict = online_lra_propagator->conflict();
      prepared_lra_theory_for_check = &base_prepared_lra_theory;
      checked_lra_tableau = &online_lra_propagator->tableau();
    } else
#endif
    {
      prepared_lra_theory_for_check = &base_prepared_lra_theory;
      if (true_interface_equalities.empty()) {
        statistics.increment(":no.interface.lra-prepared-reused");
        round_lra_check.emplace(check_incremental_lra_theory(*prepared_lra_theory_for_check,
                                                             lra_assignments_for_check));
        lra_conflict = round_lra_check->conflict;
        checked_lra_tableau = &round_lra_check->tableau;
      } else {
        if (lra_model_epoch.prepared.has_value() &&
            same_interface_equality_atoms(lra_model_epoch.true_interface_equalities,
                                          true_interface_equalities)) {
          statistics.increment(":no.interface.lra-model-epoch-prepared-hits");
        } else {
          if (lra_model_epoch.prepared.has_value()) {
            statistics.increment(":no.interface.lra-model-epoch-invalidations");
          }
          assign_interface_equality_atoms(true_interface_equalities,
                                          lra_model_epoch.true_interface_equalities);
          lra_model_epoch.prepared.emplace(
              terms, lra_atoms_for_check, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
              options.lra_sparse_leaving, mixed_fixed_elimination,
              options.produce_model || !shared_real_terms.empty(),
              options.lra_least_violated_leaving, lra_tuning_from_options(options.lra_tuning));
          lra_model_epoch.checked_assignments.clear();
          lra_model_epoch.tableau = lra_model_epoch.prepared->base_tableau;
          lra_model_epoch.conflict.reset();
          lra_model_epoch.check_valid = false;
          statistics.increment(":no.interface.lra-model-epoch-prepared-misses");
          statistics.increment(":no.interface.lra-prepared-augmented");
        }
        prepared_lra_theory_for_check = &lra_model_epoch.prepared.value();
        if (lra_model_epoch.check_valid &&
            same_theory_assignments(lra_model_epoch.checked_assignments,
                                    lra_assignments_for_check)) {
          statistics.increment(":no.interface.lra-model-epoch-check-hits");
        } else {
          if (lra_model_epoch.check_valid) {
            lra_model_epoch.tableau->pop(0);
            statistics.increment(":no.interface.lra-model-epoch-tableau-reuses");
          }
          lra_model_epoch.conflict =
              check_lra_theory_in_place(*prepared_lra_theory_for_check, lra_assignments_for_check,
                                        lra_model_epoch.tableau.value());
          lra_model_epoch.checked_assignments = lra_assignments_for_check;
          lra_model_epoch.check_valid = true;
          statistics.increment(":no.interface.lra-model-epoch-check-misses");
        }
        lra_conflict = lra_model_epoch.conflict;
        checked_lra_tableau = &lra_model_epoch.tableau.value();
      }
    }
    if (lra_conflict.has_value()) {
      statistics.increment(kStatLraTheoryConflicts);
      record_theory_conflict(statistics);
      if (auto stop = terminator.stop_outcome()) return *stop;
      statistics.increment(":no.interface.lra-conflict-lits", lra_conflict->size());
      record_interface_conflict(lra_conflict, no_manager, ":no.interface.lra-conflicts",
                                statistics);
      model_equality_arrangement.record_theory_conflict(active_model_equality_propagation_start,
                                                        statistics);
      if (lra_conflict->empty()) {
        publish_sat_stats(backend.statistics(), statistics);
        return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
      }
      backend.add_clause(lra_conflict.value());
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    std::vector<TermPairKey> candidate_final_scan_pairs;
    std::optional<LraModelEqualPartition> lra_model_partition;
    std::vector<core::TermId> candidate_lra_implied_equalities;
    bool candidate_lra_implied_equalities_had_internal_model_equalities = false;
    std::vector<core::TermId> lra_implied_equalities_for_model;
    bool lra_implied_equalities_for_model_complete = false;
    bool shared_model_exact_fallback = false;
    if (euf_model.has_value() && !shared_real_terms.empty()) {
      std::vector<core::TermId> mutation_terms = shared_real_terms;
      if (options.nelson_oppen_model_equality_mutation_scope == ModelEqualityMutationScope::Care) {
        mutation_terms.clear();
        mutation_terms.reserve(euf_care.term_pairs.size() * 2);
        for (const TermPairKey& pair : euf_care.term_pairs) {
          mutation_terms.push_back(pair.lhs);
          mutation_terms.push_back(pair.rhs);
        }
        std::sort(mutation_terms.begin(), mutation_terms.end());
        mutation_terms.erase(std::unique(mutation_terms.begin(), mutation_terms.end()),
                             mutation_terms.end());
      }
      lra_model_partition = lra_model_equal_partition(
          *prepared_lra_theory_for_check, *checked_lra_tableau, lra_assignments_for_check,
          shared_real_terms, false_interface_pairs, mutation_terms,
          options.nelson_oppen_model_equality_mutation_scope, model_mutation_enabled,
          options.produce_model && options.qf_uflra_model_reconciliation, statistics);
      if (options.qf_uflra_model_reconciliation && lra_model_partition->unmapped_terms.empty() &&
          !lra_model_partition->unresolved_forbidden_pairs.empty()) {
        const RealModelSeparationResult targeted_separation =
            targeted_shared_real_model_separation_atoms(
                terms, lra_atoms_for_check, lra_assignments_for_check,
                lra_model_partition->unresolved_forbidden_pairs, options, statistics);
        if (targeted_separation.status == RealModelSeparationStatus::ProbeLimit) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA mandatory disequality separation hit the LRA extra-atom probe limit",
              statistics, ""};
        }
        if (targeted_separation.status == RealModelSeparationStatus::Success) {
          const std::optional<LraModelState> separated_model =
              build_lra_model_state(terms, lra_atoms_for_check, lra_assignments_for_check,
                                    targeted_separation.atoms, options);
          if (separated_model.has_value()) {
            lra_model_partition = lra_model_equal_partition(
                separated_model->prepared, separated_model->tableau, separated_model->assignments,
                shared_real_terms, false_interface_pairs, shared_real_terms,
                options.nelson_oppen_model_equality_mutation_scope, false, options.produce_model,
                statistics);
          }
          if (separated_model.has_value() &&
              lra_model_partition->unresolved_forbidden_pairs.empty()) {
            statistics.increment(":no.shared-model.targeted-separation-successes");
            for (const core::TermId atom : targeted_separation.atoms) {
              no_manager.record_lra_separation_atom(atom);
            }
          } else {
            statistics.increment(":no.shared-model.targeted-separation-failures");
          }
        } else {
          statistics.increment(":no.shared-model.targeted-separation-failures");
        }
      }
      if (options.nelson_oppen_model_equality_branching && model_mutation_enabled) {
        statistics.increment(":no.interface.model-equality-mutation-checks",
                             lra_model_partition->candidate_pairs_before_mutation);
        statistics.increment(":no.interface.model-equality-mutated",
                             lra_model_partition->candidate_pairs_before_mutation >
                                     lra_model_partition->candidate_pair_count
                                 ? lra_model_partition->candidate_pairs_before_mutation -
                                       lra_model_partition->candidate_pair_count
                                 : 0);
      }
      if (options.qf_uflra_model_reconciliation) {
        std::vector<core::TermId> lra_mapped_terms(lra_model_partition->mapped_terms.begin(),
                                                   lra_model_partition->mapped_terms.end());
        std::sort(lra_mapped_terms.begin(), lra_mapped_terms.end());
        no_manager.begin_shared_model_iteration(lra_model_partition->classes, lra_mapped_terms);
        statistics.increment(":no.shared-model.iterations");
        statistics.increment(":no.shared-model.lra-value-classes",
                             lra_model_partition->classes.size());
        statistics.increment(":no.shared-model.lra-spanning-equalities",
                             no_manager.shared_model().lra_model_spanning_equalities.size());
      }
    }
    if (candidate_final_scan_enabled && euf_model.has_value() && !shared_real_terms.empty()) {
      candidate_final_scan_pairs = qf_uflra_candidate_final_scan_pairs(
          terms, euf_atoms, lra_assignments_for_check, euf_care, shared_real_terms);
      statistics.set(":no.interface.candidate-final-scan-pairs", candidate_final_scan_pairs.size());
      statistics.set(":no.interface.candidate-final-scan-skipped-pairs",
                     no_manager.summary().candidate_pairs > candidate_final_scan_pairs.size()
                         ? no_manager.summary().candidate_pairs - candidate_final_scan_pairs.size()
                         : 0);
    }

    if (euf_model.has_value() && !shared_real_terms.empty()) {
      statistics.increment(":no.interface.convex-rounds");
      const std::vector<ExplainedInterfaceEquality> implied_equalities =
          euf_implied_real_equalities(terms, euf_model.value(), shared_real_terms, statistics);
      statistics.increment(":no.interface.euf-spanning-equalities", implied_equalities.size());
      statistics.increment(":no.interface.equalities-propagated", implied_equalities.size());
      statistics.increment(":no.interface.euf-to-lra-equalities", implied_equalities.size());
      const InterfacePropagationResult propagation = force_implied_interface_equalities(
          terms, clausifier, backend, no_manager, implied_equalities, euf_assignments_for_check,
          InterfaceEqualityClassification::EufImplied, statistics);
      publish_interface_propagation_result(propagation, ":no.interface.euf-to-lra", statistics);
      if (propagation.triggered_recheck()) {
        if (model_equality_arrangement.has_active_branch()) {
          model_equality_arrangement.record_rejection(std::nullopt, statistics);
        }
        publish_sat_stats(backend.statistics(), statistics);
        continue;
      }
    }

    if (euf_model.has_value() && !shared_real_terms.empty()) {
      if (options.nelson_oppen_propagation != NelsonOppenPropagationMode::Exact) {
        QfUflraLraEqualityProducer equality_producer = options.qf_uflra_lra_equality_producer;
        if (equality_producer == QfUflraLraEqualityProducer::Adaptive) {
          statistics.increment(":no.interface.lra-adaptive-selected");
          if (adaptive_legacy_bootstrap_active) {
            equality_producer = QfUflraLraEqualityProducer::Legacy;
            statistics.increment(":no.interface.lra-adaptive-bootstrap-legacy-rounds");
            statistics.increment(":no.interface.lra-adaptive-legacy-rounds");
          } else if (adaptive_legacy_equality_producer) {
            equality_producer = QfUflraLraEqualityProducer::Legacy;
            statistics.increment(":no.interface.lra-adaptive-legacy-rounds");
          } else {
            equality_producer = QfUflraLraEqualityProducer::ModelUnit;
            adaptive_model_unit_attempted = true;
            statistics.increment(":no.interface.lra-adaptive-model-unit-rounds");
          }
        }
        const std::vector<ExplainedInterfaceEquality> opportunistic_equalities =
            lra_entailed_shared_equalities(terms, *prepared_lra_theory_for_check,
                                           *checked_lra_tableau, shared_real_terms,
                                           equality_producer, statistics);
        statistics.increment(":no.interface.equalities-propagated",
                             opportunistic_equalities.size());
        statistics.increment(":no.interface.lra-to-euf-equalities",
                             opportunistic_equalities.size());
        const LraToEufEqualityFilterResult opportunistic_filtered =
            filter_lra_implied_equalities_for_euf(
                terms, no_manager, euf_model.value(), opportunistic_equalities,
                options.qf_uflra_model_reconciliation, statistics);
        if (learn_shared_model_direct_conflict(opportunistic_filtered, backend, statistics)) {
          model_equality_arrangement.record_theory_conflict(active_model_equality_propagation_start,
                                                            statistics);
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        const InterfacePropagationResult opportunistic_propagation =
            force_implied_interface_equalities(
                terms, clausifier, backend, no_manager, opportunistic_filtered.forced_equalities,
                lra_assignments_for_check, InterfaceEqualityClassification::LraImplied, statistics);
        publish_interface_propagation_result(opportunistic_propagation,
                                             ":no.interface.lra-opportunistic", statistics);
        if (opportunistic_propagation.triggered_recheck()) {
          if (model_equality_arrangement.has_active_branch()) {
            model_equality_arrangement.record_rejection(std::nullopt, statistics);
          }
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        if (options.qf_uflra_lra_equality_producer == QfUflraLraEqualityProducer::Adaptive &&
            adaptive_legacy_bootstrap_active) {
          adaptive_legacy_bootstrap_active = false;
          statistics.increment(":no.interface.lra-adaptive-bootstrap-completions");
        }
      } else {
        statistics.increment(":no.interface.lra-opportunistic-skipped-rounds");
      }

      if (options.qf_uflra_model_reconciliation) {
        statistics.increment(":no.shared-model.compatibility-checks");
        if (!lra_model_partition->unmapped_terms.empty() ||
            !lra_model_partition->unresolved_forbidden_pairs.empty()) {
          shared_model_exact_fallback = true;
          statistics.increment(":no.shared-model.exact-fallbacks");
        } else {
          SharedModelReconciliationResult reconciliation;
          if (options.experimental_generic_model_combination) {
            const core::SortId real_sort = terms.symbols().real_sort();
            const std::optional<theory::TheoryModelProjection> euf_projection =
                project_euf_shared_model(euf_model.value(), shared_real_terms, real_sort,
                                         generic_assignment_epoch);
            if (!euf_projection.has_value()) {
              reconciliation.requires_exact_fallback = true;
            } else {
              CheckedProjectionComponent lra_component(
                  "lra", real_sort, theory::Convexity::Convex,
                  project_lra_shared_model(lra_model_partition.value(), real_sort,
                                           generic_assignment_epoch));
              CheckedProjectionComponent euf_component(
                  "euf", real_sort, theory::Convexity::Convex, euf_projection.value(), {},
                  [&](const theory::TheoryMutationRequest& request) {
                    const SharedModelReconciliationResult mutation =
                        reconcile_lra_candidate_with_euf(euf_model.value(), no_manager, statistics);
                    if (!mutation.compatible) {
                      theory::TheoryMutationResult result;
                      result.candidate_decisions.reserve(mutation.split_pairs.size());
                      for (const TermPairKey& pair : mutation.split_pairs) {
                        result.candidate_decisions.push_back(theory::TheoryDecisionRequest{
                            .pair = {.lhs = pair.lhs, .rhs = pair.rhs, .sort = real_sort},
                            .preferred_value = theory::InterfaceRelationValue::Equal,
                            .reasons = {}});
                      }
                      return result;
                    }
                    const std::optional<theory::TheoryModelProjection> projection =
                        project_euf_shared_model(euf_model.value(), shared_real_terms, real_sort,
                                                 generic_assignment_epoch,
                                                 request.source.revision + 1);
                    if (!projection.has_value()) {
                      return theory::TheoryMutationResult{
                          .status = theory::TheoryMutationStatus::Unknown,
                          .model = {},
                          .candidate_decisions = {},
                          .diagnostic = "EUF mutation lost a shared model term"};
                    }
                    return theory::TheoryMutationResult{
                        .status = theory::TheoryMutationStatus::Mutated,
                        .model = projection.value(),
                        .candidate_decisions = {},
                        .diagnostic = {}};
                  });
              const std::vector<ModelBasedComponentRound> components{
                  {.solver = &lra_component, .assignment = {}},
                  {.solver = &euf_component, .assignment = {}}};
              std::vector<theory::InterfaceRelation> arrangement;
              arrangement.reserve(true_interface_equalities.size() +
                                  false_interface_equalities.size());
              const auto append_relation = [&arrangement, real_sort](
                                               const InterfaceEqualityRegistryEntry& equality,
                                               theory::InterfaceRelationValue value) {
                arrangement.push_back(theory::InterfaceRelation{.lhs = equality.lhs,
                                                                .rhs = equality.rhs,
                                                                .sort = real_sort,
                                                                .equality_lit = equality.lit,
                                                                .value = value});
              };
              for (const InterfaceEqualityRegistryEntry& equality : true_interface_equalities) {
                append_relation(equality, theory::InterfaceRelationValue::Equal);
              }
              for (const InterfaceEqualityRegistryEntry& equality : false_interface_equalities) {
                append_relation(equality, theory::InterfaceRelationValue::Disequal);
              }
              const std::vector shared_terms{
                  SharedInterfaceTerms{.sort = real_sort, .terms = shared_real_terms}};
              const ModelBasedCombinationResult generic_result =
                  generic_model_coordinator.reconcile(
                      generic_assignment_epoch, components, arrangement, shared_terms,
                      [&](const theory::InterfacePair& pair) {
                        return materialize_shared_model_split(
                            terms, clausifier, backend, no_manager,
                            canonical_term_pair(pair.lhs, pair.rhs), statistics);
                      });
              publish_model_based_combination_statistics(generic_model_coordinator.statistics(),
                                                         statistics);
              switch (generic_result.status) {
                case ModelBasedCombinationStatus::Accept:
                  reconciliation.compatible = true;
                  break;
                case ModelBasedCombinationStatus::DecideEquality:
                  for (const sat::Lit decision : generic_result.decision_lits) {
                    model_equality_arrangement.enqueue_pending_branch(decision, statistics);
                  }
                  if (options.qf_uflra_lra_equality_producer ==
                          QfUflraLraEqualityProducer::Adaptive &&
                      adaptive_model_unit_attempted && !adaptive_legacy_equality_producer) {
                    adaptive_legacy_equality_producer = true;
                    statistics.increment(":no.interface.lra-adaptive-fallback-activations");
                  }
                  publish_sat_stats(backend.statistics(), statistics);
                  continue;
                case ModelBasedCombinationStatus::LearnClause:
                  if (std::all_of(generic_result.clause.begin(), generic_result.clause.end(),
                                  [&](sat::Lit literal) {
                                    const auto value = backend.value(std::abs(literal.value));
                                    return value && *value != (literal.value > 0);
                                  }))
                    record_theory_conflict(statistics);
                  if (auto stop = terminator.stop_outcome()) return *stop;
                  if (generic_result.clause.empty()) {
                    return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
                  }
                  backend.add_clause(generic_result.clause);
                  publish_sat_stats(backend.statistics(), statistics);
                  continue;
                case ModelBasedCombinationStatus::Cancelled:
                  return CheckSatOutcome{CheckSatResult::Unknown, "cancelled", statistics, "",
                                         InconclusiveReason::Cancelled};
                case ModelBasedCombinationStatus::ResourceLimit:
                  return CheckSatOutcome{CheckSatResult::Unknown, generic_result.diagnostic,
                                         statistics, "", InconclusiveReason::ResourceLimit};
                case ModelBasedCombinationStatus::Unsupported:
                  return CheckSatOutcome{CheckSatResult::Unsupported, generic_result.diagnostic,
                                         statistics, "", InconclusiveReason::Unsupported};
                case ModelBasedCombinationStatus::Unknown:
                  return CheckSatOutcome{CheckSatResult::Unknown, generic_result.diagnostic,
                                         statistics, "", InconclusiveReason::Unknown};
                case ModelBasedCombinationStatus::PropagateEqualities:
                  throw std::logic_error("legacy QF_UFLRA coordinator returned a fresh equality");
                case ModelBasedCombinationStatus::FreshDecision:
                  throw std::logic_error("convex QF_UFLRA component requested a fresh split");
              }
            }
          } else {
            reconciliation =
                reconcile_lra_candidate_with_euf(euf_model.value(), no_manager, statistics);
          }
          shared_model_exact_fallback = reconciliation.requires_exact_fallback;
          if (shared_model_exact_fallback) {
            statistics.increment(":no.shared-model.exact-fallbacks");
          } else if (!reconciliation.split_pairs.empty()) {
            std::size_t materialized = 0;
            for (const TermPairKey& split_pair : reconciliation.split_pairs) {
              const std::optional<sat::Lit> split = materialize_shared_model_split(
                  terms, clausifier, backend, no_manager, split_pair, statistics);
              if (split.has_value()) {
                model_equality_arrangement.enqueue_pending_branch(split.value(), statistics);
                ++materialized;
              }
            }
            if (materialized != 0) {
              if (options.qf_uflra_lra_equality_producer == QfUflraLraEqualityProducer::Adaptive &&
                  adaptive_model_unit_attempted && !adaptive_legacy_equality_producer) {
                adaptive_legacy_equality_producer = true;
                statistics.increment(":no.interface.lra-adaptive-fallback-activations");
              }
              publish_sat_stats(backend.statistics(), statistics);
              continue;
            }
            shared_model_exact_fallback = true;
            statistics.increment(":no.shared-model.exact-fallbacks");
          } else if (reconciliation.compatible) {
            statistics.increment(":no.online.final-lra-equality-scan-skipped");
            if (!options.produce_model) {
              statistics.increment(":no.shared-model.converged-rounds");
              publish_sat_stats(backend.statistics(), statistics);
              if (replay_changed_assignment()) continue;
              return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
            }

            RealModelValues real_values = lra_model_partition->concrete_values;
            if (!complete_euf_real_model_values(euf_model.value(),
                                                euf_model_real_terms_before_interface_filter,
                                                shared_real_terms, real_values, statistics)) {
              statistics.increment(":no.shared-model.validation-failures");
              throw core::ModelError("QF_UFLRA cannot assign consistent EUF-side Real values");
            }
            std::vector<TheoryModelAssignment> uf_model_assignments;
            uf_model_assignments.reserve(uf_model_assignments.size() + euf_assignments.size() +
                                         no_manager.shared_model().internal_equalities.size());
            for (const AssignedTheoryAtom& atom : euf_assignments) {
              uf_model_assignments.push_back(
                  TheoryModelAssignment{atom.term, atom.value, atom.filtered});
            }
            for (const core::TermId equality :
                 shared_model_internal_equality_terms(terms, no_manager)) {
              uf_model_assignments.push_back(TheoryModelAssignment{equality, true, false});
            }
            const auto model = build_typed_model(terms, assertions, uf_model_assignments,
                                                 real_values, conversions, nullptr, &statistics);
            statistics.increment(":no.shared-model.reconstructed-models");
            statistics.increment(":no.shared-model.converged-rounds");
            publish_sat_stats(backend.statistics(), statistics);
            if (replay_changed_assignment()) continue;
            return CheckSatOutcome{CheckSatResult::Sat,      "",   statistics, "",
                                   InconclusiveReason::None, model};
          }
        }
      }

      if (candidate_final_scan_enabled) {
        const std::vector<TermPairKey> model_filtered_candidate_pairs =
            filter_lra_model_candidate_pairs(candidate_final_scan_pairs,
                                             lra_model_partition.value(), statistics);
        const std::vector<TermPairKey> fallback_pairs = unresolved_interface_pairs(
            euf_model.value(), no_manager, model_filtered_candidate_pairs, statistics);
        const LraImpliedRealEqualitiesResult implied_equalities =
            lra_implied_real_equalities_for_pairs(
                terms, lra_atoms_for_check, *prepared_lra_theory_for_check, *checked_lra_tableau,
                lra_assignments_for_check, fallback_pairs, options, &statistics);
        if (implied_equalities.probe_limit_hit) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA candidate LRA-to-EUF equality scan hit the LRA extra-atom probe "
              "limit",
              statistics, ""};
        }
        candidate_lra_implied_equalities = interface_equality_terms(implied_equalities.equalities);
        statistics.increment(":no.interface.equalities-propagated",
                             implied_equalities.equalities.size());
        statistics.increment(":no.interface.lra-to-euf-equalities",
                             implied_equalities.equalities.size());
        statistics.increment(":no.interface.candidate-final-scan-equalities",
                             implied_equalities.equalities.size());
        statistics.increment(":no.interface.lra-exact-fallback-equalities",
                             implied_equalities.equalities.size());
        const LraToEufEqualityFilterResult filtered_equalities =
            filter_lra_implied_equalities_for_euf(
                terms, no_manager, euf_model.value(), implied_equalities.equalities,
                options.qf_uflra_model_reconciliation, statistics);
        if (learn_shared_model_direct_conflict(filtered_equalities, backend, statistics)) {
          model_equality_arrangement.record_theory_conflict(active_model_equality_propagation_start,
                                                            statistics);
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        candidate_lra_implied_equalities = filtered_equalities.model_equalities;
        candidate_lra_implied_equalities_had_internal_model_equalities =
            filtered_equalities.mergeable_skipped > 0;
        const InterfacePropagationResult propagation = force_implied_interface_equalities(
            terms, clausifier, backend, no_manager, filtered_equalities.forced_equalities,
            lra_assignments_for_check, InterfaceEqualityClassification::LraImplied, statistics);
        publish_interface_propagation_result(propagation, ":no.interface.lra-to-euf", statistics);
        if (propagation.triggered_recheck()) {
          if (model_equality_arrangement.has_active_branch()) {
            model_equality_arrangement.record_rejection(std::nullopt, statistics);
          }
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
      } else if (!options.qf_uflra_model_reconciliation || shared_model_exact_fallback) {
        const LraImpliedRealEqualitiesResult implied_equalities =
            lra_implied_real_equalities_for_partition(
                terms, lra_atoms_for_check, *prepared_lra_theory_for_check, *checked_lra_tableau,
                lra_assignments_for_check, lra_model_partition.value(), euf_model.value(),
                no_manager, options, statistics);
        if (implied_equalities.probe_limit_hit) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA LRA-to-EUF equality scan hit the LRA extra-atom probe limit",
              statistics, ""};
        }
        statistics.increment(":no.interface.equalities-propagated",
                             implied_equalities.equalities.size());
        statistics.increment(":no.interface.lra-to-euf-equalities",
                             implied_equalities.equalities.size());
        statistics.increment(":no.interface.lra-exact-fallback-equalities",
                             implied_equalities.equalities.size());
        const LraToEufEqualityFilterResult filtered_equalities =
            filter_lra_implied_equalities_for_euf(
                terms, no_manager, euf_model.value(), implied_equalities.equalities,
                options.qf_uflra_model_reconciliation, statistics);
        if (learn_shared_model_direct_conflict(filtered_equalities, backend, statistics)) {
          model_equality_arrangement.record_theory_conflict(active_model_equality_propagation_start,
                                                            statistics);
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        append_unique_equalities_by_pair(terms, lra_implied_equalities_for_model,
                                         filtered_equalities.model_equalities);
        lra_implied_equalities_for_model_complete = true;
        const InterfacePropagationResult propagation = force_implied_interface_equalities(
            terms, clausifier, backend, no_manager, filtered_equalities.forced_equalities,
            lra_assignments_for_check, InterfaceEqualityClassification::LraImplied, statistics);
        publish_interface_propagation_result(propagation, ":no.interface.lra-to-euf", statistics);
        if (propagation.triggered_recheck()) {
          if (model_equality_arrangement.has_active_branch()) {
            model_equality_arrangement.record_rejection(std::nullopt, statistics);
          }
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
      }
    }

    ModelEqualityBranchChoice model_equality_branch;
    if (lra_model_partition.has_value() && !options.qf_uflra_model_reconciliation) {
      model_equality_branch = choose_lra_model_equality_branch(
          terms, clausifier, backend, no_manager, lra_model_partition.value(),
          shared_real_pair_count, euf_care, options, model_equality_candidate_queue, statistics);
    }
    if (model_equality_branch.pending_branch.has_value()) {
      model_equality_arrangement.enqueue_pending_branch(
          model_equality_branch.pending_branch.value(), statistics);
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    publish_sat_stats(backend.statistics(), statistics);
    if (!options.produce_model && euf_model.has_value()) {
      if (!shared_real_terms.empty()) {
        statistics.increment(":no.interface.arrangement-checks");
        const ForcedModelEqualityResult forced = compute_forced_model_equalities(
            terms, clausifier, backend, no_manager, model_equality_arrangement,
            ForcedModelEqualityInputs{
                euf_model.value(), shared_real_terms, lra_atoms, lra_assignments,
                LraCheckInputs{lra_atoms_for_check, prepared_lra_theory_for_check,
                               checked_lra_tableau, lra_assignments_for_check},
                lra_model_partition, candidate_final_scan_enabled, candidate_lra_implied_equalities,
                candidate_lra_implied_equalities_had_internal_model_equalities,
                lra_implied_equalities_for_model_complete, lra_implied_equalities_for_model,
                active_model_equality_propagation_start, false},
            options, statistics);
        if (forced.status == ForcedModelEqualityStatus::Recheck) {
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        if (forced.status == ForcedModelEqualityStatus::EqualityScanProbeLimit) {
          statistics.increment(":no.interface.arrangement-failures");
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA final arrangement equality scan hit the LRA extra-atom probe "
              "limit",
              statistics, ""};
        }
        if (forced.status == ForcedModelEqualityStatus::SeparationProbeLimit) {
          statistics.increment(":no.interface.arrangement-failures");
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA final arrangement hit the LRA extra-atom probe limit", statistics,
              ""};
        }
        if (forced.status == ForcedModelEqualityStatus::SeparationFailed) {
          statistics.increment(":no.interface.arrangement-failures");
          if (options.qf_uflra_model_reconciliation) {
            statistics.increment(":no.shared-model.validation-failures");
          }
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA final arrangement could not separate Real argument constants",
              statistics, ""};
        }
        const RealModelSeparationResult& separation_atoms = forced.separation;
        statistics.set(":no.interface.arrangement-separations", separation_atoms.atoms.size());
        if (options.qf_uflra_model_reconciliation) {
          for (const core::TermId atom : separation_atoms.atoms) {
            no_manager.record_lra_separation_atom(atom);
          }
          statistics.increment(":no.shared-model.lra-separations", separation_atoms.atoms.size());
          statistics.increment(":no.shared-model.converged-rounds");
        }
      }
    }
    if (options.produce_model) {
      std::vector<core::TermId> euf_implied_equalities;
      std::vector<core::TermId> lra_implied_equalities_for_euf;
      std::vector<core::TermId> extra_lra_model_atoms;
      if (euf_model.has_value() && !shared_real_terms.empty()) {
        const ForcedModelEqualityResult forced = compute_forced_model_equalities(
            terms, clausifier, backend, no_manager, model_equality_arrangement,
            ForcedModelEqualityInputs{
                euf_model.value(), shared_real_terms, lra_atoms, lra_assignments,
                LraCheckInputs{lra_atoms_for_check, prepared_lra_theory_for_check,
                               checked_lra_tableau, lra_assignments_for_check},
                lra_model_partition, candidate_final_scan_enabled, candidate_lra_implied_equalities,
                candidate_lra_implied_equalities_had_internal_model_equalities,
                lra_implied_equalities_for_model_complete, lra_implied_equalities_for_model,
                active_model_equality_propagation_start, true},
            options, statistics);
        if (forced.status == ForcedModelEqualityStatus::Recheck) {
          publish_sat_stats(backend.statistics(), statistics);
          continue;
        }
        if (forced.status == ForcedModelEqualityStatus::EqualityScanProbeLimit) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA model reconstruction equality scan hit the LRA extra-atom probe "
              "limit",
              statistics, ""};
        }
        if (forced.status == ForcedModelEqualityStatus::SeparationProbeLimit) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA model reconstruction hit the LRA extra-atom probe limit", statistics,
              ""};
        }
        if (forced.status == ForcedModelEqualityStatus::SeparationFailed) {
          if (options.qf_uflra_model_reconciliation) {
            statistics.increment(":no.shared-model.validation-failures");
          }
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA model reconstruction could not separate Real argument constants",
              statistics, ""};
        }
        // The model assignments below are built from these two lists.
        euf_implied_equalities = forced.euf_equalities;
        lra_implied_equalities_for_euf = forced.lra_equalities;
        extra_lra_model_atoms = euf_implied_equalities;
        const RealModelSeparationResult& separation_atoms = forced.separation;
        extra_lra_model_atoms.insert(extra_lra_model_atoms.end(), separation_atoms.atoms.begin(),
                                     separation_atoms.atoms.end());
        if (options.qf_uflra_model_reconciliation) {
          for (const core::TermId atom : separation_atoms.atoms) {
            no_manager.record_lra_separation_atom(atom);
          }
          statistics.increment(":no.shared-model.lra-separations", separation_atoms.atoms.size());
        }
        statistics.set(":no.interface.model-separations", separation_atoms.atoms.size());
      } else {
        statistics.set(":no.interface.model-separations", 0);
        statistics.set(":no.interface.model-separation-candidates", 0);
        statistics.set(":no.interface.model-separation-euf-mergeable-pairs", 0);
        statistics.set(":no.interface.model-separation-euf-forbidden-pairs", 0);
        statistics.set(":no.interface.model-separation-search-capped", 0);
        statistics.set(":no.interface.model-separation-search-branches", 0);
        statistics.set(":no.interface.model-separation-search-backtracks", 0);
        statistics.set(":no.interface.model-separation-model-oriented", 0);
      }

      std::optional<LraModelState> lra_model = build_lra_model_state(
          terms, lra_atoms_for_check, lra_assignments_for_check, extra_lra_model_atoms, options);
      if (!lra_model.has_value()) {
        if (options.qf_uflra_model_reconciliation) {
          statistics.increment(":no.shared-model.validation-failures");
        }
        throw core::ModelError("QF_UFLRA reconstruction found an LRA conflict");
      }
      RealModelValues real_values = real_model_values_from_lra(
          model_terms, lra_model->prepared, lra_model->tableau, lra_model->assignments);
      if (euf_model.has_value() &&
          !complete_euf_real_model_values(euf_model.value(),
                                          euf_model_real_terms_before_interface_filter,
                                          shared_real_terms, real_values, statistics)) {
        if (options.qf_uflra_model_reconciliation) {
          statistics.increment(":no.shared-model.validation-failures");
        }
        throw core::ModelError("QF_UFLRA cannot assign consistent EUF-side Real values");
      }
      if (options.qf_uflra_model_reconciliation) {
        statistics.increment(":no.shared-model.reconstructed-models");
        statistics.increment(":no.shared-model.converged-rounds");
      }
      std::vector<TheoryModelAssignment> uf_model_assignments;
      uf_model_assignments.reserve(uf_model_assignments.size() + euf_assignments.size() +
                                   lra_implied_equalities_for_euf.size());
      for (const AssignedTheoryAtom& atom : euf_assignments) {
        uf_model_assignments.push_back(TheoryModelAssignment{atom.term, atom.value, atom.filtered});
      }
      for (const core::TermId equality : lra_implied_equalities_for_euf) {
        uf_model_assignments.push_back(TheoryModelAssignment{equality, true, false});
      }
      if (replay_changed_assignment()) continue;
      return CheckSatOutcome{CheckSatResult::Sat,
                             "",
                             statistics,
                             "",
                             InconclusiveReason::None,
                             build_typed_model(terms, assertions, uf_model_assignments, real_values,
                                               conversions, nullptr, &statistics)};
    }
    if (replay_changed_assignment()) continue;
    return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
  }
}

}  // namespace

namespace {
#if PRIMO_HAVE_IPASIR
// Original activation/preparation contract: docs/assumption-incremental-uf.md.
// Only definitions inhabit prepared_; query facts belong to the fresh callback.
class IncrementalUfEngine final : public IncrementalEngine {
 public:
  IncrementalUfEngine(core::TermManager& terms, const CheckSatOptions& options)
      : terms_(terms),
        backend_(options.cadical_options, true),
        clausifier_(backend_, terms, false),
        true_term_(terms.mk_bool(true)),
        false_term_(terms.mk_bool(false)),
        prepared_(terms, {}, true_term_, false_term_, options.euf_explain_search,
                  options.euf_proof_forest_diagnostics) {}

  void push() override {
    selectors_.push_back({0});
    model_boundaries_.push_back(model_roots_.size());
  }
  void pop() override {
    if (selectors_.empty()) throw std::invalid_argument("engine scope underflow");
    const auto selector = selectors_.back();
    selectors_.pop_back();
    model_roots_.resize(model_boundaries_.back());
    model_boundaries_.pop_back();
    if (selector.value != 0) {
      backend_.add_clause({sat::Lit{-selector.value}});
      ++retired_;
    }
  }
  void append(const std::vector<core::TermId>& assertions) override {
    const auto start = std::chrono::steady_clock::now();
    for (const auto root : assertions) {
      model_roots_.push_back(root);
      if (selectors_.empty()) {
        clausifier_.assert_formula(root);
      } else {
        if (selectors_.back().value == 0) {
          selectors_.back() = {backend_.new_var()};
          backend_.protect_variable(selectors_.back().value);
        }
        clausifier_.assert_guarded_formula(root, selectors_.back());
      }
    }
    append_us_ += elapsed(start);
  }
  CheckSatOutcome check(const std::vector<core::TermId>&, const CheckSatOptions& options) override {
    const auto start = std::chrono::steady_clock::now();
    util::Statistics statistics;
    const auto prepare_start = std::chrono::steady_clock::now();
    // Complete the new registry suffix before freezing it for the callback.
    // Boolean argument meanings survive retirement of roots; see the lead 22
    // checklist in docs/assumption-incremental-uf.md.
    const auto scanned_before = clausifier_.boolean_argument_scan_count();
    statistics.set(":session.euf-new-boolean-arguments",
                   clausifier_.register_boolean_argument_terms());
    statistics.set(":session.euf-registration-atoms-scanned",
                   clausifier_.boolean_argument_scan_count() - scanned_before);
    const auto new_atoms = std::span(clausifier_.theory_atoms()).subspan(prepared_atom_count_);
    const auto appended = prepared_.ensure_theory_atoms(terms_, new_atoms);
    if (appended != new_atoms.size())
      throw std::logic_error("duplicate atom in incremental UF registry suffix");
    prepared_atom_count_ += appended;
    statistics.set(":session.euf-preparation-atoms-scanned", new_atoms.size());
    statistics.set(":session.euf-new-prepared-atoms", appended);
    statistics.set(":time.session.prepare-us", elapsed(prepare_start));
    for (const auto& atom : clausifier_.theory_atoms())
      backend_.protect_variable(std::abs(atom.lit.value));
    statistics.increment(kStatSolverQfUf);
    statistics.set(kStatTheoryAtoms, prepared_.flat_atoms.size());
    statistics.set(kStatPureTheoryFilterEnabled, 0);
    CheckSatOptions resolved = options;
    resolved.pure_theory_filter = PureTheoryFilterMode::Off;
    resolved.euf_propagation =
        resolve_euf_propagation_mode(options, clausifier_.theory_atoms().size(), statistics);
    const auto phase =
        resolve_theory_phase_mode(options.theory_phase, clausifier_.theory_atoms(), statistics);
    for (const auto& atom : clausifier_.theory_atoms()) {
      if (phase == TheoryPhaseMode::Negative ||
          (phase == TheoryPhaseMode::Polarity && atom.occurs_negative && !atom.occurs_positive))
        backend_.phase(sat::Lit{-atom.lit.value});
      else if (phase == TheoryPhaseMode::Positive ||
               (phase == TheoryPhaseMode::Polarity && atom.occurs_positive &&
                !atom.occurs_negative))
        backend_.phase(atom.lit);
    }
    SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
    const ScopedTerminatorConnection termination(backend_, terminator);
    const auto reset_start = std::chrono::steady_clock::now();
    OnlineEufPropagator propagator(terms_, clausifier_.theory_atoms(), true_term_, false_term_,
                                   resolved, options.euf_propagation == EufPropagationMode::Auto,
                                   statistics, &prepared_);
    std::vector<int> observed;
    for (const auto& atom : clausifier_.theory_atoms())
      observed.push_back(std::abs(atom.lit.value));
    const AssignmentConnection assignment_connection(backend_, propagator, observed);
    statistics.set(":time.session.theory-reset-us", elapsed(reset_start));
    CheckSatOutcome outcome;
    outcome.result = CheckSatResult::Unknown;
    const auto solve_start = std::chrono::steady_clock::now();
    while (!terminator.terminate()) {
      for (const auto selector : selectors_)
        if (selector.value != 0) backend_.assume(selector);
      statistics.increment(kStatSatSolveCalls);
      outcome.result = map_sat_result(backend_.solve());
      propagator.flush_incremental_stats();
      if (terminator.terminate() || outcome.result != CheckSatResult::Sat) break;
      statistics.increment(kStatTheoryChecks);
      const auto conflict = propagator.incremental_conflict();
      if (!conflict) break;
      statistics.increment(kStatTheoryConflicts);
      record_theory_conflict(statistics);
      backend_.add_clause(*conflict);
    }
    statistics.set(":time.solve-us", elapsed(solve_start));
    if (auto stop = terminator.stop_outcome()) outcome = *stop;
    if (outcome.result == CheckSatResult::Sat && options.produce_model) {
      const auto model_start = std::chrono::steady_clock::now();
      std::vector<TheoryModelAssignment> assigned;
      for (const auto& atom : clausifier_.theory_atoms()) {
        const auto value = backend_.value(std::abs(atom.lit.value));
        if (!value) throw std::logic_error("SAT backend did not assign observed EUF atom");
        assigned.push_back({atom.term, atom.lit.value > 0 ? *value : !*value, false});
      }
      // Reconstruct the final lowered goal. Original applications over an ITE
      // argument must be evaluated through that model, not completed as new UF
      // classes. SmtSession checks the model against the original assertions.
      outcome.typed_model = build_typed_model(terms_, model_roots_, assigned, {}, {}, nullptr,
                                              &statistics, terms_.symbols().visible_symbols());
      statistics.set(":time.model-us", elapsed(model_start));
    }
    statistics.set(":session.engine-queries", ++queries_);
    statistics.set(":session.theory-state-resets", queries_);
    statistics.set(":session.retired-selectors", retired_);
    statistics.set(":session.active-selectors", static_cast<std::size_t>(std::count_if(
                                                    selectors_.begin(), selectors_.end(),
                                                    [](sat::Lit lit) { return lit.value != 0; })));
    statistics.set(":session.retained-terms", terms_.size());
    statistics.set(":session.active-model-roots", model_roots_.size());
    statistics.set(":session.retained-atoms", prepared_.flat_atoms.size());
    statistics.set(":session.prepared-atoms", prepared_.flat_atoms.size());
    statistics.set(":session.euf-flat-terms", prepared_.flat.size());
    statistics.set(":session.euf-flat-definitions", prepared_.installed_definition_count);
    statistics.set(":session.pure-theory-filter", 0);
    statistics.set(":session.sat-preprocessing", 0);
    statistics.set(":session.sat-inprocessing", 0);
    statistics.set(":time.session.append-us", append_us_);
    append_us_ = 0;
    const auto sat_stats = backend_.statistics();
    publish_sat_stats(sat_stats, statistics);
    statistics.set(":session.clauses-added", sat_stats.clauses);
    statistics.set(":session.retained-active-clauses",
                   sat_stats.active_irredundant_clauses + sat_stats.active_redundant_clauses);
    statistics.set(":session.retained-redundant-clauses", sat_stats.active_redundant_clauses);
    statistics.set(":time.total-us", elapsed(start));
    outcome.statistics = std::move(statistics);
    return outcome;
  }

 private:
  static std::uint64_t elapsed(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count());
  }
  core::TermManager& terms_;
  sat::CadicalBackend backend_;
  BooleanClausifier clausifier_;
  core::TermId true_term_;
  core::TermId false_term_;
  PreparedFlatTheory prepared_;
  std::size_t prepared_atom_count_ = 0;
  std::vector<sat::Lit> selectors_;
  std::vector<core::TermId> model_roots_;
  std::vector<std::size_t> model_boundaries_;
  std::size_t retired_ = 0;
  std::size_t queries_ = 0;
  std::uint64_t append_us_ = 0;
};

// Activation-root encoding and fresh working theory state are specified in
// docs/assumption-incremental-lra.md. The structural base never receives bounds.
class IncrementalLraEngine final : public IncrementalEngine {
 public:
  IncrementalLraEngine(core::TermManager& terms, const CheckSatOptions& options)
      : terms_(terms),
        backend_(options.cadical_options, true),
        clausifier_(backend_, terms, false),
        prepared_(terms, {}, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
                  options.lra_sparse_leaving, false, true, options.lra_least_violated_leaving,
                  lra_tuning_from_options(options.lra_tuning)) {}

  void push() override { selectors_.push_back({0}); }
  void pop() override {
    if (selectors_.empty()) throw std::invalid_argument("engine scope underflow");
    const sat::Lit selector = selectors_.back();
    selectors_.pop_back();
    if (selector.value != 0) {
      backend_.add_clause({sat::Lit{-selector.value}});
      ++retired_;
    }
  }
  void append(const std::vector<core::TermId>& assertions) override {
    const auto start = std::chrono::steady_clock::now();
    for (const core::TermId root : assertions) {
      if (selectors_.empty()) {
        clausifier_.assert_formula(root);
      } else {
        if (selectors_.back().value == 0) {
          selectors_.back() = {backend_.new_var()};
          backend_.protect_variable(selectors_.back().value);
        }
        clausifier_.assert_guarded_formula(root, selectors_.back());
      }
    }
    while (prepared_.atoms.size() < clausifier_.theory_atoms().size()) {
      const auto atom = clausifier_.theory_atoms()[prepared_.atoms.size()];
      backend_.protect_variable(std::abs(atom.lit.value));
      static_cast<void>(prepared_.append_atom(atom));
    }
    append_us_ += elapsed(start);
  }
  CheckSatOutcome check(const std::vector<core::TermId>& model_terms,
                        const CheckSatOptions& options) override {
    const auto start = std::chrono::steady_clock::now();
    util::Statistics statistics;
    ++queries_;
    statistics.set(":session.engine-queries", queries_);
    statistics.set(":session.retired-selectors", retired_);
    statistics.set(":session.active-selectors", static_cast<std::size_t>(std::count_if(
                                                    selectors_.begin(), selectors_.end(),
                                                    [](sat::Lit lit) { return lit.value != 0; })));
    statistics.set(":session.retained-terms", terms_.size());
    statistics.set(":session.retained-atoms", prepared_.atoms.size());
    statistics.set(":session.prepared-atoms", prepared_.atoms.size());
    statistics.set(":time.session.append-us", append_us_);
    append_us_ = 0;
    statistics.set(":session.sat-preprocessing", 0);
    statistics.set(":session.sat-inprocessing", 0);
    statistics.increment(kStatSolverQfLra);
    statistics.set(kStatTheoryAtoms, prepared_.atoms.size());
    SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
    const ScopedTerminatorConnection termination(backend_, terminator);
    const auto reset_start = std::chrono::steady_clock::now();
    OnlineLraPropagator propagator(
        prepared_, clausifier_.theory_atoms(), options.lra_propagation, options.lra_row_propagation,
        options.lra_bidirectional_row_propagation, options.lra_theory_decisions,
        options.lra_model_phase.value_or(false), options.lra_pivoting_rule == LraPivotingRule::Soi,
        options.lra_row_propagation_limits, backend_, statistics, true,
        options.max_theory_conflicts);
    if (options.lra_equality_propagation) {
      propagator.enable_variable_equality_propagation();
    }
    std::vector<int> observed;
    for (const auto& atom : clausifier_.theory_atoms())
      observed.push_back(std::abs(atom.lit.value));
    const AssignmentConnection assignment_connection(backend_, propagator, observed);
    statistics.set(":session.theory-state-resets", ++resets_);
    statistics.set(":time.session.theory-reset-us", elapsed(reset_start));
    CheckSatOutcome outcome;
    if (terminator.terminate()) {
      outcome.result = CheckSatResult::Unknown;
    } else {
      for (const auto selector : selectors_) {
        if (selector.value != 0) backend_.assume(selector);
      }
      statistics.increment(kStatSatSolveCalls);
      const auto solve_start = std::chrono::steady_clock::now();
      outcome.result = map_sat_result(backend_.solve());
      statistics.set(":time.solve-us", elapsed(solve_start));
    }
    static_cast<void>(terminator.terminate());
    if (auto stop = terminator.stop_outcome()) outcome = *stop;
    if (outcome.result == CheckSatResult::Sat && options.produce_model) {
      const auto model_start = std::chrono::steady_clock::now();
      const auto assigned =
          assigned_atoms_from_view(assignment_connection.view(), clausifier_.theory_atoms());
      outcome.typed_model =
          build_qf_lra_model(terms_, model_terms, {}, prepared_, propagator.tableau(), assigned,
                             statistics, terms_.symbols().visible_symbols());
      statistics.set(":time.model-us", elapsed(model_start));
    }
    publish_sat_stats(backend_.statistics(), statistics);
    const auto sat_stats = backend_.statistics();
    statistics.set(":session.clauses-added", sat_stats.clauses);
    statistics.set(":session.retained-active-clauses",
                   sat_stats.active_irredundant_clauses + sat_stats.active_redundant_clauses);
    statistics.set(":session.retained-redundant-clauses", sat_stats.active_redundant_clauses);
    statistics.set(":time.total-us", elapsed(start));
    outcome.statistics = std::move(statistics);
    return outcome;
  }

 private:
  static std::uint64_t elapsed(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count());
  }
  core::TermManager& terms_;
  sat::CadicalBackend backend_;
  BooleanClausifier clausifier_;
  PreparedLraTheory prepared_;
  std::vector<sat::Lit> selectors_;
  std::size_t retired_ = 0;
  std::size_t queries_ = 0;
  std::size_t resets_ = 0;
  std::uint64_t append_us_ = 0;
};
#endif
}  // namespace

std::unique_ptr<IncrementalEngine> make_incremental_uf_engine(core::TermManager& terms,
                                                              const CheckSatOptions& options) {
#if PRIMO_HAVE_IPASIR
  return std::make_unique<IncrementalUfEngine>(terms, options);
#else
  static_cast<void>(terms);
  static_cast<void>(options);
  throw UnsupportedInput("retained solving requires an IPASIR backend");
#endif
}

std::unique_ptr<IncrementalEngine> make_incremental_lra_engine(core::TermManager& terms,
                                                               const CheckSatOptions& options) {
#if PRIMO_HAVE_IPASIR
  return std::make_unique<IncrementalLraEngine>(terms, options);
#else
  static_cast<void>(terms);
  static_cast<void>(options);
  throw UnsupportedInput("retained solving requires an IPASIR backend");
#endif
}

CheckSatOutcome solve_qf_uf_engine(core::TermManager& terms,
                                   const std::vector<core::TermId>& assertions,
                                   const CheckSatOptions& options, util::Statistics& statistics,
                                   const ModelConversionChain& conversions) {
  return enforce_theory_conflict_limit(
      solve_qf_uf_engine_impl(terms, assertions, options, statistics, conversions), options,
      statistics);
}

CheckSatOutcome solve_qf_lra_engine(core::TermManager& terms,
                                    const std::vector<core::TermId>& assertions,
                                    const std::vector<core::TermId>& model_terms,
                                    const ModelConversionChain& conversions,
                                    const CheckSatOptions& options, util::Statistics& statistics) {
  return enforce_theory_conflict_limit(
      solve_qf_lra_engine_impl(terms, assertions, model_terms, conversions, options, statistics),
      options, statistics);
}

CheckSatOutcome solve_qf_lia_engine(core::TermManager& terms,
                                    const std::vector<core::TermId>& assertions,
                                    const std::vector<core::TermId>& model_terms,
                                    const CheckSatOptions& options, const LiaSettings& settings,
                                    util::Statistics& statistics,
                                    const ModelConversionChain& conversions) {
  return enforce_theory_conflict_limit(
      solve_qf_lia_engine_impl(terms, assertions, model_terms, options, settings, statistics,
                               conversions),
      options, statistics);
}

CheckSatOutcome solve_qf_uflra_engine(core::TermManager& terms,
                                      const std::vector<core::TermId>& assertions,
                                      const std::vector<core::TermId>& model_terms,
                                      const ModelConversionChain& conversions,
                                      const CheckSatOptions& options,
                                      util::Statistics& statistics) {
  return enforce_theory_conflict_limit(
      solve_qf_uflra_engine_impl(terms, assertions, model_terms, conversions, options, statistics),
      options, statistics);
}

CheckSatOutcome solve_qf_uflia_engine(core::TermManager& terms,
                                      const std::vector<core::TermId>& assertions,
                                      const std::vector<core::TermId>& model_terms,
                                      const ModelConversionChain& conversions,
                                      const CheckSatOptions& options, const LiaSettings& settings,
                                      util::Statistics& statistics) {
  return enforce_theory_conflict_limit(
      solve_qf_uflia_engine_impl(terms, assertions, model_terms, conversions, options, settings,
                                 statistics),
      options, statistics);
}

void record_ackermann_cost_diagnostics_for_session(const core::TermManager& terms,
                                                   const std::vector<core::TermId>& roots,
                                                   util::Statistics& statistics) {
  record_ackermann_cost_diagnostics(terms, roots, statistics);
}

std::unique_ptr<theory::TheoryComponent> make_uf_euf_component(core::TermManager& terms,
                                                               const CheckSatOptions& options,
                                                               util::Statistics& statistics,
                                                               bool online) {
#if PRIMO_HAVE_IPASIR
  return std::make_unique<EufTheoryComponent>(terms, options, statistics, online,
                                              options.qf_uflia_model_combination,
                                              ":time.qf-uflia.euf-check-us");
#else
  static_cast<void>(terms);
  static_cast<void>(options);
  static_cast<void>(statistics);
  static_cast<void>(online);
  return nullptr;
#endif
}

void register_euf_boolean_arguments(BooleanClausifier& clausifier, util::Statistics& statistics) {
  // Every EUF recipe uses the same Boolean cardinality preparation; see
  // docs/euf-boolean-arguments.md. Generated batches must complete it again
  // before the atom registry is borrowed or observed by a theory consumer.
  statistics.increment(util::StatKey::BooleanDomainRegisteredTerms,
                       clausifier.register_boolean_argument_terms());
}

Tactic make_purify_mixed(PurifyMixedOptions options) {
  return make_goal_tactic("purify-mixed", [options](TacticContext& context, const Goal& goal) {
    const TacticContext::FreshConstantFactory fresh_constant = [&context](core::SortId sort,
                                                                          std::string_view prefix) {
      return context.fresh_constant(sort, prefix);
    };
    const PurificationScope scope{.terms = context.terms(), .fresh_constant = fresh_constant};
    MixedTheoryPurificationResult purification =
        purify_mixed_arithmetic_applications(scope, goal.assertions());
    util::Statistics& statistics = context.statistics();
    statistics.set(":no.purification.definitions",
                   purification.arithmetic_uf_definitions + purification.euf_real_term_definitions);
    statistics.set(":no.purification.arithmetic-uf-definitions",
                   purification.arithmetic_uf_definitions);
    statistics.set(":no.purification.euf-real-term-definitions",
                   purification.euf_real_term_definitions);
    statistics.set(":no.purification.reused-applications", purification.reused_applications);
    statistics.set(":no.purification.reused-real-terms", purification.reused_real_terms);
    statistics.set(":no.purification.memoized-formula-rewrites",
                   purification.memoized_formula_rewrites);
    statistics.set(":no.purification.memoized-arithmetic-rewrites",
                   purification.memoized_arithmetic_rewrites);
    statistics.set(":no.purification.memoized-euf-rewrites", purification.memoized_euf_rewrites);
    statistics.set(":no.purification.skipped-no-sites", purification.skipped_no_sites ? 1 : 0);
    Goal result = goal.with_assertions(std::move(purification.assertions));
    // Keep the purification map for model construction. Without it a UF
    // application's value has to be rediscovered from asserted equalities and
    // value coincidence, which is what bugs/qf-uflia-nested-application-model
    // is; with it the value is a lookup on the label.
    if (context.produce_model() && !purification.application_proxies.empty()) {
      ModelReconstruction reconstruction = result.reconstruction();
      reconstruction.conversions = reconstruction.conversions.appended(
          PurificationProxyConversion{std::move(purification.application_proxies)});
      result = result.with_reconstruction(std::move(reconstruction));
    }
    if (!options.canonicalize_arithmetic_after || purification.skipped_no_sites) {
      return AnyOf::single(std::move(result));
    }
    const Tactic normalize = make_normalize_arithmetic(
        NormalizeArithmeticOptions{.stage = ArithmeticNormalizationStage::PostPurification,
                                   .canonicalize = true,
                                   .lower_real_equalities = false,
                                   .guarded_real_equality_lowering = false});
    return normalize(context, AnyOf::single(std::move(result)));
  });
}

}  // namespace primo::smt
