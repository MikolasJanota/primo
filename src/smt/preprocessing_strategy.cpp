#include "smt/preprocessing_strategy.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/formula_metrics.hpp"
#include "smt/frontend.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

bool within_optional_limit(std::size_t value, std::size_t limit) {
  return limit == 0 || value <= limit;
}

bool exceeds_optional_limit(std::size_t value, std::size_t limit) {
  return limit != 0 && value > limit;
}

bool should_use_auto_basic_finite_domain(std::size_t initial_reachable_terms,
                                         std::size_t finite_domain_count,
                                         const UfSymmetryBreakingResult& symmetry,
                                         const AutoPreprocessTuning& tuning) {
  return within_optional_limit(initial_reachable_terms, tuning.finite_domain_term_limit) &&
         finite_domain_count >= tuning.finite_domain_min_domains && symmetry.accepted_sets != 0 &&
         within_optional_limit(symmetry.terms, tuning.finite_domain_max_symmetry_terms);
}

std::string bool_parameter(bool value) { return value ? "true" : "false"; }

void initialize_skippable_legacy_statistics(util::Statistics& statistics) {
  // The legacy pipeline ran these stages even after an earlier stage produced false. Strategy
  // sequences stop at terminal goals, so establish the same observable zero/default values before
  // execution; stages that run overwrite them with their actual results.
  statistics.set(util::StatKey::GuardedRealEqualityDistributed, 0);
  statistics.set(":no.purification.definitions", 0);
  statistics.set(":no.purification.arithmetic-uf-definitions", 0);
  statistics.set(":no.purification.euf-real-term-definitions", 0);
  statistics.set(":no.purification.reused-applications", 0);
  statistics.set(":no.purification.reused-real-terms", 0);
  statistics.set(":no.purification.memoized-formula-rewrites", 0);
  statistics.set(":no.purification.memoized-arithmetic-rewrites", 0);
  statistics.set(":no.purification.memoized-euf-rewrites", 0);
  statistics.set(":no.purification.skipped-no-sites", 1);
  statistics.set(":preprocessing.boolean-flatten-post.attempts", 0);
  statistics.set(":preprocessing.boolean-flatten-post.accepted", 0);
  statistics.set(":preprocessing.boolean-flatten-post.threshold-stops", 0);
  statistics.set(":preprocessing.boolean-flatten-post.rewrites", 0);
  statistics.set(":preprocessing.boolean-flatten-post.max-arity", 0);
}

std::string profile_name(PreprocessingProfileKind kind) {
  switch (kind) {
    case PreprocessingProfileKind::Off:
      return "off";
    case PreprocessingProfileKind::Basic:
      return "basic";
    case PreprocessingProfileKind::FiniteDomainBasic:
      return "finite-domain-basic";
    case PreprocessingProfileKind::CompactClauseBasic:
      return "compact-clause-basic";
    case PreprocessingProfileKind::Strong:
      return "strong";
    case PreprocessingProfileKind::SingleAssertion:
      return "single-assertion";
    case PreprocessingProfileKind::LargeInputBypass:
      return "large-input-bypass";
    case PreprocessingProfileKind::QfLia:
      return "qf-lia";
  }
  return "off";
}

Tactic make_qf_lia_solve_eqs() {
  return make_goal_tactic("qf-lia-solve-eqs", [](TacticContext& context, const Goal& goal) {
    std::vector<core::TermId> assertions = goal.assertions();
    ModelReconstruction reconstruction = goal.reconstruction();
    constexpr int kMaxSolvedConstantPasses = 8;
    for (int pass = 0; pass < kMaxSolvedConstantPasses; ++pass) {
      SolvedConstantSubstitutionResult solved = substitute_solved_constants(
          context.terms(), assertions, /*enable_gaussian_elimination=*/false);
      context.statistics().increment(util::StatKey::SolvedConstantCandidates, solved.candidates);
      context.statistics().increment(util::StatKey::SolvedConstantAccepted, solved.accepted);
      context.statistics().increment(util::StatKey::SolvedConstantRewrites, solved.rewrites);
      context.statistics().increment(util::StatKey::SolvedConstantRejectedCycles,
                                     solved.rejected_cycles);
      context.statistics().increment(util::StatKey::SolvedConstantRejectedBlowups,
                                     solved.rejected_blowups);
      if (solved.accepted == 0) {
        break;
      }
      assertions = std::move(solved.assertions);
      if (context.produce_model()) {
        reconstruction.conversions = reconstruction.conversions.appended(
            LiaSubstitutionConversion{std::move(solved.substitutions)});
      }
    }
    return AnyOf::single(
        goal.with_assertions(std::move(assertions)).with_reconstruction(std::move(reconstruction)));
  });
}

