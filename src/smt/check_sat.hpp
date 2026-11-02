#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/model.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/preprocessing.hpp"
#include "theory/quantifiers/tuple_orders.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

enum class IncrementalMode { Rebuild, Assumptions };

enum class PreprocessingMode {
  Off,
  Basic,
  Strong,
  Auto,
};

enum class AutoPreprocessSelectorMode {
  Legacy,
  Structural,
};

struct AutoPreprocessTuning {
  // Zero means unlimited for upper bounds. Minimum counts are always positive.
  std::size_t large_conjunct_limit = 10000;
  std::size_t compact_term_limit = 20000;
  std::size_t compact_min_conjuncts = 2;
  std::size_t finite_domain_term_limit = 15000;
  std::size_t finite_domain_min_domains = 200;
  std::size_t finite_domain_max_symmetry_terms = 6;
};

enum class EufPropagationMode {
  Auto,
  Off,
  Conflict,
  Model,
  Online,
};

enum class EufEngine {
  Lazy,
  Incremental,
};

enum class PureTheoryFilterMode {
  Auto,
  On,
  Off,
};

enum class TheoryPhaseMode {
  Negative,
  Positive,
  None,
  Polarity,
  Auto,
};

enum class EufExplainSearchMode {
  OneSidedBfs,
  BidirectionalBfs,
  ProofForest,
  ReorientedProofForest,
  StructuralProofForest,
};

enum class QfUfPortfolioMode {
  Off,
  BasicPolarity,
};

enum class ModelEqualityBranchPolicy {
  First,
  Ranked,
  Adaptive,
  CareQueued,
};

enum class ModelEqualityMutationScope {
  All,
  Care,
};

// Which formulas reach the mixed EUF/LRA solver rather than the pure QF_LRA
// one. This is a *dispatch* policy, not a theory setting.
//
// `Narrow` (default): only a Bool-sorted Application counts as an EUF
// predicate, so a formula that is pure QF_LRA apart from propositional
// variables stays on the pure path.
//
// `Wide`: a Bool-sorted Constant counts too -- the behaviour before
// 2026-08-07. That sends any QF_LRA formula containing a single propositional
// variable to the mixed solver, which is usually much worse (it hands those
// variables to congruence closure) but is genuinely better on some instances.
// See docs/nelson-oppen-qf-lra-regression.md and open-leads lead 2b: neither
// path dominates.
//
// Kept reachable for two reasons beyond curiosity: it is a portfolio dimension,
// and it lets differential fuzzing exercise the mixed path on inputs that would
// otherwise never reach it.
enum class MixedDispatchMode {
  Narrow,
  Wide,
};

// Mirrors theory::arithmetic::LraTableau::PivotingRule -- kept as a
// separate check_sat.hpp-local enum (translated at the wiring point, see
// lra_pivoting_rule_from_options in check_sat.cpp) rather than reused
// directly, the same way EufExplainSearchMode is distinct from
// theory::equality::FlatExplanationSearchMode -- keeps this header free
// of theory-layer includes.
enum class LraPivotingRule {
  Bland,
  Dantzig,
  Soi,
  Sparse,
};

// Defensive caps on row-based/interval bound propagation's per-bound-change
// cost. Unlike same-slack propagation (O(atoms sharing a slack), typically
// small), a row-based scan is triggered by *any* bound change on a variable
// that may occur in many rows, and the reverse direction runs an O(row size)
// child-bound computation for *each* other term of each affected row --
// multiplicatively pricier, which is why its two caps default smaller.
//
// The defaults are not derived from theory; they were picked to be generous
// enough to matter on real formulas and small enough to bound worst-case cost
// on adversarial/fuzzed input (the Gaussian-elimination caps' own precedent).
// They are options rather than constants because measurement says the
// defaults are the binding constraint, not the mechanism: on the 500KB-5MB
// gap band the scan fires 11-68 times against a 7k-190k opportunity count,
// and raising the caps to 256 makes scrambled312090 -- a file that times out
// at 60s by default -- solve in about 58s. See docs/qf-lra-heuristics.md
// section 6.5 (direction D1); the sweep that settles the defaults needs to
// vary them per run, not per build.
// Mirrors theory::arithmetic::LraTableau::Tuning, translated at the same
// wiring point as LraPivotingRule and for the same reason -- this header
// stays free of theory-layer includes. That header documents the defaults
// and the argument that every value in range is sound (these knobs change
// how long an answer takes, never which answer it is).
//
// Hardcoded constants until 2026-08-06. Exposed so the parameter-tuning
// campaign can vary them per run rather than per build; the defaults are
// exactly the constants they replaced, so an untuned run is unchanged.
// See docs/tuning-parameters.md.
// Mirrors theory::arithmetic::LraTableau::SoiMinimization /
// ::SoiMinimizationOrder, translated at the wiring point the same way
// LraPivotingRule is -- keeps this header free of theory-layer includes.
enum class LraSoiMinimization { QuickXplain, Deletion, None };
enum class LraSoiMinimizationOrder { Id, RowSupport, Violation };

