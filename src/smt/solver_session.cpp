#include "smt/solver_session.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "smt/check_sat.hpp"
#include "smt/cnf_export.hpp"
#include "smt/formula_analysis.hpp"
#include "smt/formula_metrics.hpp"
#include "smt/frontend.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "smt/preprocessing_strategy.hpp"
#include "smt/quantifiers/quantified_solver.hpp"
#include "smt/solver_engines.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_leaves.hpp"
#include "smt/solver_support.hpp"
#include "smt/solver_tactic.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "smt/theory_combination.hpp"
#include "smt/typed_model_builder.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/printer.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

namespace {

constexpr util::StatKey kStatAssertionsInitial = util::StatKey::AssertionsInitial;
constexpr util::StatKey kStatAssertionsFinal = util::StatKey::AssertionsFinal;
constexpr util::StatKey kStatPreprocessingPasses = util::StatKey::PreprocessingPasses;
constexpr util::StatKey kStatPreprocessingChangedPasses = util::StatKey::PreprocessingChangedPasses;
constexpr util::StatKey kStatPreprocessingAdaptivePasses =
    util::StatKey::PreprocessingAdaptivePasses;
constexpr util::StatKey kStatPreprocessingTermSizeInitial =
    util::StatKey::PreprocessingTermSizeInitial;
constexpr util::StatKey kStatPreprocessingTermSizeFinal = util::StatKey::PreprocessingTermSizeFinal;
constexpr util::StatKey kStatEqBridgeEqualities = util::StatKey::EqBridgeEqualities;
constexpr util::StatKey kStatEqBridgeSkippedOrNodes = util::StatKey::EqBridgeSkippedOrNodes;
constexpr util::StatKey kStatBooleanUnitFacts = util::StatKey::BooleanUnitFacts;
constexpr util::StatKey kStatBooleanUnitRewrites = util::StatKey::BooleanUnitRewrites;
constexpr util::StatKey kStatForcedAtomFacts = util::StatKey::ForcedAtomFacts;
constexpr util::StatKey kStatForcedAtomDisequalities = util::StatKey::ForcedAtomDisequalities;
constexpr util::StatKey kStatForcedAtomRewrites = util::StatKey::ForcedAtomRewrites;
constexpr util::StatKey kStatUnitEqualityConsequences = util::StatKey::UnitEqualityConsequences;
constexpr util::StatKey kStatUnitEqualitySubstitutionUnits =
    util::StatKey::UnitEqualitySubstitutionUnits;
constexpr util::StatKey kStatUnitEqualitySubstitutions = util::StatKey::UnitEqualitySubstitutions;
constexpr util::StatKey kStatUnitEqualitySubstitutionAccepted =
    util::StatKey::UnitEqualitySubstitutionAccepted;
constexpr util::StatKey kStatUnitEqualitySubstitutionRejected =
    util::StatKey::UnitEqualitySubstitutionRejected;
constexpr util::StatKey kStatEqualityQueryNodes = util::StatKey::EqualityQueryNodes;
constexpr util::StatKey kStatEqualityQueryMemoHits = util::StatKey::EqualityQueryMemoHits;
constexpr util::StatKey kStatEqualityQueryAddTermRequests =
    util::StatKey::EqualityQueryAddTermRequests;
constexpr util::StatKey kStatEqualityQueryAddTermSeenHits =
    util::StatKey::EqualityQueryAddTermSeenHits;
constexpr util::StatKey kStatSolvedConstantCandidates = util::StatKey::SolvedConstantCandidates;
constexpr util::StatKey kStatSolvedConstantAccepted = util::StatKey::SolvedConstantAccepted;
constexpr util::StatKey kStatSolvedConstantRewrites = util::StatKey::SolvedConstantRewrites;
constexpr util::StatKey kStatSolvedConstantRejectedCycles =
    util::StatKey::SolvedConstantRejectedCycles;
constexpr util::StatKey kStatSolvedConstantRejectedBlowups =
    util::StatKey::SolvedConstantRejectedBlowups;
constexpr util::StatKey kStatRealEqualityLowered = util::StatKey::RealEqualityLowered;
constexpr util::StatKey kStatSolvedConstantGaussianCandidates =
    util::StatKey::SolvedConstantGaussianCandidates;
constexpr util::StatKey kStatFiniteDomainDomains = util::StatKey::FiniteDomainDomains;
constexpr util::StatKey kStatFiniteDomainAmoClauses = util::StatKey::FiniteDomainAmoClauses;
constexpr util::StatKey kStatFiniteDomainRawChoices = util::StatKey::FiniteDomainRawChoices;
constexpr util::StatKey kStatFiniteDomainRawAmoClauses = util::StatKey::FiniteDomainRawAmoClauses;
constexpr util::StatKey kStatFiniteDomainPostAmoClauses = util::StatKey::FiniteDomainPostAmoClauses;
constexpr util::StatKey kStatFiniteDomainDedupedAmoClauses =
    util::StatKey::FiniteDomainDedupedAmoClauses;
constexpr util::StatKey kStatFiniteDomainEvaluatedEqualities =
    util::StatKey::FiniteDomainEvaluatedEqualities;
constexpr util::StatKey kStatFiniteDomainEvaluationCutoffs =
    util::StatKey::FiniteDomainEvaluationCutoffs;
constexpr util::StatKey kStatEqualityDefinitions = util::StatKey::EqualityDefinitions;
constexpr util::StatKey kStatEqualityDefinitionCandidatePairs =
    util::StatKey::EqualityDefinitionCandidatePairs;
constexpr util::StatKey kStatEqualityDefinitionAcceptedPairs =
    util::StatKey::EqualityDefinitionAcceptedPairs;
constexpr util::StatKey kStatEqualityDefinitionPredictedClauses =
    util::StatKey::EqualityDefinitionPredictedClauses;
constexpr util::StatKey kStatEqualityDefinitionRejectedDomainSize =
    util::StatKey::EqualityDefinitionRejectedDomainSize;
constexpr util::StatKey kStatEqualityDefinitionRejectedClauseLimit =
    util::StatKey::EqualityDefinitionRejectedClauseLimit;
constexpr util::StatKey kStatGroundCongruenceClauses = util::StatKey::GroundCongruenceClauses;
constexpr util::StatKey kStatGroundCongruenceSkippedGroups =
    util::StatKey::GroundCongruenceSkippedGroups;
constexpr util::StatKey kStatUfSymmetryAcceptedSets = util::StatKey::UfSymmetryAcceptedSets;
constexpr util::StatKey kStatUfSymmetryValues = util::StatKey::UfSymmetryValues;
constexpr util::StatKey kStatUfSymmetryTerms = util::StatKey::UfSymmetryTerms;
constexpr util::StatKey kStatUfSymmetryConstraints = util::StatKey::UfSymmetryConstraints;
constexpr util::StatKey kStatUfSymmetryValuePrecedenceTerms =
    util::StatKey::UfSymmetryValuePrecedenceTerms;
constexpr util::StatKey kStatUfSymmetryValuePrecedenceConstraints =
    util::StatKey::UfSymmetryValuePrecedenceConstraints;
constexpr util::StatKey kStatUfSymmetryRejectedNoninvariant =
    util::StatKey::UfSymmetryRejectedNoninvariant;
constexpr util::StatKey kStatAutoSingleAssertion = util::StatKey::AutoSingleAssertion;
constexpr util::StatKey kStatAutoStrong = util::StatKey::AutoStrong;
constexpr util::StatKey kStatAutoBasicSingle = util::StatKey::AutoBasicSingle;
constexpr util::StatKey kStatAutoBasicMulti = util::StatKey::AutoBasicMulti;
constexpr util::StatKey kStatAutoLargeAssertionSkip = util::StatKey::AutoLargeAssertionSkip;
constexpr util::StatKey kStatSatTheoryNegativePhase = util::StatKey::SatTheoryNegativePhase;
constexpr util::StatKey kStatSatPhaseAutoPolarity = util::StatKey::SatPhaseAutoPolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicSinglePredicatePolarity =
    util::StatKey::SatPhaseAutoBasicSinglePredicatePolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicMultiPolarity =
    util::StatKey::SatPhaseAutoBasicMultiPolarity;
constexpr util::StatKey kStatSatVars = util::StatKey::SatVars;
constexpr util::StatKey kStatSatClauses = util::StatKey::SatClauses;
constexpr util::StatKey kStatSatClauseLits = util::StatKey::SatClauseLits;
constexpr util::StatKey kStatSatUnitClauses = util::StatKey::SatUnitClauses;
constexpr util::StatKey kStatSatBinaryClauses = util::StatKey::SatBinaryClauses;
constexpr util::StatKey kStatSolverBoolean = util::StatKey::SolverBoolean;
constexpr util::StatKey kStatSolverQfUf = util::StatKey::SolverQfUf;
constexpr util::StatKey kStatSolverQfLra = util::StatKey::SolverQfLra;
constexpr util::StatKey kStatSatSolveCalls = util::StatKey::SatSolveCalls;
constexpr util::StatKey kStatTheoryAtoms = util::StatKey::TheoryAtoms;
constexpr util::StatKey kStatEufPredicateAtoms = util::StatKey::EufPredicateAtoms;
constexpr util::StatKey kStatPureTheoryFilterEnabled = util::StatKey::PureTheoryFilterEnabled;
constexpr util::StatKey kStatPureTheoryAtomsPositive = util::StatKey::PureTheoryAtomsPositive;
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
constexpr util::StatKey kStatEufPropagationAutoOff = util::StatKey::EufPropagationAutoOff;
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
constexpr std::string_view kStatTimeTotalUs = ":time.total-us";
constexpr std::string_view kStatTimeParseUs = ":time.parse-us";
constexpr std::string_view kStatTimeFrontendUs = ":time.frontend-us";
constexpr std::string_view kStatTimePreprocessingUs = ":time.preprocessing-us";
constexpr std::string_view kStatTimeSolveUs = ":time.solve-us";
using PhaseClock = std::chrono::steady_clock;

util::Statistics::Value elapsed_microseconds(PhaseClock::time_point start,
                                             PhaseClock::time_point end) {
  return static_cast<util::Statistics::Value>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

}  // namespace

namespace {

CheckSatOutcome run_query(std::string_view input, CheckSatOptions options,
                          Frontend* supplied_frontend, std::vector<core::TermId> assertions,
                          CnfConflictCollector* collector = nullptr) {
  std::unique_ptr<Frontend> owned_frontend;
  const PhaseClock::time_point total_start = PhaseClock::now();
  util::Statistics statistics;
  statistics = util::Statistics{};
  statistics.set(kStatTimeTotalUs, 0);
  statistics.set(kStatTimeParseUs, 0);
  statistics.set(kStatTimeFrontendUs, 0);
  statistics.set(kStatTimePreprocessingUs, 0);
  statistics.set(kStatTimeSolveUs, 0);
  statistics.set(kStatAssertionsInitial, 0);
  statistics.set(kStatAssertionsFinal, 0);
  statistics.set(kStatPreprocessingPasses, 0);
  statistics.set(kStatPreprocessingChangedPasses, 0);
  statistics.set(kStatPreprocessingAdaptivePasses, 0);
  statistics.set(kStatPreprocessingTermSizeInitial, 0);
  statistics.set(kStatPreprocessingTermSizeFinal, 0);
  statistics.set(":preprocessing.boolean-flatten.attempts", 0);
  statistics.set(":preprocessing.boolean-flatten.accepted", 0);
  statistics.set(":preprocessing.boolean-flatten.threshold-stops", 0);
  statistics.set(":preprocessing.boolean-flatten.operands-copied", 0);
  statistics.set(":preprocessing.boolean-flatten.rewrites", 0);
  statistics.set(":preprocessing.boolean-flatten.max-arity", 0);
  statistics.set(":preprocessing.boolean-implies.visited", 0);
  statistics.set(":preprocessing.boolean-implies.clause-candidates", 0);
  statistics.set(":preprocessing.boolean-implies.rewritten", 0);
  statistics.set(":preprocessing.boolean-implies.all-mode-rewrites", 0);
  statistics.set(":preprocessing.boolean-implies.widened-operands", 0);
  statistics.set(":preprocessing.boolean-implies.threshold-stops", 0);
  statistics.set(":preprocessing.boolean-implies.terms-created", 0);
  statistics.set(kStatEqBridgeEqualities, 0);
  statistics.set(kStatEqBridgeSkippedOrNodes, 0);
  statistics.set(kStatBooleanUnitFacts, 0);
  statistics.set(kStatBooleanUnitRewrites, 0);
  statistics.set(kStatForcedAtomFacts, 0);
  statistics.set(kStatForcedAtomDisequalities, 0);
  statistics.set(kStatForcedAtomRewrites, 0);
  statistics.set(kStatUnitEqualityConsequences, 0);
  statistics.set(kStatUnitEqualitySubstitutionUnits, 0);
  statistics.set(kStatUnitEqualitySubstitutions, 0);
  statistics.set(kStatUnitEqualitySubstitutionAccepted, 0);
  statistics.set(kStatUnitEqualitySubstitutionRejected, 0);
  statistics.set(kStatEqualityQueryNodes, 0);
  statistics.set(kStatEqualityQueryMemoHits, 0);
  statistics.set(kStatEqualityQueryAddTermRequests, 0);
  statistics.set(kStatEqualityQueryAddTermSeenHits, 0);
  statistics.set(kStatSolvedConstantCandidates, 0);
  statistics.set(kStatSolvedConstantAccepted, 0);
  statistics.set(kStatSolvedConstantRewrites, 0);
  statistics.set(kStatSolvedConstantRejectedCycles, 0);
  statistics.set(kStatSolvedConstantRejectedBlowups, 0);
  statistics.set(kStatRealEqualityLowered, 0);
  statistics.set(kStatSolvedConstantGaussianCandidates, 0);
  statistics.set(kStatFiniteDomainDomains, 0);
  statistics.set(kStatFiniteDomainAmoClauses, 0);
  statistics.set(kStatFiniteDomainRawChoices, 0);
  statistics.set(kStatFiniteDomainRawAmoClauses, 0);
  statistics.set(kStatFiniteDomainPostAmoClauses, 0);
  statistics.set(kStatFiniteDomainDedupedAmoClauses, 0);
  statistics.set(kStatFiniteDomainEvaluatedEqualities, 0);
  statistics.set(kStatFiniteDomainEvaluationCutoffs, 0);
  statistics.set(kStatEqualityDefinitions, 0);
  statistics.set(kStatEqualityDefinitionCandidatePairs, 0);
  statistics.set(kStatEqualityDefinitionAcceptedPairs, 0);
  statistics.set(kStatEqualityDefinitionPredictedClauses, 0);
  statistics.set(kStatEqualityDefinitionRejectedDomainSize, 0);
  statistics.set(kStatEqualityDefinitionRejectedClauseLimit, 0);
  statistics.set(kStatGroundCongruenceClauses, 0);
  statistics.set(kStatGroundCongruenceSkippedGroups, 0);
  statistics.set(kStatUfSymmetryAcceptedSets, 0);
  statistics.set(kStatUfSymmetryValues, 0);
  statistics.set(kStatUfSymmetryTerms, 0);
  statistics.set(kStatUfSymmetryConstraints, 0);
  statistics.set(kStatUfSymmetryValuePrecedenceTerms, 0);
  statistics.set(kStatUfSymmetryValuePrecedenceConstraints, 0);
  statistics.set(kStatUfSymmetryRejectedNoninvariant, 0);
  statistics.set(kStatAutoSingleAssertion, 0);
  statistics.set(kStatAutoStrong, 0);
  statistics.set(kStatAutoBasicSingle, 0);
  statistics.set(kStatAutoBasicMulti, 0);
  statistics.set(kStatAutoLargeAssertionSkip, 0);
  statistics.set(kStatSatTheoryNegativePhase, 0);
  statistics.set(kStatSatPhaseAutoPolarity, 0);
  statistics.set(kStatSatPhaseAutoBasicSinglePredicatePolarity, 0);
  statistics.set(kStatSatPhaseAutoBasicMultiPolarity, 0);
  statistics.set(kStatSatVars, 0);
  statistics.set(kStatSatClauses, 0);
  statistics.set(kStatSatClauseLits, 0);
  statistics.set(kStatSatUnitClauses, 0);
  statistics.set(kStatSatBinaryClauses, 0);
  statistics.set(kStatSolverBoolean, 0);
  statistics.set(kStatSolverQfUf, 0);
  statistics.set(kStatSolverQfLra, 0);
  statistics.set(kStatSatSolveCalls, 0);
  statistics.set(kStatTheoryAtoms, 0);
  statistics.set(kStatEufPredicateAtoms, 0);
  statistics.set(kStatPureTheoryFilterEnabled, 0);
  statistics.set(kStatPureTheoryAtomsPositive, 0);
  statistics.set(kStatPureTheoryAtomsNegative, 0);
  statistics.set(kStatPureTheoryLiteralsFiltered, 0);
  statistics.set(kStatEufAssignments, 0);
  statistics.set(kStatEufEqAssignments, 0);
  statistics.set(kStatEufDiseqAssignments, 0);
  statistics.set(kStatTheoryChecks, 0);
  statistics.set(kStatTheoryConflicts, 0);
  statistics.set(util::StatKey::TheoryConflictCount, 0);
  statistics.set(kStatEufPropagationRounds, 0);
  statistics.set(kStatEufPropagationCandidates, 0);
  statistics.set(kStatEufPropagationDelivered, 0);
  statistics.set(kStatEufPropagationReasonLits, 0);
  statistics.set(kStatEufPropagationBudget, 0);
  statistics.set(kStatEufPropagationDeliveryBudget, 0);
  statistics.set(kStatEufPropagationBudgetCutoffs, 0);
  statistics.set(kStatEufPropagationHitRateCutoffs, 0);
  statistics.set(kStatEufPropagationAutoOnline, 0);
  statistics.set(kStatEufPropagationAutoConflict, 0);
  statistics.set(kStatEufPropagationAutoOff, 0);
  statistics.set(kStatEufDiseqPropAssignedAppDisequalities, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryCandidates, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryExistingArgAtom, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryArgAtomTrue, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryArgAtomFalse, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionCandidates, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionExistingArgAtom, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionArgAtomTrue, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionArgAtomFalse, 0);
  statistics.set(kStatEufDiseqPropGuardedFunctionGuardCandidates, 0);
  statistics.set(kStatEufDiseqPropGuardedArgumentGuardCandidates, 0);
  statistics.set(kStatEufAckermannApplications, 0);
  statistics.set(kStatEufAckermannFunctionSymbols, 0);
  statistics.set(kStatEufAckermannEligibleSymbols, 0);
  statistics.set(kStatEufAckermannUnarySymbols, 0);
  statistics.set(kStatEufAckermannNarySymbols, 0);
  statistics.set(kStatEufAckermannEstimatedConstraints, 0);
  statistics.set(kStatEufAckermannEstimatedEqualities, 0);
  statistics.set(kStatEufAckermannEstimatedClauseLits, 0);
  statistics.set(kStatEufAckermannMaxSymbolApplications, 0);
  statistics.set(kStatEufAckermannMaxSymbolArity, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe100, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe1000, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe10000, 0);
  statistics.set(kStatEufDynamicAckShadowInvocations, 0);
  statistics.set(kStatEufDynamicAckShadowResultPairs, 0);
  statistics.set(kStatEufDynamicAckShadowArgAtomsMissing, 0);
  statistics.set(kStatEufDynamicAckShadowSatVisibleClauses, 0);
  statistics.set(kStatEufDynamicAckShadowSatisfied, 0);
  statistics.set(kStatEufDynamicAckShadowUnit, 0);
  statistics.set(kStatEufDynamicAckShadowConflicting, 0);
  statistics.set(kStatEufDynamicAckShadowResultDiseqAssigned, 0);
  statistics.set(kStatEufDynamicAckShadowAllArgsEqAssigned, 0);
  statistics.set(kStatEufDynamicAckShadowWidthTotal, 0);
  statistics.set(kStatEufDynamicAckShadowMaxWidth, 0);
  statistics.set(kStatEufDynamicAckShadowWidthLe3, 0);
  statistics.set(kStatEufDynamicAckShadowWidthGt3, 0);
  statistics.set(kStatEufDiseqAckCandidates, 0);
  statistics.set(kStatEufDiseqAckQueuePushes, 0);
  statistics.set(kStatEufDiseqAckQueuePops, 0);
  statistics.set(kStatEufDiseqAckWidthSkips, 0);
  statistics.set(kStatEufDiseqAckMissingArgAtom, 0);
  statistics.set(kStatEufDiseqAckSatisfiedSkips, 0);
  statistics.set(kStatEufDiseqAckUnit, 0);
  statistics.set(kStatEufDiseqAckConflict, 0);
  statistics.set(kStatEufDiseqAckPropagated, 0);
  statistics.set(kStatEufDiseqAckReturned, 0);
  statistics.set(kStatEufDiseqAckPendingCleared, 0);
  statistics.set(kStatEufDiseqAckReasonLits, 0);
  statistics.set(kStatEufDiseqAckQueueBudgetCutoffs, 0);
  statistics.set(kStatEufDiseqAckPerSymbolCutoffs, 0);
  statistics.set(kStatEufIncrementalTerms, 0);
  statistics.set(kStatEufIncrementalFlatDefinitions, 0);
  statistics.set(kStatEufIncrementalMerges, 0);
  statistics.set(kStatEufIncrementalPendingPops, 0);
  statistics.set(kStatEufIncrementalLookupProbes, 0);
  statistics.set(kStatEufIncrementalLookupHits, 0);
  statistics.set(kStatEufIncrementalCongruenceMerges, 0);
  statistics.set(kStatEufIncrementalDisequalityChecks, 0);
  statistics.set(kStatEufIncrementalConflicts, 0);
  statistics.set(kStatEufIncrementalExplainCalls, 0);
  statistics.set(kStatEufIncrementalExplainCacheHits, 0);
  statistics.set(kStatEufIncrementalExplainCacheBoundMisses, 0);
  statistics.set(kStatEufIncrementalExplainLength, 0);
  statistics.set(kStatEufIncrementalExplainTopLevelQueries, 0);
  statistics.set(kStatEufIncrementalExplainRecursiveQueries, 0);
  statistics.set(kStatEufIncrementalExplainRepeatedPairQueries, 0);
  statistics.set(kStatEufIncrementalExplainMaxRecursionDepth, 0);
  statistics.set(kStatEufIncrementalExplainBfsSearches, 0);
  statistics.set(kStatEufIncrementalExplainBfsVisited, 0);
  statistics.set(kStatEufIncrementalExplainBfsPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainBfsMaxQueue, 0);
  statistics.set(kStatEufIncrementalExplainProofForestQueries, 0);
  statistics.set(kStatEufIncrementalExplainProofForestPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainProofForestMaxPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainProofForestMissingLca, 0);
  statistics.set(kStatEufIncrementalExplainProofForestShorterThanBfs, 0);
  statistics.set(kStatEufIncrementalExplainProofForestEqualToBfs, 0);
  statistics.set(kStatEufIncrementalExplainProofForestLongerThanBfs, 0);
  statistics.set(kStatEufIncrementalExplainCausalEdgeSkips, 0);
  statistics.set(kStatEufIncrementalExplainDirectEdges, 0);
  statistics.set(kStatEufIncrementalExplainCongruenceEdges, 0);
  statistics.set(kStatEufIncrementalExplainReasonDuplicateLiterals, 0);
  statistics.set(kStatEufIncrementalConflictReasonMaxLength, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe4, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe8, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe16, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe32, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe64, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthGt64, 0);
  statistics.set(kStatEufIncrementalBacktracks, 0);
  statistics.set(kStatEufIncrementalTrailEntries, 0);
  statistics.set(kStatEufIncrementalClassMemberTrailEntriesSaved, 0);
  statistics.set(kStatEufIncrementalUseListAppends, 0);
  statistics.set(kStatEufIncrementalUseListDuplicateSkips, 0);
  statistics.set(kStatEufIncrementalAffectedUseListDedup, 0);
  statistics.set(kStatEufIncrementalMaxUseListSize, 0);
  statistics.set(kStatEufIncrementalChangedTermDrains, 0);
  statistics.set(kStatEufIncrementalChangedTermsDelivered, 0);
  statistics.set(kStatEufIncrementalChangedTermGenerationRebases, 0);
  statistics.set(kStatEufIncrementalMaxCheckpointDepth, 0);

  const auto finish = [&](CheckSatResult result, std::string message = "",
                          InconclusiveReason reason = InconclusiveReason::None) {
    statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
    if (reason == InconclusiveReason::None) {
      if (result == CheckSatResult::Unknown) {
        reason = InconclusiveReason::Unknown;
      } else if (result == CheckSatResult::Unsupported) {
        reason = InconclusiveReason::Unsupported;
      }
    }
    return CheckSatOutcome{result, std::move(message), statistics, "", reason};
  };

  if (options.cancelled && options.cancelled()) {
    return finish(CheckSatResult::Unknown, "cancelled", InconclusiveReason::Cancelled);
  }

  try {
    if (supplied_frontend == nullptr) {
      const PhaseClock::time_point parse_start = PhaseClock::now();
      smtlib::Parser parser(input);
      const std::vector<smtlib::Command> commands = parser.parse_commands();
      statistics.set(kStatTimeParseUs, elapsed_microseconds(parse_start, PhaseClock::now()));
      const PhaseClock::time_point frontend_start = PhaseClock::now();
      owned_frontend = std::make_unique<Frontend>();
      bool checked = false;
      for (const smtlib::Command& command : commands) {
        if (command.kind == smtlib::CommandKind::Exit) break;
        if (checked && command.kind != smtlib::CommandKind::GetModel &&
            command.kind != smtlib::CommandKind::Ignored) {
          throw UnsupportedInput("single-query API requires SmtSession for subsequent commands");
        }
        if (command.kind == smtlib::CommandKind::CheckSat) checked = true;
        owned_frontend->process_command(command, assertions);
      }
      statistics.set(kStatTimeFrontendUs, elapsed_microseconds(frontend_start, PhaseClock::now()));
      if (!owned_frontend->saw_check_sat()) {
        return finish(CheckSatResult::Unsupported, "missing check-sat command");
      }
    }
    Frontend& frontend = supplied_frontend != nullptr ? *supplied_frontend : *owned_frontend;
    statistics.set(kStatAssertionsInitial, assertions.size());
    statistics.set(":frontend.define-fun.nullary-direct-uses",
                   frontend.nullary_definition_direct_uses());
    statistics.set(":frontend.define-fun.nullary-eager-compiles",
                   frontend.nullary_definition_eager_compiles());
    statistics.set(":frontend.define-fun.nullary-side-assertions",
                   frontend.nullary_definition_side_assertions());
    // Retain the old names for benchmark compatibility. A cache hit is now a
    // direct use, while a miss/entry corresponds to eager declaration-time compilation.
    statistics.set(":frontend.define-fun.nullary-cache-hits",
                   frontend.nullary_definition_direct_uses());
    statistics.set(":frontend.define-fun.nullary-cache-misses",
                   frontend.nullary_definition_eager_compiles());
    statistics.set(":frontend.define-fun.nullary-cache-entries",
                   frontend.nullary_definition_eager_compiles());
    if (frontend.requested_model()) {
      if (!frontend.produce_models()) {
        return finish(CheckSatResult::Unsupported, "get-model requires :produce-models true");
      }
      options.produce_model = true;
    }
    // Quantified formulas must never reach preprocessing or a ground leaf,
    // whose passes treat unknown kinds as opaque. They take their own route in
    // logic UF and are unsupported elsewhere; see docs/quantifiers.md.
    if (frontend.terms().quantifier_count() > 0 &&
        std::any_of(assertions.begin(), assertions.end(), [&frontend](core::TermId assertion) {
          return frontend.terms().contains_quantifier(assertion);
        })) {
      if (frontend.logic() != std::optional<std::string>("UF")) {
        return finish(CheckSatResult::Unsupported,
                      "quantified formulas are supported only in logic UF");
      }
      // Ground preprocessing does not support binders. The diagnostic must
      // stop here rather than entering the quantifier solver.
      if (options.preprocess_only) {
        return finish(CheckSatResult::Unknown,
                      "preprocess-only: quantified input skips ground preprocessing");
      }
      const PhaseClock::time_point solve_start = PhaseClock::now();
      CheckSatOutcome outcome =
          solve_quantified_uf(frontend.terms(), assertions, options, statistics,
                              [&frontend](core::SortId sort, std::string_view prefix) {
                                return frontend.fresh_internal_constant(sort, prefix);
                              });
      statistics.set(kStatTimeSolveUs, elapsed_microseconds(solve_start, PhaseClock::now()));
      statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
      outcome.statistics = statistics;
      return outcome;
    }
    const std::vector<core::TermId> original_assertions =
        options.produce_model ? assertions : std::vector<core::TermId>{};
    const auto finalize_model = [&](CheckSatOutcome& outcome) {
      if (outcome.result != CheckSatResult::Sat || !options.produce_model) return;
      if (!outcome.typed_model) throw core::ModelError("SAT result has no typed interpretation");
      bool check = options.check_model;
#ifndef NDEBUG
      check = true;
#endif
      if (check) {
        const auto start = PhaseClock::now();
        core::ModelEvaluator evaluator(frontend.terms(), *outcome.typed_model);
        for (const core::TermId assertion : original_assertions) {
          if (evaluator.evaluate(assertion) !=
              core::ModelValue{frontend.terms().symbols().bool_sort(), true}) {
            throw core::ModelError("original assertion is false at term " +
                                   std::to_string(assertion));
          }
        }
        statistics.set(":time.model.check-us", elapsed_microseconds(start, PhaseClock::now()));
      }
      const auto start = PhaseClock::now();
      outcome.model = smtlib::print_model(*outcome.typed_model);
      statistics.set(":time.model.serialization-us",
                     elapsed_microseconds(start, PhaseClock::now()));
    };
    const PhaseClock::time_point preprocessing_start = PhaseClock::now();
    // The session is what owns symbol creation, so it is the session that hands
    // that capability to the strategy it executes.
    const TacticContext::FreshConstantFactory fresh_constant =
        [&frontend](core::SortId sort, std::string_view prefix) {
          return frontend.fresh_internal_constant(sort, prefix);
        };
    TacticContext preprocessing_context(frontend.terms(), statistics, options.cancelled,
                                        options.produce_model, fresh_constant);
    PreparedPreprocessingStrategy prepared = prepare_and_execute_preprocessing_strategy(
        frontend, std::move(assertions), options, preprocessing_context);
    // Preprocessing returns a disjunction. Reading only its first alternative
    // would report unsat for a formula satisfiable through a later one, so
    // every alternative is carried through to the solver.
    PreprocessedAlternatives triaged =
        triage_preprocessed_alternatives(frontend.terms(), prepared.result);
    if (triaged.any_inconclusive) {
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      CheckSatOutcome interrupted =
          outcome_from_tactic_result(prepared.result, preprocessing_context);
      return finish(interrupted.result, std::move(interrupted.message), interrupted.reason);
    }
    if (triaged.surviving.empty()) {
      // Every alternative reduced to false.
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      if (collector != nullptr) {
        collector->record_preprocessing_unsat();
        statistics.set(":export.preprocessing-unsat", 1);
      }
      return finish(CheckSatResult::Unsat);
    }
    if (collector != nullptr && triaged.surviving.size() != 1) {
      throw UnsupportedInput("--export-cnf requires a single prepared solver goal");
    }
    // Statistics, diagnostics, and the Boolean-shape heuristics describe the
    // whole preprocessed goal, so they read the union of the alternatives.
    // With the single alternative every production strategy produces today
    // this is exactly the previous assertion vector.
    assertions.clear();
    for (const Goal& alternative : triaged.surviving) {
      assertions.insert(assertions.end(), alternative.assertions().begin(),
                        alternative.assertions().end());
    }
    const FiniteDomainAtMostOneResult& raw_finite_domain_result =
        *prepared.profile.raw_finite_domain;
    statistics.set(kStatAssertionsFinal, assertions.size());
    statistics.set(kStatPreprocessingTermSizeFinal,
                   reachable_term_count(frontend.terms(), assertions));
    if (options.nelson_oppen_diagnostics) {
      TheoryCombinationManager no_manager(frontend.terms());
      no_manager.rebuild(assertions);
      const NelsonOppenInterfaceSummary& no_interface = no_manager.summary();
      statistics.set(":no.interface.terms", no_interface.terms.size());
      statistics.set(":no.interface.sorts", no_interface.sort_count);
      statistics.set(":no.interface.candidate-pairs", no_interface.candidate_pairs);
      statistics.set(":no.interface.registry-entries", no_interface.equality_registry.size());
      statistics.set(":no.interface.visible-equalities", no_interface.visible_equalities.size());
      statistics.set(":no.interface.missing-visible-equalities", no_interface.missing_equalities);
      statistics.set(":no.purification.sites", no_interface.purification_sites.size());
      statistics.set(":no.purification.euf-real-terms", no_interface.euf_real_terms);
      statistics.set(":no.purification.arithmetic-uf-apps",
                     no_interface.arithmetic_uninterpreted_applications);
    }
    if (options.euf_ackermann_diagnostics) {
      record_ackermann_cost_diagnostics_for_session(frontend.terms(), assertions, statistics);
    }
    // A single alternative with no assertions is already true, so the
    // disjunction is satisfiable regardless of the others. Trivially false
    // alternatives were dropped by the triage above.
    const auto trivially_true = std::ranges::find_if(
        triaged.surviving,
        [](const Goal& alternative) { return alternative.assertions().empty(); });
    if (trivially_true != triaged.surviving.end() && !options.preprocess_only) {
      const ModelReconstruction reconstruction = trivially_true->reconstruction();
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      CheckSatOutcome outcome = finish(CheckSatResult::Sat);
      if (options.produce_model) {
        outcome.typed_model = build_typed_model(frontend.terms(), {}, {}, {},
                                                reconstruction.conversions, nullptr, &statistics);
      }
      finalize_model(outcome);
      statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
      outcome.statistics = statistics;
      return outcome;
    }
    if (options.preprocess_only) {
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      return finish(CheckSatResult::Unknown, "preprocess-only");
    }

    CheckSatOptions solve_options = options;
    const BooleanShape final_boolean_shape = boolean_shape(frontend.terms(), assertions);
    statistics.set(":preprocessing.boolean-shape.or-nodes", final_boolean_shape.or_nodes);
    statistics.set(":preprocessing.boolean-shape.not-nodes", final_boolean_shape.not_nodes);
    statistics.set(":preprocessing.boolean-shape.top-level-or-nodes",
                   final_boolean_shape.top_level_or_nodes);
    statistics.set(":preprocessing.boolean-shape.max-top-level-or-children",
                   final_boolean_shape.max_top_level_or_children);
    if (should_use_auto_top_level_or_tseitin(options, raw_finite_domain_result,
                                             final_boolean_shape)) {
      solve_options.top_level_or_tseitin = true;
      statistics.increment(":sat.top-level-or-tseitin.auto");
    }
    statistics.set(kStatTimePreprocessingUs,
                   elapsed_microseconds(preprocessing_start, PhaseClock::now()));

    const auto finish_solve = [&](CheckSatOutcome outcome, PhaseClock::time_point solve_start) {
      finalize_model(outcome);
      statistics.set(kStatTimeSolveUs, elapsed_microseconds(solve_start, PhaseClock::now()));
      statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
      outcome.statistics = statistics;
      return outcome;
    };
    TacticContext tactic_context(frontend.terms(), statistics, options.cancelled,
                                 options.produce_model, fresh_constant);
    const Strategy solver_strategy = make_session_solver_strategy(
        frontend, solve_options, statistics, tactic_context, collector);
    const Tactic solver_dispatch = compile(solver_strategy);

    const PhaseClock::time_point solve_start = PhaseClock::now();
    const AnyOf solved = solver_dispatch(tactic_context, AnyOf(std::move(triaged.surviving)));
    CheckSatOutcome outcome = outcome_from_tactic_result(solved, tactic_context);
    return finish_solve(std::move(outcome), solve_start);
  } catch (const UnsupportedInput& error) {
    return finish(CheckSatResult::Unsupported, error.what());
  } catch (const smtlib::ParseError& error) {
    return finish(CheckSatResult::Unsupported, error.what());
  }
}

}  // namespace

CheckSatOutcome check_sat_smtlib_once(std::string_view input, CheckSatOptions options) {
  return run_query(input, std::move(options), nullptr, {});
}

CheckSatOutcome check_sat_frontend_once(std::unique_ptr<Frontend> frontend,
                                        std::vector<core::TermId> assertions,
                                        CheckSatOptions options) {
  if (!frontend) throw std::invalid_argument("query frontend must not be null");
  return run_query({}, std::move(options), frontend.get(), std::move(assertions));
}

CheckSatOutcome check_sat_frontend_export_once(Frontend& frontend,
                                               std::vector<core::TermId> assertions,
                                               CheckSatOptions options,
                                               CnfConflictCollector& collector) {
  return run_query({}, std::move(options), &frontend, std::move(assertions), &collector);
}

}  // namespace primo::smt