Tactic make_large_input_solve_eqs(bool gaussian_elimination) {
  return make_goal_tactic(
      "large-input-solve-eqs", [gaussian_elimination](TacticContext& context, const Goal& goal) {
        SolvedConstantSubstitutionResult solved =
            substitute_solved_constants(context.terms(), goal.assertions(), gaussian_elimination);
        context.statistics().increment(util::StatKey::SolvedConstantCandidates, solved.candidates);
        context.statistics().increment(util::StatKey::SolvedConstantAccepted, solved.accepted);
        context.statistics().increment(util::StatKey::SolvedConstantRewrites, solved.rewrites);
        context.statistics().increment(util::StatKey::SolvedConstantRejectedCycles,
                                       solved.rejected_cycles);
        context.statistics().increment(util::StatKey::SolvedConstantRejectedBlowups,
                                       solved.rejected_blowups);
        context.statistics().increment(util::StatKey::SolvedConstantGaussianCandidates,
                                       solved.gaussian_candidates);
        Goal result = goal.with_assertions(std::move(solved.assertions));
        if (!context.produce_model()) {
          return AnyOf::single(std::move(result));
        }
        ModelReconstruction reconstruction = goal.reconstruction();
        reconstruction.conversions = reconstruction.conversions.appended(
            DirectedEqualitySubstitutionConversion{std::move(solved.substitutions)});
        return AnyOf::single(result.with_reconstruction(std::move(reconstruction)));
      });
}

struct LargeInputSolveEqsOptions {
  bool gaussian_elimination = false;
  std::vector<StrategyParameter> strategy_parameters() const {
    return {{"gaussian-elimination", bool_parameter(gaussian_elimination)}};
  }
};

void record_profile_statistics(const PreprocessingProfile& profile, const CheckSatOptions& options,
                               util::Statistics& statistics) {
  const ImplicitConjunctionAnalysis& conjunction = profile.conjunction_analysis;
  statistics.set(":preprocessing.auto.structural.conjunct-occurrences",
                 conjunction.conjunct_occurrences);
  statistics.set(":preprocessing.auto.structural.unique-conjuncts", conjunction.unique_conjuncts);
  statistics.set(":preprocessing.auto.structural.clause-like-conjuncts",
                 conjunction.clause_like_conjunct_occurrences);
  statistics.set(":preprocessing.auto.structural.implication-clause-candidates",
                 conjunction.implication_clause_candidates);
  statistics.set(":preprocessing.auto.selector.structural", profile.structural_selector ? 1 : 0);
  statistics.set(":preprocessing.auto.limit.large-conjuncts",
                 options.auto_preprocess_tuning.large_conjunct_limit);
  statistics.set(":preprocessing.auto.limit.compact-terms",
                 options.auto_preprocess_tuning.compact_term_limit);
  statistics.set(":preprocessing.auto.limit.compact-min-conjuncts",
                 options.auto_preprocess_tuning.compact_min_conjuncts);
  statistics.set(":preprocessing.auto.limit.finite-domain-terms",
                 options.auto_preprocess_tuning.finite_domain_term_limit);
  statistics.set(":preprocessing.auto.limit.finite-domain-min-domains",
                 options.auto_preprocess_tuning.finite_domain_min_domains);
  statistics.set(":preprocessing.auto.limit.finite-domain-max-symmetry-terms",
                 options.auto_preprocess_tuning.finite_domain_max_symmetry_terms);
  statistics.set(":preprocessing.auto.selector.shadow-evaluated",
                 profile.selector_shadow_evaluated ? 1 : 0);
  statistics.set(":preprocessing.auto.selector.legacy-disagreement",
                 profile.selector_legacy_disagreement ? 1 : 0);
  if (profile.auto_single_assertion) {
    statistics.increment(util::StatKey::AutoSingleAssertion);
  }
  if (profile.auto_strong) {
    statistics.increment(util::StatKey::AutoStrong);
  }
  if (profile.auto_basic_single) {
    statistics.increment(util::StatKey::AutoBasicSingle);
  }
  if (profile.auto_basic_multi) {
    statistics.increment(util::StatKey::AutoBasicMulti);
  }
  if (profile.auto_large_assertion_skip) {
    statistics.increment(util::StatKey::AutoLargeAssertionSkip);
  }
  statistics.set(":preprocessing.auto.profile.large-skip",
                 profile.auto_large_assertion_skip ? 1 : 0);
  statistics.set(":preprocessing.auto.profile.finite-domain-basic",
                 profile.auto_basic_single ? 1 : 0);
  statistics.set(":preprocessing.auto.profile.compact-clause-basic",
                 profile.auto_basic_multi ? 1 : 0);
  statistics.set(":preprocessing.auto.profile.strong", profile.auto_strong ? 1 : 0);
  statistics.set(":preprocessing.auto.analysis.finite-domain-domains",
                 profile.finite_domain_analysis.domains);
  statistics.set(":preprocessing.auto.analysis.finite-domain-choices",
                 profile.finite_domain_analysis.choices);
  statistics.set(":preprocessing.auto.analysis.predicted-amo-clauses",
                 profile.finite_domain_analysis.predicted_amo_clauses);
  statistics.set(":preprocessing.finite-domain.explicit-disequality-choices",
                 profile.finite_domain_analysis.explicit_disequality_choices);
  statistics.set(":preprocessing.finite-domain.integer-numeral-choices",
                 profile.finite_domain_analysis.integer_numeral_choices);
  statistics.set(":preprocessing.finite-domain.rejected-missing-distinctness",
                 profile.finite_domain_analysis.rejected_missing_distinctness);
}

}  // namespace

