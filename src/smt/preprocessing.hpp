#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/model_reconstruction.hpp"

namespace primo::smt {

enum class ArithmeticAtomCanonicalizationScope {
  All,
  ComparisonsOnly,
};

struct ArithmeticAtomCanonicalizationResult {
  std::vector<core::TermId> assertions;
  std::size_t atoms_visited = 0;
  std::size_t atoms_rewritten = 0;
  std::size_t comparisons = 0;
  std::size_t equalities = 0;
  std::size_t constant_folds = 0;
  std::size_t orientation_reversals = 0;
  std::size_t aliases_merged = 0;
  std::size_t duplicate_assertions_removed = 0;
  std::size_t terms_created = 0;
};

// Monniaux, CAV 2009, Section 2.1: scales Real linear predicates to primitive
// integer coefficients with deterministic sign and term order, then rebuilds
// through TermManager so semantic aliases hash-cons before clausification.
// The comparison-only scope is used after mixed purification, where Real-sorted
// Equality nodes define EUF applications and must remain owned by EUF.
ArithmeticAtomCanonicalizationResult canonicalize_arithmetic_atoms(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    ArithmeticAtomCanonicalizationScope scope = ArithmeticAtomCanonicalizationScope::All);

enum class BooleanImpliesRewriteMode {
  Off,
  Clause,
  All,
};

enum class BooleanNegationRewriteMode {
  Off,
  NegationNormalForm,
};

struct BooleanNegationRewriteResult {
  std::vector<core::TermId> assertions;
  std::size_t visited = 0;
  std::size_t rewritten = 0;
  std::size_t terms_created = 0;
};

// Rewrites the Boolean skeleton to negation normal form without entering
// equality atoms or uninterpreted applications. The implementation memoizes
// both polarities of every source DAG node and does not distribute And over Or.
// docs/preprocessing.md specifies the data structure, complexity, boundaries,
// and tests; this is the documented textbook subset, not a paper optimization.
BooleanNegationRewriteResult rewrite_boolean_negations_to_nnf(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    BooleanNegationRewriteMode mode);

struct ImplicitConjunctionAnalysis {
  std::size_t conjunct_occurrences = 0;
  std::size_t unique_conjuncts = 0;
  std::size_t clause_like_conjunct_occurrences = 0;
  std::size_t implication_clause_candidates = 0;

  [[nodiscard]] bool all_clause_like() const {
    return conjunct_occurrences == clause_like_conjunct_occurrences;
  }
};

// Treats the assertion vector as one implicit conjunction without constructing
// an And term. Counts are saturating and each reachable And node is inspected
// once, so sharing in the term DAG cannot amplify traversal cost.
ImplicitConjunctionAnalysis analyze_implicit_conjunction(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions);

struct BooleanImpliesRewriteResult {
  std::vector<core::TermId> assertions;
  std::size_t visited = 0;
  std::size_t clause_candidates = 0;
  std::size_t rewritten = 0;
  std::size_t all_mode_rewrites = 0;
  std::size_t widened_operands = 0;
  std::size_t threshold_stops = 0;
  std::size_t terms_created = 0;
};

BooleanImpliesRewriteResult rewrite_boolean_implications(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    BooleanImpliesRewriteMode mode, std::size_t clause_arity_limit);

class Preprocessing {};

struct BooleanFlatteningResult {
  std::vector<core::TermId> assertions;
  std::size_t attempts = 0;
  std::size_t accepted = 0;
  std::size_t threshold_stops = 0;
  std::size_t operands_copied = 0;
  std::size_t rewrites = 0;
  std::size_t max_arity = 0;
};

// Optionally widens nested And/Or nodes in the reachable assertion DAG. The
// threshold bounds arity growth caused by this pass; explicitly wider source
// nodes remain wider. A zero threshold disables rewriting.
BooleanFlatteningResult flatten_associative_booleans(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions,
                                                     std::size_t max_arity);

struct EqualityBridgeResult {
  std::vector<core::TermId> equalities;
  std::size_t skipped_or_nodes = 0;
};

class EqualityBridge {
 public:
  explicit EqualityBridge(core::TermManager& terms);