struct LraTuning {
  std::size_t sparse_pricing_candidates = 16;
  std::size_t bland_fallback_factor = 4;
  std::size_t bland_fallback_offset = 64;
  LraSoiMinimization soi_minimization = LraSoiMinimization::QuickXplain;
  LraSoiMinimizationOrder soi_minimization_order = LraSoiMinimizationOrder::Id;
};

struct LraRowPropagationLimits {
  std::size_t max_row_size = 64;
  std::size_t max_fanout = 64;
  std::size_t bidirectional_max_row_size = 32;
  std::size_t bidirectional_max_fanout = 32;
};

struct AutoBudget {
  enum class Kind {
    Auto,
    Unlimited,
    Fixed,
  };

  Kind kind = Kind::Auto;
  std::size_t value = 0;
};

enum class CheckSatResult {
  Sat,
  Unsat,
  Unknown,
  Unsupported,
};

enum class NelsonOppenPropagationMode {
  Exact,
  Final,
  Online,
};

enum class QfUflraLraEqualityProducer {
  Legacy,
  ModelUnit,
  Adaptive,
};

struct CheckSatOutcome {
  CheckSatResult result = CheckSatResult::Unknown;
  std::string message;
  util::Statistics statistics;
  std::string model;
  InconclusiveReason reason = InconclusiveReason::None;
  std::shared_ptr<const core::Model> typed_model = {};
};