std::vector<StrategyParameter> PreprocessingProfile::strategy_parameters() const {
  return {{"adaptive-shrink-percent", std::to_string(adaptive_shrink_percent)},
          {"kind", profile_name(kind)},
          {"passes", std::to_string(preprocessing_passes)},
          {"strong", bool_parameter(strong_preprocessing)},
          {"use-eq-bridge", bool_parameter(use_eq_bridge)},
          {"use-finite-domain-amo", bool_parameter(use_finite_domain_amo)},
          {"use-symmetry-breaking", bool_parameter(use_symmetry_breaking)}};
}

PreprocessingProfile analyze_preprocessing_profile(core::TermManager& terms,
                                                   const std::vector<core::TermId>& assertions,
                                                   const CheckSatOptions& options, bool is_qf_lia) {
  PreprocessingProfile profile;
  profile.initial_reachable_terms = options.preprocessing_mode == PreprocessingMode::Auto
                                        ? reachable_term_count(terms, assertions)
                                        : 0;
  if (is_qf_lia) {
    // Integer preprocessing is deliberately selected independently from the
    // Real/UF profiles below. Basic and strong currently share the audited
    // Boolean/value-propagation round; strong gains integer finite-domain
    // processing only when that separate contract is enabled.
    profile.kind = PreprocessingProfileKind::QfLia;
    profile.preprocessing_loop_enabled = options.preprocessing_mode == PreprocessingMode::Basic ||
                                         options.preprocessing_mode == PreprocessingMode::Strong;
    profile.strong_preprocessing = options.preprocessing_mode == PreprocessingMode::Strong;
    profile.preprocessing_passes = profile.strong_preprocessing
                                       ? std::max<std::size_t>(options.preprocessing_passes, 3)
                                       : options.preprocessing_passes;
    profile.adaptive_shrink_percent =
        std::min<std::size_t>(options.preprocessing_adaptive_shrink_percent, 100);
    if (profile.strong_preprocessing) {
      FiniteDomainAtMostOne finite_domain(terms);
      profile.raw_finite_domain =
          std::make_shared<const FiniteDomainAtMostOneResult>(finite_domain.generate(assertions));
      profile.finite_domain_analysis.domains = profile.raw_finite_domain->domains;
      profile.finite_domain_analysis.choices = profile.raw_finite_domain->choices;
      profile.finite_domain_analysis.predicted_amo_clauses =
          profile.raw_finite_domain->clauses.size();
      profile.finite_domain_analysis.explicit_disequality_choices =
          profile.raw_finite_domain->explicit_disequality_choices;
      profile.finite_domain_analysis.integer_numeral_choices =
          profile.raw_finite_domain->integer_numeral_choices;
      profile.finite_domain_analysis.rejected_missing_distinctness =
          profile.raw_finite_domain->rejected_missing_distinctness;
      profile.use_finite_domain_amo = true;
      profile.use_finite_domain_equality_definitions = options.finite_domain_equality_definitions;
    }
    return profile;
  }
  const bool auto_mode = options.preprocessing_mode == PreprocessingMode::Auto;
  profile.structural_selector =
      auto_mode && options.auto_preprocess_selector == AutoPreprocessSelectorMode::Structural;
  profile.conjunction_analysis = profile.structural_selector
                                     ? analyze_implicit_conjunction(terms, assertions)
                                     : ImplicitConjunctionAnalysis{};
  const ImplicitConjunctionAnalysis& conjunction = profile.conjunction_analysis;

  const bool auto_candidate_single_assertion = auto_mode && assertions.size() <= 1;
  if (profile.structural_selector) {
    profile.auto_large_assertion_skip = exceeds_optional_limit(
        conjunction.conjunct_occurrences, options.auto_preprocess_tuning.large_conjunct_limit);
    if (!profile.auto_large_assertion_skip) {
      FiniteDomainAtMostOne finite_domain(terms);
      profile.finite_domain_analysis = finite_domain.analyze(assertions);
    }
  } else if (auto_mode) {
    profile.auto_large_assertion_skip = exceeds_optional_limit(
        assertions.size(), options.auto_preprocess_tuning.large_conjunct_limit);
    profile.auto_basic_multi =
        options.auto_basic_multi_preprocessing && assertions.size() > 1 &&
        !profile.auto_large_assertion_skip &&
        within_optional_limit(profile.initial_reachable_terms,
                              options.auto_preprocess_tuning.compact_term_limit);
  }
  const bool preprocessing_enabled = options.preprocessing_mode != PreprocessingMode::Off;
  profile.preprocessing_loop_enabled = preprocessing_enabled && !profile.auto_large_assertion_skip;
  profile.use_symmetry_breaking =
      profile.preprocessing_loop_enabled && options.uf_symmetry_breaking;
  bool auto_basic_single_candidate =
      options.auto_basic_single_preprocessing && auto_candidate_single_assertion &&
      within_optional_limit(profile.initial_reachable_terms,
                            options.auto_preprocess_tuning.finite_domain_term_limit) &&
      profile.use_symmetry_breaking;
  if (profile.structural_selector) {
    auto_basic_single_candidate =
        options.auto_basic_single_preprocessing && !profile.auto_large_assertion_skip &&
        within_optional_limit(profile.initial_reachable_terms,
                              options.auto_preprocess_tuning.finite_domain_term_limit) &&
        profile.finite_domain_analysis.domains >=
            options.auto_preprocess_tuning.finite_domain_min_domains &&
        profile.use_symmetry_breaking;
  }
  if (!profile.structural_selector &&
      (auto_candidate_single_assertion || options.preprocessing_mode == PreprocessingMode::Strong ||
       (auto_mode && assertions.size() > 1 && !profile.auto_basic_multi &&
        !profile.auto_large_assertion_skip))) {
    FiniteDomainAtMostOne finite_domain(terms);
    profile.raw_finite_domain =
        std::make_shared<const FiniteDomainAtMostOneResult>(finite_domain.generate(assertions));
    profile.finite_domain_analysis.domains = profile.raw_finite_domain->domains;
    profile.finite_domain_analysis.choices = profile.raw_finite_domain->choices;
    profile.finite_domain_analysis.predicted_amo_clauses =
        profile.raw_finite_domain->clauses.size();
  }
  if (auto_basic_single_candidate) {
    UfSymmetryBreaker symmetry_breaker(terms);
    profile.analyzed_symmetry = symmetry_breaker.break_symmetries(
        assertions,
        UfSymmetryBreakingOptions{.value_precedence = options.finite_domain_value_precedence});
  }
  profile.auto_basic_single =
      auto_basic_single_candidate && profile.analyzed_symmetry.has_value() &&
      should_use_auto_basic_finite_domain(
          profile.initial_reachable_terms, profile.finite_domain_analysis.domains,
          profile.analyzed_symmetry.value(), options.auto_preprocess_tuning);
  if (profile.structural_selector) {
    const bool enough_conjuncts =
        conjunction.conjunct_occurrences >= options.auto_preprocess_tuning.compact_min_conjuncts;
    profile.auto_basic_multi =
        options.auto_basic_multi_preprocessing && !profile.auto_large_assertion_skip &&
        !profile.auto_basic_single && enough_conjuncts && conjunction.all_clause_like() &&
        profile.finite_domain_analysis.choices != 0 &&
        within_optional_limit(profile.initial_reachable_terms,
                              options.auto_preprocess_tuning.compact_term_limit);
    profile.auto_strong = !profile.auto_large_assertion_skip && !profile.auto_basic_single &&
                          !profile.auto_basic_multi;
    profile.selector_shadow_evaluated = true;
    const bool legacy_large = exceeds_optional_limit(
        assertions.size(), options.auto_preprocess_tuning.large_conjunct_limit);
    const bool legacy_basic_multi =
        options.auto_basic_multi_preprocessing && assertions.size() > 1 && !legacy_large &&
        within_optional_limit(profile.initial_reachable_terms,
                              options.auto_preprocess_tuning.compact_term_limit);
    const bool legacy_basic_single =
        auto_candidate_single_assertion && auto_basic_single_candidate && profile.auto_basic_single;
    const bool legacy_single =
        auto_candidate_single_assertion && !legacy_basic_single && !legacy_large;
    const bool legacy_strong = assertions.size() > 1 && !legacy_large && !legacy_basic_multi;
    const int structural_profile = profile.auto_large_assertion_skip ? 1
                                   : profile.auto_basic_single       ? 2
                                   : profile.auto_basic_multi        ? 3
                                                                     : 4;
    const int legacy_profile = legacy_large          ? 1
                               : legacy_basic_single ? 2
                               : legacy_basic_multi  ? 3
                               : legacy_single       ? 5
                               : legacy_strong       ? 4
                                                     : 0;
    profile.selector_legacy_disagreement = structural_profile != legacy_profile;
  } else {
    profile.auto_single_assertion = auto_candidate_single_assertion && !profile.auto_basic_single;
    profile.auto_strong = auto_mode && assertions.size() > 1 &&
                          !profile.auto_large_assertion_skip && !profile.auto_basic_multi;
  }
  const bool auto_single_assertion_eqdefs_allowed =
      profile.auto_single_assertion &&
      (options.finite_domain_eqdefs_auto_term_limit == 0 ||
       profile.initial_reachable_terms <= options.finite_domain_eqdefs_auto_term_limit);
  profile.strong_preprocessing =
      options.preprocessing_mode == PreprocessingMode::Strong || profile.auto_strong;
  profile.use_eq_bridge =
      options.eq_bridge || profile.strong_preprocessing || profile.auto_single_assertion;
  profile.use_finite_domain_amo = profile.strong_preprocessing || profile.auto_single_assertion;
  if (profile.structural_selector && profile.use_finite_domain_amo) {
    FiniteDomainAtMostOne finite_domain(terms);
    profile.raw_finite_domain =
        std::make_shared<const FiniteDomainAtMostOneResult>(finite_domain.generate(assertions));
  }
  profile.use_bounded_auto_equality_definitions =
      options.finite_domain_equality_definitions &&
      options.auto_finite_domain_equality_definitions &&
      options.preprocessing_mode == PreprocessingMode::Auto &&
      (profile.auto_strong || auto_single_assertion_eqdefs_allowed);
  profile.use_finite_domain_equality_definitions =
      options.finite_domain_equality_definitions &&
      (options.preprocessing_mode == PreprocessingMode::Strong ||
       profile.use_bounded_auto_equality_definitions);
  profile.use_ground_congruence_definitions = options.ground_congruence;
  profile.use_ground_unit_substitution = options.ground_unit_substitution;
  profile.use_guarded_ground_unit_substitution =
      options.guarded_ground_unit_substitution && !options.ground_unit_substitution;
  profile.preprocessing_passes = (profile.strong_preprocessing || profile.auto_single_assertion)
                                     ? std::max<std::size_t>(options.preprocessing_passes, 3)
                                     : options.preprocessing_passes;
  profile.adaptive_shrink_percent =
      std::min<std::size_t>(options.preprocessing_adaptive_shrink_percent, 100);

  if (profile.auto_large_assertion_skip) {
    profile.kind = PreprocessingProfileKind::LargeInputBypass;
  } else if (profile.auto_basic_single) {
    profile.kind = PreprocessingProfileKind::FiniteDomainBasic;
  } else if (profile.auto_basic_multi) {
    profile.kind = PreprocessingProfileKind::CompactClauseBasic;
  } else if (profile.auto_single_assertion) {
    profile.kind = PreprocessingProfileKind::SingleAssertion;
  } else if (profile.strong_preprocessing) {
    profile.kind = PreprocessingProfileKind::Strong;
  } else if (options.preprocessing_mode == PreprocessingMode::Off) {
    profile.kind = PreprocessingProfileKind::Off;
  } else {
    profile.kind = PreprocessingProfileKind::Basic;
  }
  return profile;
}