  EqualityBridgeResult bridge(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct UnitEqualityPropagationStats {
  std::size_t consequences_added = 0;
  std::size_t query_nodes = 0;
  std::size_t query_memo_hits = 0;
  std::size_t add_term_requests = 0;
  std::size_t add_term_seen_hits = 0;
};

std::vector<core::TermId> propagate_unit_equalities(core::TermManager& terms,
                                                    const std::vector<core::TermId>& assertions,
                                                    UnitEqualityPropagationStats* stats = nullptr);

struct GroundEqualitySubstitutionResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;
  std::size_t units = 0;
  std::size_t rewrites = 0;
};

GroundEqualitySubstitutionResult substitute_ground_unit_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions);

// Creates a fresh internal constant of the given sort. The session owns
// symbol creation, so the lowering pass is handed the capability rather than
// the Frontend, which keeps it usable from a tactic.
using FreshConstantFactory = std::function<core::TermId(core::SortId, std::string_view)>;

struct SimplifyExtendedTermsResult {
  std::vector<core::TermId> assertions;
  std::size_t ite_rewrites = 0;
  std::size_t equalities_distributed = 0;
  std::size_t arithmetic_rewrites = 0;
  // Distributions declined because the ITE has more than one parent. See
  // simplify_extended_terms for why distributing a shared ITE costs more than
  // the auxiliary it avoids.
  std::size_t equality_distributions_blocked = 0;
  // Distributions declined because a branch is itself an ITE, so the rewrite
  // would pay the guard structure without retiring the auxiliary.
  std::size_t equality_distributions_nested = 0;
  // Traversals actually run. One means nothing fired; the last round of a
  // productive run is the one that changes nothing.
  std::size_t rounds = 0;
};

// Rewrites Ite, IntDiv, IntMod and Abs terms before they are lowered.
//
// These rules live here rather than in the TermManager constructors on
// purpose. A constructor fold is permanently on and cannot be measured or
// switched off, and these are the trivial end of the if-then-else rewrite
// system that lead 16 in open-leads.md is about -- a lead whose own falsifier
// requires the techniques to be evaluated together rather than shipped
// individually. The constructors keep only what defines the representation:
// sort checks, constant folding, and the divisor sign normalization that makes
// (div x 3) and (- (div x (- 3))) one node.
//
// Nothing but the frontend builds these four kinds, so a single pass before
// lowering sees all of them.
struct SimplifyConstantItesResult {
  std::vector<core::TermId> assertions;
  std::size_t atoms_visited = 0;
  std::size_t atoms_simplified = 0;
  std::size_t branches_collapsed = 0;
  // Uniformity work only: excludes leaf-set construction and csimp cache keys.
  // Expanded mixed frames can be classified again after their children finish.
  std::size_t uniformity_checks = 0;
  std::size_t leaf_comparisons = 0;
  std::size_t leaf_scan_equivalent = 0;
};

// Constant if-then-else simplification, piece 1 of open-leads lead 16.
// Bromberger sect. 8.2.3, crediting Kim/Somenzi/Jin: an atom comparing a
// constant against a term built only from numbers and ite can be decided
// branch by branch, collapsing wherever every leaf agrees.
//
// Must run before lower-extended-terms, which replaces the ite it reads.
SimplifyConstantItesResult simplify_constant_ites(core::TermManager& terms,
                                                  const std::vector<core::TermId>& assertions);

struct SimplifyExtendedTermsOptions {
  // Bottom-up traversals to run. One pass cannot reach the nodes its own rules
  // create, so a nested ITE under an equality only distributes one level per
  // round. More rounds are *not* free: each distribution doubles its
  // assertion, so iterating to a fixed point is exponential -- on
  // nec-smt/large/getoption_directories/prp-4-113.smt2 an unbounded run
  // reached 116s and 23GB against 3.0s with the leaf off. The bound is the
  // safety valve, not a thoroughness knob. Zero disables the leaf.
  std::size_t rounds = 1;
  // (= u (ite c a b)) -> (and (=> c (= u a)) (=> (not c) (= u b))).
  // Separately switchable from the rest of the leaf because it is the only
  // rule here that *grows* the formula, and it is the one that has to be
  // measured on its own: the collapses, the negated-condition normalization,
  // the Boolean-ITE-to-connective rules and the div/mod/abs identities all
  // shrink what they touch.
  bool distribute_equalities = true;
};

SimplifyExtendedTermsResult simplify_extended_terms(core::TermManager& terms,
                                                    const std::vector<core::TermId>& assertions,
                                                    const SimplifyExtendedTermsOptions& options);

struct LowerExtendedTermsResult {
  std::vector<core::TermId> assertions;
  std::size_t boolean_ites = 0;
  std::size_t auxiliary_ites = 0;
  std::size_t quotients = 0;
  std::size_t abs_rewrites = 0;
};

// Lowers Ite, Abs, IntDiv and IntMod into the Boolean and linear fragment the
// clausifier and the theory solvers accept. Every encoding is a definitional
// extension: the introduced constants are pinned by the assertions introduced
// alongside them, so no model is gained or lost.
//
// This must run on every path, including --preprocessing-mode=off, which is
// why its tactic sits in the unconditional initial sequence rather than in a
// profile-gated round. Reaching a theory solver with one of these kinds still
// present is a pipeline bug, and LinearPolynomial::from_term throws rather
// than silently treating it as a free variable.
LowerExtendedTermsResult lower_extended_terms(core::TermManager& terms,
                                              const std::vector<core::TermId>& assertions,
                                              const FreshConstantFactory& fresh_constant);

struct RealEqualityLoweringResult {
  std::vector<core::TermId> assertions;
  std::size_t lowered = 0;
  std::size_t guarded_distributed = 0;
};

// Rewrites every Equality node whose operands are Real-sorted into
// `And(Le(a,b), Le(b,a))` (valid since Real is a total order). Must run
// after every other preprocessing pass that reasons about genuine
// Equality nodes (solve-eqs, eq-bridging, forced-atom/unit-equality
// propagation) but before Boolean clausification/theory dispatch, so
// NOT(a=b) still reduces to ordinary Boolean structure
// (NOT(AND(Le,Le))) that BooleanClausifier already handles -- this is
// what lets QF_LRA disequality avoid needing case-split machinery in
// LraTableau/OnlineLraPropagator. Non-Real equalities are left
// untouched.
RealEqualityLoweringResult lower_real_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    bool enable_guarded_real_equality_distribution = false);

struct SolvedConstantSubstitutionResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;
  std::size_t candidates = 0;
  std::size_t accepted = 0;
  std::size_t rewrites = 0;
  std::size_t rejected_cycles = 0;
  std::size_t rejected_blowups = 0;
  std::size_t gaussian_candidates = 0;
};

// enable_gaussian_elimination additionally row-reduces (Gauss-Jordan,
// exact rationals) the system of asserted Real-sorted linear equalities
// before the ordinary bare-variable-equality candidate scan, catching
// definitions the latter can't (e.g. `2x = 5`, or a variable only solvable
// jointly from several equations). Opt-in (default false in
// CheckSatOptions) since it's materially new algorithmic machinery, not
// yet campaign-fuzzed.
SolvedConstantSubstitutionResult substitute_solved_constants(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    bool enable_gaussian_elimination = false);

struct MonotoneEliminationResult {
  std::vector<core::TermId> assertions;
  // Sorted by TermId for reproducibility. No dependency order is needed:
  // whichever eliminated variable of a discharged atom is valued last sees
  // every other leaf already valued, and repairs that atom.
  std::vector<MonotoneEliminatedVariable> eliminated;
  std::size_t candidates = 0;
  std::size_t atoms_folded = 0;
};

// Eliminates real variables that occur *one-sidedly*: if, after accounting
// for Boolean polarity, every atom mentioning `x` pushes it the same
// direction, then some extreme value of `x` satisfies all of them at once,
// and each such atom can be replaced by the constant its own occurrence
// needs (true at positive polarity, false at negative).
//
// This is Fourier-Motzkin elimination of a single-sign variable: with no
// opposite bounds to pair against, the pairwise combination step is empty
// and every constraint mentioning the variable is simply discharged. The
// saving is mostly Boolean -- the atom leaves the CNF and the clauses it
// satisfies disappear -- and only secondarily a smaller tableau.
//
// Deliberately conservative. A variable is disqualified unless it is a
// 0-ary Real `Constant` whose every occurrence is a linear leaf of a
// Le/Lt atom: appearing under an uninterpreted application, inside a Real
// equality, or at mixed polarity all force it two-sided. An atom carrying
// any leaf that model reconstruction could not evaluate disqualifies
// every variable in it, so a discharged constraint can always be repaired
// when the model is built.
MonotoneEliminationResult eliminate_monotone_variables(core::TermManager& terms,
                                                       const std::vector<core::TermId>& assertions);

struct BooleanUnitPropagationResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, bool>> assignments;
  std::size_t units = 0;
  std::size_t rewrites = 0;
  std::size_t memo_hits = 0;
  std::size_t memo_misses = 0;
};

