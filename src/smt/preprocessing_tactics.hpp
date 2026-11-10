#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "smt/preprocessing.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

class Frontend;

// Immutable settings for one equivalence/equisatisfiability-preserving
// preprocessing round. Strategy construction maps command-line options into
// this value once; the tactic itself does not inspect CLI state.
struct PreprocessingRoundOptions {
  bool solved_constant_substitution = false;
  bool gaussian_elimination = false;
  bool use_eq_bridge = false;
  bool use_finite_domain_evaluation = false;
  bool use_ground_congruence_definitions = false;
  bool use_ground_unit_substitution = false;
  bool use_guarded_ground_unit_substitution = false;

  std::vector<StrategyParameter> strategy_parameters() const;
};

struct PropagateValuesOptions {
  std::vector<StrategyParameter> strategy_parameters() const { return {}; }
};

struct SolveEqsOptions {
  bool solved_constant_substitution = true;
  bool gaussian_elimination = false;
  bool use_ground_unit_substitution = false;
  bool use_guarded_ground_unit_substitution = false;

  std::vector<StrategyParameter> strategy_parameters() const;
};

struct DeriveEqualitiesOptions {
  bool equality_bridge = true;
  bool unit_consequences = true;

  std::vector<StrategyParameter> strategy_parameters() const;
};

struct FiniteDomainPreprocessOptions {
  bool evaluate_equalities = false;
  bool generate_ground_congruence = false;
  bool symmetry_breaking = false;
  bool value_precedence = true;
  bool generate_at_most_one = false;
  bool generate_post_at_most_one = false;
  bool generate_equality_definitions = false;
  std::size_t equality_definition_max_domain_size = 0;
  std::size_t equality_definition_max_clauses = 0;

  std::vector<StrategyParameter> strategy_parameters() const;
};

// Formula snapshots produced by profile analysis before the fixed-point round.
// Cached constraints are valid only for this exact source assertion vector in
// the same term manager. The delayed emission stage refreshes them after any
// intervening transformation, so eliminated constants cannot be reintroduced.
struct FiniteDomainPreprocessInput {
  std::vector<core::TermId> source_assertions;
  std::shared_ptr<const FiniteDomainAtMostOneResult> raw_at_most_one =
      std::make_shared<const FiniteDomainAtMostOneResult>();
  std::optional<UfSymmetryBreakingResult> analyzed_symmetry;
};

enum class BooleanNormalizationStage {
  Initial,
  Final,
};

struct NormalizeBooleanOptions {
  BooleanNormalizationStage stage = BooleanNormalizationStage::Initial;
  std::size_t flatten_threshold = 0;
  BooleanNegationRewriteMode negation_mode = BooleanNegationRewriteMode::Off;
  BooleanImpliesRewriteMode implication_mode = BooleanImpliesRewriteMode::Off;

  std::vector<StrategyParameter> strategy_parameters() const;
};

enum class ArithmeticNormalizationStage {
  Initial,
  Catchup,
  PostPurification,
};

struct NormalizeArithmeticOptions {
  ArithmeticNormalizationStage stage = ArithmeticNormalizationStage::Initial;
  bool canonicalize = false;
  bool lower_real_equalities = false;
  bool guarded_real_equality_lowering = false;

  std::vector<StrategyParameter> strategy_parameters() const;
};

struct EliminateMonotoneOptions {
  std::vector<StrategyParameter> strategy_parameters() const { return {}; }
};

struct PurifyMixedOptions {
  bool canonicalize_arithmetic_after = false;

  std::vector<StrategyParameter> strategy_parameters() const;
};

Tactic make_propagate_values(PropagateValuesOptions options = {});
Tactic make_solve_eqs(SolveEqsOptions options);
Tactic make_derive_equalities(DeriveEqualitiesOptions options);
Tactic make_finite_domain_preprocess(FiniteDomainPreprocessOptions options,
                                     FiniteDomainPreprocessInput input = {});
Tactic make_normalize_boolean(NormalizeBooleanOptions options);
// Lowers Ite, Abs, IntDiv and IntMod into the Boolean and linear fragment.
// This leaf is unconditional: it sits in the initial sequence that runs before
// the preprocessing profile is even chosen, so --preprocessing-mode=off cannot
// skip it. Fresh constants come from the executing TacticContext.
// Rewrites Ite, IntDiv, IntMod and Abs before they are lowered. Runs in the
// initial sequence immediately ahead of the lowering leaf.
Tactic make_simplify_constant_ites();
Tactic make_simplify_extended_terms(const SimplifyExtendedTermsOptions& options);

Tactic make_lower_extended_terms();

Tactic make_normalize_arithmetic(NormalizeArithmeticOptions options);
Tactic make_eliminate_monotone(EliminateMonotoneOptions options = {});
// The fresh proxy constants this needs come from the executing TacticContext,
// so the tactic holds no session reference and stays valid inside a cached
// Strategy. It throws TacticError if the context supplies no factory.
Tactic make_purify_mixed(PurifyMixedOptions options = {});

Tactic make_preprocessing_round(PreprocessingRoundOptions options);

}  // namespace primo::smt