Strategy make_preprocessing_strategy(const CheckSatOptions& options,
                                     const PreprocessingProfile& profile,
                                     const std::vector<core::TermId>& symmetry_source_assertions) {
  std::vector<Strategy> children;
  children.push_back(Strategy::leaf(
      "preprocessing-profile", profile,
      Tactic("preprocessing-profile", [](TacticContext&, const AnyOf& input) { return input; })));
  if (profile.kind == PreprocessingProfileKind::QfLia) {
    children.push_back(
        Strategy::leaf("qf-lia-solve-eqs", NoStrategyParameters{}, make_qf_lia_solve_eqs()));
  } else if (profile.auto_large_assertion_skip && options.solved_constant_substitution) {
    const LargeInputSolveEqsOptions solve_options{.gaussian_elimination =
                                                      options.gaussian_elimination};
    children.push_back(
        Strategy::leaf("large-input-solve-eqs", solve_options,
                       make_large_input_solve_eqs(solve_options.gaussian_elimination)));
  }
  if (profile.preprocessing_loop_enabled && profile.preprocessing_passes > 0) {
    if (profile.kind == PreprocessingProfileKind::QfLia) {
      const NormalizeBooleanOptions integer_boolean_options{
          .stage = BooleanNormalizationStage::Final,
          .flatten_threshold = options.boolean_flatten_post_threshold,
          .negation_mode = BooleanNegationRewriteMode::NegationNormalForm,
          .implication_mode = BooleanImpliesRewriteMode::Off};
      children.push_back(Strategy::leaf("normalize-boolean", integer_boolean_options,
                                        make_normalize_boolean(integer_boolean_options)));
    }
    const bool integer_profile = profile.kind == PreprocessingProfileKind::QfLia;
    const PreprocessingRoundOptions round_options{
        .solved_constant_substitution = options.solved_constant_substitution,
        .gaussian_elimination = !integer_profile && options.gaussian_elimination,
        .use_eq_bridge = !integer_profile && profile.use_eq_bridge,
        .use_finite_domain_evaluation = false,
        .use_ground_congruence_definitions =
            !integer_profile && profile.use_ground_congruence_definitions,
        .use_ground_unit_substitution = !integer_profile && profile.use_ground_unit_substitution,
        .use_guarded_ground_unit_substitution =
            !integer_profile && profile.use_guarded_ground_unit_substitution};
    const std::size_t max_passes =
        profile.preprocessing_passes + options.preprocessing_adaptive_extra_passes;
    Strategy round = Strategy::leaf("preprocessing-round", round_options,
                                    make_preprocessing_round(round_options));
    children.push_back(Strategy::repeat(
        std::move(round), max_passes, "adaptive-reachable-term-shrink",
        [baseline_passes = profile.preprocessing_passes,
         adaptive_shrink_percent = profile.adaptive_shrink_percent,
         max_passes](TacticContext& context, const AnyOf& before, const AnyOf& after,
                     std::size_t completed_passes) {
          context.statistics().increment(util::StatKey::PreprocessingChangedPasses);
          if (truth(after, context) != GoalTruth::Inconclusive) {
            return false;
          }
          if (completed_passes < baseline_passes) {
            return true;
          }
          const std::size_t before_size =
              reachable_term_count(context.terms(), before.alternatives().front().assertions());
          const std::size_t after_size =
              reachable_term_count(context.terms(), after.alternatives().front().assertions());
          const bool shrank_enough =
              after_size < before_size &&
              after_size * 100 <= before_size * (100 - adaptive_shrink_percent);
          if (shrank_enough && completed_passes < max_passes) {
            context.statistics().increment(util::StatKey::PreprocessingAdaptivePasses);
            return true;
          }
          return false;
        },
        [](const TacticContext&, const AnyOf& before, const AnyOf& after) {
          if (before.size() != after.size()) {
            return false;
          }
          for (std::size_t index = 0; index < before.size(); ++index) {
            if (before.alternatives()[index].assertions() !=
                after.alternatives()[index].assertions()) {
              return false;
            }
          }
          return true;
        },
        [](TacticContext& context, const AnyOf&, const AnyOf&, std::size_t) {
          context.statistics().increment(util::StatKey::PreprocessingPasses);
          // Keep the historical public statistic even though preprocessing-pass is no longer an
          // executable tactic. The repeat observer is the compatibility boundary for one
          // completed preprocessing pass.
          context.statistics().increment(":tactic.preprocessing-pass.invocations");
        },
        "preprocessing-fixed-point"));
  }
  const FiniteDomainPreprocessOptions finite_options{
      .evaluate_equalities = false,
      .generate_ground_congruence = false,
      .symmetry_breaking = profile.use_symmetry_breaking,
      .value_precedence = options.finite_domain_value_precedence,
      .generate_at_most_one = profile.use_finite_domain_amo,
      .generate_post_at_most_one = options.finite_domain_post_amo,
      .generate_equality_definitions = profile.use_finite_domain_equality_definitions,
      .equality_definition_max_domain_size = profile.use_bounded_auto_equality_definitions
                                                 ? options.finite_domain_eqdefs_auto_domain_limit
                                                 : 0,
      .equality_definition_max_clauses = profile.use_bounded_auto_equality_definitions
                                             ? options.finite_domain_eqdefs_auto_clause_limit
                                             : 0};
  children.push_back(Strategy::leaf(
      "finite-domain-preprocess", finite_options,
      make_finite_domain_preprocess(
          finite_options,
          FiniteDomainPreprocessInput{.source_assertions = symmetry_source_assertions,
                                      .raw_at_most_one = profile.raw_finite_domain,
                                      .analyzed_symmetry = profile.analyzed_symmetry})));
  const NormalizeArithmeticOptions arithmetic_options{
      .stage = ArithmeticNormalizationStage::Catchup,
      .canonicalize = options.arithmetic_atom_canonicalization,
      .lower_real_equalities = true,
      .guarded_real_equality_lowering = options.guarded_real_equality_lowering};
  children.push_back(Strategy::leaf("normalize-arithmetic", arithmetic_options,
                                    make_normalize_arithmetic(arithmetic_options)));
  if (options.monotone_elimination.value_or(false)) {
    const EliminateMonotoneOptions monotone_options;
    children.push_back(Strategy::leaf("eliminate-monotone", monotone_options,
                                      make_eliminate_monotone(monotone_options)));
  }
  const PurifyMixedOptions purify_options{.canonicalize_arithmetic_after =
                                              options.arithmetic_atom_canonicalization};
  children.push_back(
      Strategy::leaf("purify-mixed", purify_options, make_purify_mixed(purify_options)));
  const NormalizeBooleanOptions boolean_options{
      .stage = BooleanNormalizationStage::Final,
      .flatten_threshold = options.boolean_flatten_post_threshold,
      .implication_mode = options.boolean_implies_rewrite};
  children.push_back(Strategy::leaf("normalize-boolean", boolean_options,
                                    make_normalize_boolean(boolean_options)));
  return Strategy::sequence(std::move(children));
}