BooleanUnitPropagationResult propagate_boolean_units(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions);

struct ForcedAtomPropagationResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, bool>> assignments;
  std::vector<std::pair<core::TermId, core::TermId>> equality_pairs;
  std::vector<std::pair<core::TermId, core::TermId>> disequality_pairs;
  std::size_t units = 0;
  std::size_t disequalities = 0;
  std::size_t rewrites = 0;
  std::size_t memo_hits = 0;
  std::size_t memo_misses = 0;
  std::size_t query_nodes = 0;
  std::size_t query_memo_hits = 0;
  std::size_t add_term_requests = 0;
  std::size_t add_term_seen_hits = 0;
};

ForcedAtomPropagationResult propagate_forced_atoms(core::TermManager& terms,
                                                   const std::vector<core::TermId>& assertions);

struct FiniteDomainAtMostOneResult {
  std::vector<core::TermId> clauses;
  std::size_t domains = 0;
  std::size_t choices = 0;
  std::size_t explicit_disequality_choices = 0;
  std::size_t integer_numeral_choices = 0;
  std::size_t rejected_missing_distinctness = 0;
};

struct FiniteDomainAnalysis {
  std::size_t domains = 0;
  std::size_t choices = 0;
  std::size_t predicted_amo_clauses = 0;
  std::size_t explicit_disequality_choices = 0;
  std::size_t integer_numeral_choices = 0;
  std::size_t rejected_missing_distinctness = 0;
};

class FiniteDomainAtMostOne {
 public:
  explicit FiniteDomainAtMostOne(core::TermManager& terms);

  FiniteDomainAnalysis analyze(const std::vector<core::TermId>& assertions) const;

  FiniteDomainAtMostOneResult generate(const std::vector<core::TermId>& assertions);
  FiniteDomainAtMostOneResult generate(
      const std::vector<core::TermId>& assertions,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities);
  FiniteDomainAtMostOneResult generate(
      const std::vector<core::TermId>& assertions,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_equalities,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities);

 private:
  core::TermManager& terms_;
};

struct FiniteDomainEqualityDefinitionsResult {
  std::vector<core::TermId> clauses;
  std::size_t candidate_term_pairs = 0;
  std::size_t term_pairs = 0;
  std::size_t predicted_clauses = 0;
  std::size_t rejected_domain_size = 0;
  std::size_t rejected_clause_limit = 0;
};

struct FiniteDomainEqualityDefinitionsOptions {
  std::size_t max_domain_size = 0;
  std::size_t max_clauses = 0;
};

class FiniteDomainEqualityDefinitions {
 public:
  explicit FiniteDomainEqualityDefinitions(core::TermManager& terms);

  FiniteDomainEqualityDefinitionsResult generate(
      const std::vector<core::TermId>& assertions,
      FiniteDomainEqualityDefinitionsOptions options = {});

 private:
  core::TermManager& terms_;
};

struct CongruenceDefinitionResult {
  std::vector<core::TermId> clauses;
  std::size_t groups = 0;
  std::size_t skipped_groups = 0;
};

class CongruenceDefinitionGenerator {
 public:
  explicit CongruenceDefinitionGenerator(core::TermManager& terms);

  CongruenceDefinitionResult generate(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct FiniteDomainEvaluationResult {
  std::vector<core::TermId> assertions;
  std::size_t equalities_simplified = 0;
  std::size_t enumeration_cutoffs = 0;
};

class FiniteDomainEvaluator {
 public:
  explicit FiniteDomainEvaluator(core::TermManager& terms);

  FiniteDomainEvaluationResult simplify(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct UfSymmetryBreakingResult {
  std::vector<core::TermId> constraints;
  std::size_t accepted_sets = 0;
  std::size_t values = 0;
  std::size_t terms = 0;
  std::size_t value_precedence_terms = 0;
  std::size_t value_precedence_constraints = 0;
  std::size_t rejected_noninvariant = 0;
};

struct UfSymmetryBreakingOptions {
  bool value_precedence = true;
};

class UfSymmetryBreaker {
 public:
  explicit UfSymmetryBreaker(core::TermManager& terms);

  UfSymmetryBreakingResult break_symmetries(const std::vector<core::TermId>& assertions,
                                            UfSymmetryBreakingOptions options = {});

 private:
  core::TermManager& terms_;
};

}  // namespace primo::smt