struct CheckSatOptions {
  IncrementalMode incremental_mode = IncrementalMode::Rebuild;
  // Default-off exhaustive Boolean case splitting used to exercise AnyOf
  // execution. The tactic materializes at most 2^16 alternatives.
  std::size_t experimental_split_bools = 0;
  PreprocessingMode preprocessing_mode = PreprocessingMode::Auto;
  EufEngine euf_engine = EufEngine::Incremental;
  EufPropagationMode euf_propagation = EufPropagationMode::Auto;
  PureTheoryFilterMode pure_theory_filter = PureTheoryFilterMode::Auto;
  TheoryPhaseMode theory_phase = TheoryPhaseMode::Negative;
  EufExplainSearchMode euf_explain_search = EufExplainSearchMode::OneSidedBfs;
  bool euf_proof_forest_diagnostics = false;
  bool euf_diseq_propagation_diagnostics = false;
  bool euf_ackermann_diagnostics = false;
  bool nelson_oppen_diagnostics = false;
  bool nelson_oppen_model_equality_branching = false;
  bool nelson_oppen_model_equality_mutation = true;
  ModelEqualityMutationScope nelson_oppen_model_equality_mutation_scope =
      ModelEqualityMutationScope::All;
  ModelEqualityBranchPolicy nelson_oppen_model_equality_branch_policy =
      ModelEqualityBranchPolicy::First;
  NelsonOppenPropagationMode nelson_oppen_propagation = NelsonOppenPropagationMode::Final;
  bool euf_dynamic_ackermann_shadow = false;
  bool euf_diseq_ackermann = false;
  AutoBudget euf_propagation_budget;
  AutoBudget euf_propagation_delivery{AutoBudget::Kind::Fixed, 1000};
  // QF_LRA same-slack bound propagation (Milestone 3b: Dutertre & de Moura
  // Section 4.2's "deriving satisfied atoms"). Unlike euf_propagation,
  // this is a plain on/off performance switch, not a multi-mode choice --
  // the propagation check is exact and cheap (O(atoms sharing a slack)),
  // so there's no heuristic budget/backoff tuning to expose.
  bool lra_propagation = true;
  // Experimental pure-QF_LRA use of Bjørner--Nachmanson Section 3.2.
  // Proved variable equalities propagate existing difference comparisons;
  // the mixed-theory producer remains independently configured below.
  bool lra_equality_propagation = false;
  // Row-based/interval bound propagation (Dutertre & de Moura Section 4,
  // generalizing lra_propagation's same-slack case to derive an implied
  // bound for a row from its terms' current bounds). Only takes effect
  // when lra_propagation is also enabled. Opt-in (materially new
  // algorithmic machinery, not yet campaign-fuzzed) -- see
  // docs/arithmetic.md's Milestone roadmap.
  bool lra_row_propagation = false;
  // Competitiveness-roadmap item 7: the reverse direction of
  // lra_row_propagation -- tighten one of a row's *other* terms from the
  // row's own basic-variable bound plus its remaining terms, rather than
  // only deriving the row's own implied bound. Only takes effect when
  // lra_row_propagation is also enabled. Its own flag (not folded into
  // lra_row_propagation) so the two directions can be A/B tested
  // independently -- see docs/arithmetic.md's Milestone roadmap.
  bool lra_bidirectional_row_propagation = false;
  // Cost caps for the two row-propagation directions above -- see
  // LraRowPropagationLimits. Inert unless lra_row_propagation is on.
  LraRowPropagationLimits lra_row_propagation_limits;
  // Competitiveness-roadmap item 8: theory-guided decisions via CaDiCaL's
  // cb_decide -- the arithmetic theory actively ranks and supplies a
  // decision literal on (almost) every decision, rather than leaving every
  // decision to CaDiCaL's generic VSIDS. Independent of lra_propagation/
  // lra_row_propagation (it fires at decision time, not propagation time),
  // so it has its own flag rather than depending on either -- see
  // docs/arithmetic.md's Milestone roadmap.
  bool lra_theory_decisions = false;
  // H5 model-based phase selection: keep CaDiCaL's VSIDS variable choice
  // but refresh arithmetic atom polarities from the online tableau's current
  // assignment at quiescence points. An unset policy resolves to on for declared
  // QF_LRA rebuild queries with automatic preprocessing, off elsewhere.
  // Explicit true/false overrides it.
  // Low-level engines without a declared logic treat an unset policy as off.
  std::optional<bool> lra_model_phase;
  // Default since 2026-07-29 (heuristics-note item H3). On the 237-file
  // stratified sample this solves 168 against Bland's 144, PAR2 1538.9s
  // against 1962.4s, with zero regressions and zero wrong answers -- see
  // docs/arithmetic.md. Bland remains reachable via --lra-pivoting-rule
  // bland, and check() still falls back to Bland selection past a per-call
  // pivot threshold for termination.
  LraPivotingRule lra_pivoting_rule = LraPivotingRule::Sparse;
  // Only meaningful with LraPivotingRule::Sparse -- see
  // LraTableau::set_sparse_leaving. Default-on with it: the entering-side
  // rule alone reaches 154/237, this takes it to 168, and it is the whole of
  // the latendresse result (0/15 -> 6/15, matching z3).
  bool lra_sparse_leaving = true;
  bool lra_least_violated_leaving = false;
  // Numeric Check/Pivot knobs -- see LraTuning.
  LraTuning lra_tuning;
  // See MixedDispatchMode. Narrow keeps pure QF_LRA on the pure path.
  MixedDispatchMode mixed_dispatch = MixedDispatchMode::Narrow;
  // Heuristics-note item H7 -- see LraTableau::set_fixed_elimination.
  // Opt-in (--lra-fixed-elimination): on the 595-file scrambled competition
  // set it solves 374 against 373 with it off, a median 0.94x speedup over
  // commonly-solved files, and +19/-18 churn that nets to nothing. Its one
  // real effect is on the largest instances (>5MB: 7 -> 14 of 50), paid for
  // in the 50-500KB band. Worth enabling on equality-dominated input.
  bool lra_fixed_elimination = false;
  // Fixed-variable elimination is part of the mixed convex equality-sharing
  // path by default. It exposes EqRow consequences and reduces exact fallback
  // probes without changing the pure QF_LRA default above. The mixed setting
  // remains independently disableable for ablation.
  bool qf_uflra_fixed_elimination = true;
  bool uf_symmetry_breaking = true;
  bool finite_domain_value_precedence = true;
  bool finite_domain_equality_definitions = true;
  bool finite_domain_post_amo = true;
  bool auto_finite_domain_equality_definitions = false;
  bool eq_bridge = true;
  bool ground_congruence = false;
  bool ground_unit_substitution = false;
  bool guarded_ground_unit_substitution = false;
  bool guarded_real_equality_lowering = false;
  // Monniaux (CAV 2009), Section 2.1: normalize proportional Real atoms to
  // one primitive-integer core term before clausification. Opt-in because the
  // first broad A/B changed QF_LRA search trajectories and lost one net solve.
  bool arithmetic_atom_canonicalization = false;
  // The simplify-extended-terms leaf (ite/div/mod/abs rewrites that run before
  // lowering). Lowering itself is unconditional -- the linear core cannot see
  // these kinds -- but the rewrites are a technique like any other and have to
  // be switchable to be measurable. docs/open-leads.md lead 16 measures against
  // this switch.
  bool extended_simplification = true;
  // Traversals the leaf runs. One is a single bottom-up pass, which cannot see
  // the nodes its own rules create; more rounds distribute nested ITEs further
  // down. Bounded rather than run to a fixed point because the equality
  // distribution doubles its assertion each time it fires: unbounded, the
  // largest nec-smt file took 116s and 23GB against 3.0s with the leaf off.
  std::size_t extended_simplification_rounds = 1;
  // The (= u (ite c a b)) distribution specifically, separate from the leaf's
  // own switch because it is the only rule there that grows the formula.
  //
  // Off by default on measurement. Over the full QF_LIA division it is the
  // only arm that loses to simply not running the leaf: preprocessed size
  // +5.75% overall and +9.91% on nec-smt, for 0.36 percentage points more
  // auxiliary reduction than the rest of the leaf achieves on its own. Leaving
  // it on cost about 44 solves at 60s. Outside nec-smt it is very nearly free
  // -- size -0.03% and 30% fewer auxiliaries -- but that is 4368 auxiliaries
  // over 10447 files against 25 million on nec-smt, so the families that carry
  // the weight decide it.
  bool ite_equality_distribution = true;
  // Constant if-then-else simplification, open-leads lead 16 piece 1. On:
  // measured +625 solves on the 2780 nec-smt files at 60s with zero lost and
  // zero wrong answers, against a promotion bar of +200, and 38% less time on
  // the files both configurations solve.
  bool cite_simplification = true;
  // QF_LIA branch-and-bound's node cap (docs/qf-lia-branch-and-bound.md).
  // Plain branch-and-bound has no completeness argument of its own -- an
  // unbounded integer-infeasible polytope can branch forever -- so this is
  // what turns that into a reported Unknown instead of a hang. Not yet
  // empirically tuned; picked generously enough to clear small/medium
  // QF_LIA instances while still bailing out in reasonable time.
  std::size_t lia_branch_limit = 100000;
  // QF_LIA Gomory-cut budget (docs/qf-lia-branch-and-bound.md, stage 3):
  // caps how many cut-and-recheck rounds apply_gomory_cuts attempts
  // before falling through to branch-and-bound regardless. Gomory's
  // method has no anti-cycling guarantee as implemented here (that would
  // need a specific lexicographic discipline this project does not
  // attempt), so an unbounded loop is not safe. Not yet empirically
  // tuned; picked to be cheap relative to lia_branch_limit, since each
  // cut round is itself a full check() re-solve.
  std::size_t lia_gomory_cut_limit = 100;
  // QF_LIA conflict generalization (docs/open-leads.md lead 12d): emit the
  // integer stages' own derivation-based explanation as the round's theory
  // lemma instead of the coarse clause naming every arithmetic atom in the
  // problem. The coarse clause excludes exactly one Boolean assignment, so
  // without this the outer loop degenerates into model enumeration --
  // measured at 219 literals per lemma over 6,528 rounds on
  // calypto/problem-001542 where the derivation used 4 bounds, and 1,266
  // literals over 19,630 rounds on convert/convert-jpg2gif-query-1141
  // where it used ~19. On by default; kept switchable because it changes
  // search trajectories and so needs to stay A/B-measurable. Turning it
  // off restores the pre-generalization coarse clause exactly.
  bool lia_conflict_generalization = true;
  // QF_LIA patching budget (docs/qf-lia-branch-and-bound.md, stage 5;
  // Bjorner & Nachmanson Sect. 4.1): caps how many individual nonbasic moves
  // patch_integer_variables may take per round. Patching is the paper's FIRST
  // waterfall layer and the only one Primo has that tries to *find* an integer
  // model rather than refute a candidate -- which is where the measured gap
  // is, since the full corpus puts Primo ahead of cvc5 on unsat and 661 solves
  // behind on sat. Each move costs a scan over the Int variables to score it,
  // so this is a real budget rather than a safety valve. **Setting it to 0
  // disables patching**, which is the A/B control. Not yet empirically tuned.
  std::size_t lia_patch_limit = 100;
  // The same declared-QF_LRA rebuild policy as lra_model_phase. Assumptions
  // sessions cannot use assertion-dependent elimination; their automatic policy
  // is off and explicit enabling remains rejected. See docs/arithmetic.md.
  std::optional<bool> monotone_elimination;
  bool solved_constant_substitution = true;
  bool gaussian_elimination = true;
  bool qf_uflra_candidate_final_scan = false;
  // Mixed-only choice between the legacy exact-offset row indexes, the
  // current-model-filtered unit-row producer from Bjorner/Nachmanson CAV 2024
  // Section 3.2, and an adaptive mode that bootstraps with legacy through its
  // first propagation fixed point, uses model-unit thereafter, and returns to
  // legacy when shared-model reconciliation still requires a SAT split after a
  // model-unit round. Pure QF_LRA never calls any producer.
  QfUflraLraEqualityProducer qf_uflra_lra_equality_producer = QfUflraLraEqualityProducer::Legacy;
  // Default Section 4 shared-model reconciliation for convex QF_UFLRA.
  // LRA-entailed equalities that EUF can absorb are retained in the round's
  // EUF model instead of being materialized as SAT atoms. The explained exact
  // path remains available explicitly and as the conservative fallback for
  // failed candidate invariants.
  bool qf_uflra_model_reconciliation = true;
  // Transitional A/B path for the backend-neutral model-combination
  // coordinator. It remains default-off until QF_UFLRA parity is established.
  bool experimental_generic_model_combination = false;
  // Default non-convex QF_UFLIA model guidance. Candidate equalities are
  // reversible SAT assumptions; exhaustion or the decision budget activates
  // the complete arrangement baseline.
  bool qf_uflia_model_combination = true;
  // Reuse trail-synchronized EUF and rational-arithmetic component state.
  // Integer completion remains a final, non-convex check on a private tableau
  // copy, and sparse growth retains the complete-arrangement restart.
  bool qf_uflia_online_components = true;
  std::size_t qf_uflia_model_decision_budget = 512;
  bool qf_uflra_online_euf = false;
  bool top_level_or_tseitin = false;
  bool auto_top_level_or_tseitin = true;
  bool auto_basic_single_preprocessing = true;
  bool auto_basic_single_predicate_polarity = true;
  bool auto_basic_multi_preprocessing = true;
  bool auto_basic_multi_polarity = true;
  AutoPreprocessSelectorMode auto_preprocess_selector = AutoPreprocessSelectorMode::Legacy;
  AutoPreprocessTuning auto_preprocess_tuning;
  BooleanImpliesRewriteMode boolean_implies_rewrite = BooleanImpliesRewriteMode::Off;
  QfUfPortfolioMode qf_uf_portfolio = QfUfPortfolioMode::Off;
  // Diagnostic: stop before search; quantified input stops after the frontend.
  bool preprocess_only = false;
  std::size_t euf_propagation_interval = 32;
  double euf_propagation_assign_threshold = 0.25;
  std::size_t euf_diseq_ackermann_max_width = 3;
  std::size_t euf_diseq_ackermann_queue_budget = 4096;
  std::size_t euf_diseq_ackermann_per_symbol = 64;
  std::size_t nelson_oppen_model_equality_branch_budget = 16;
  // Optional associative Boolean preprocessing. Zero preserves the core DAG;
  // positive values bound arity growth caused by flattening.
  std::size_t boolean_flatten_threshold = 0;
  // Same pass, applied again AFTER the rewriting preprocessing passes.
  //
  // Why a second application point: since the DAG repair
  // (docs/boolean-dag-flattening.md) TermManager::mk_and / mk_or no longer
  // flatten same-kind children at construction. The rewriting passes build
  // fresh nodes through those, so they reintroduce nesting that the input
  // never had -- and the pre-pass above has already run by then, which is why
  // it reports attempts=0 on inputs that nonetheless end up nested.
  //
  // Default 2048, chosen by measurement rather than intuition. 256 restores
  // most QF_LRA inputs but leaves one of 55 short (20,200 threshold stops on
  // simple-scaled200), and raising the bound costs nothing where the bound
  // exists to protect: on the 94 MB cpachecker-induction.cs_fib instance that
  // motivated the DAG repair, 2048 peaks at 3,346,944 KB against 3,347,208 KB
  // at 256 -- a 264 KB *saving*, with threshold stops falling from 17 to 1.
  //
  // The bound still matters; it is what keeps a pathological input from
  // unbounded arity growth. It is simply not the axis that decides memory on
  // real inputs, because the pass rewrites each reachable node once rather
  // than materialising every prefix. Set 0 to disable.
  std::size_t boolean_flatten_post_threshold = 2048;
  std::size_t preprocessing_passes = 4;
  std::size_t preprocessing_adaptive_extra_passes = 4;
  std::size_t preprocessing_adaptive_shrink_percent = 10;
  std::size_t finite_domain_eqdefs_auto_domain_limit = 8;
  std::size_t finite_domain_eqdefs_auto_clause_limit = 5000;
  std::size_t finite_domain_eqdefs_auto_term_limit = 5000;
  std::optional<std::size_t> nelson_oppen_arrangement_probe_limit;
  // Per-query aggregate SMT conflict budget, shared across retries/fallback.
  // Counts reports, not propagations, instances or internal arithmetic branches.
  std::optional<std::size_t> max_theory_conflicts;
  // Quantified UF: enumerative instances queued per complete assignment; tuples
  // a quantifier may visit without a new lemma before a round with queued
  // lemmas yields (0 never); distinct instances generated in one solve before
  // answering unknown (docs/quantifiers.md).
  std::size_t quantifier_instances_per_round = theory::kDefaultQuantifierInstancesPerRound;
  std::size_t quantifier_stall_tuples = theory::kDefaultQuantifierStallTuples;
  std::size_t quantifier_max_instances = 100000;
  // Normalize quantifier instances before duplicate detection and lemma
  // generation (theory/quantifiers/instance_simplifier.hpp).
  bool quantifier_instance_simplification = true;
  // Tuple enumeration order (docs/papers/fair-adventurous-enumeration.md).
  theory::TupleOrder quantifier_tuple_order = theory::kDefaultTupleOrder;
  std::size_t quantifier_deepening_increment = 2;
  std::uint64_t quantifier_random_seed = 0;
  // Quantified UF: file receiving the instantiation tuples and Skolem lemmas as
  // SMT-LIB pools (smt/quantifiers/pool_export.hpp); empty writes nothing.
  std::string quantifier_export_pools;
  // Single-query QF_UF CNF/conflict artifact; empty disables export.
  std::string export_cnf;
  std::size_t qf_uf_portfolio_conflicts = 2000;
  std::vector<std::pair<std::string, int>> cadical_options;
  std::function<bool()> cancelled;
  bool produce_model = false;
  bool check_model = false;
};

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options = {});
std::string_view to_string(CheckSatResult result);

}  // namespace primo::smt