PreparedPreprocessingStrategy prepare_and_execute_preprocessing_strategy(
    Frontend& frontend, std::vector<core::TermId> assertions, CheckSatOptions& options,
    TacticContext& context) {
  // Resolve the query's owned copy using its declared logic, before any
  // transformation can erase the last arithmetic atom or change dispatch.
  // Assumptions sessions retain their incrementally safe default profile.
  const bool automatic_lra = frontend.logic() == "QF_LRA" &&
                             options.incremental_mode == IncrementalMode::Rebuild &&
                             options.preprocessing_mode == PreprocessingMode::Auto;
  options.lra_model_phase = options.lra_model_phase.value_or(automatic_lra);
  options.monotone_elimination = options.monotone_elimination.value_or(automatic_lra);
  initialize_skippable_legacy_statistics(context.statistics());
  const bool is_qf_lia = frontend.logic() == "QF_LIA" || frontend.logic() == "QF_UFLIA";
  if (frontend.logic() == "QF_LIA" && options.preprocessing_mode == PreprocessingMode::Auto) {
    // docs/open-leads.md lead 12a audits only syntactic solved-constant
    // substitution over Int. Real Gaussian candidates, monotone elimination,
    // and the general fixed-point profile remain disabled until their integer
    // proof obligations are discharged.
    // Keep pure QF_LIA's default profile frozen at the audited substitution-only path.
    options.preprocessing_mode = PreprocessingMode::Off;
  } else if (frontend.logic() == "QF_UFLIA" &&
             options.preprocessing_mode == PreprocessingMode::Auto) {
    // The integer-safe Boolean/value-propagation profile is the evaluated QF_UFLIA default.
    // It deliberately excludes Gaussian elimination and the Real/UF-only transformations
    // documented above. Explicit off remains available for controlled comparisons.
    options.preprocessing_mode = PreprocessingMode::Basic;
  }
  std::vector<Strategy> initial_children;
  // Unconditional, and first: the arithmetic and Boolean normalizers below,
  // the profile analysis that follows, and every solver path all assume the
  // linear and Boolean fragment. This sequence runs before the preprocessing
  // mode is consulted, so there is no setting that can skip lowering.
  // Simplify before lowering: once an ite becomes an auxiliary constant plus
  // two guarded equalities, none of these rules can fire. The simplification
  // is a technique and is switchable so it can be measured; the lowering below
  // it is not, because the linear core cannot see these kinds at all.
  if (options.extended_simplification) {
    const SimplifyExtendedTermsOptions extended_options{
        .rounds = options.extended_simplification_rounds,
        .distribute_equalities = options.ite_equality_distribution};
    // CITE first, then the general rewrites. csimp *produces* Boolean ites of
    // the shape (ite c true false), and the Boolean-ite rules in
    // simplify-extended-terms are what turn those into connectives -- which is
    // the thesis's compression step. Running CITE afterwards leaves its own
    // output uncompressed, and on nec-smt's largest file that was the whole of
    // a 190% formula growth.
    if (options.cite_simplification) {
      initial_children.push_back(Strategy::leaf("simplify-constant-ites", NoStrategyParameters{},
                                                make_simplify_constant_ites()));
    }
    initial_children.push_back(Strategy::leaf("simplify-extended-terms", NoStrategyParameters{},
                                              make_simplify_extended_terms(extended_options)));
  }
  initial_children.push_back(
      Strategy::leaf("lower-extended-terms", NoStrategyParameters{}, make_lower_extended_terms()));
  if (options.arithmetic_atom_canonicalization) {
    const NormalizeArithmeticOptions arithmetic_options{
        .stage = ArithmeticNormalizationStage::Initial,
        .canonicalize = true,
        .lower_real_equalities = false,
        .guarded_real_equality_lowering = false};
    initial_children.push_back(Strategy::leaf("normalize-arithmetic", arithmetic_options,
                                              make_normalize_arithmetic(arithmetic_options)));
  }
  const NormalizeBooleanOptions boolean_options{
      .stage = BooleanNormalizationStage::Initial,
      .flatten_threshold = options.boolean_flatten_threshold,
      .implication_mode = BooleanImpliesRewriteMode::Off};
  initial_children.push_back(Strategy::leaf("normalize-boolean", boolean_options,
                                            make_normalize_boolean(boolean_options)));
  const Strategy initial = Strategy::sequence(std::move(initial_children));
  AnyOf normalized = compile(initial)(context, AnyOf::single(Goal(std::move(assertions))));
  const std::vector<core::TermId> normalized_assertions =
      normalized.alternatives().front().assertions();
  PreprocessingProfile profile =
      analyze_preprocessing_profile(frontend.terms(), normalized_assertions, options, is_qf_lia);
  record_profile_statistics(profile, options, context.statistics());
  context.statistics().set(util::StatKey::PreprocessingTermSizeInitial,
                           reachable_term_count(frontend.terms(), normalized_assertions));
  if (profile.use_finite_domain_amo) {
    context.statistics().increment(util::StatKey::FiniteDomainRawChoices,
                                   profile.raw_finite_domain->choices);
    context.statistics().increment(util::StatKey::FiniteDomainRawAmoClauses,
                                   profile.raw_finite_domain->clauses.size());
  } else if (profile.auto_basic_single) {
    context.statistics().increment(util::StatKey::FiniteDomainRawChoices,
                                   profile.finite_domain_analysis.choices);
    context.statistics().increment(util::StatKey::FiniteDomainRawAmoClauses,
                                   profile.finite_domain_analysis.predicted_amo_clauses);
  }
  const Strategy remaining = make_preprocessing_strategy(options, profile, normalized_assertions);
  AnyOf result = compile(remaining)(context, normalized);
  const Strategy full = Strategy::sequence({initial, remaining});
  return PreparedPreprocessingStrategy{
      .strategy = full, .result = std::move(result), .profile = std::move(profile)};
}

}  // namespace primo::smt
