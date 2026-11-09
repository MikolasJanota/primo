#include "smt/preprocessing_tactics.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/formula_metrics.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

ModelConversionChain append_boolean_model_assignments(
    const ModelConversionChain& conversions,
    const std::vector<std::pair<core::TermId, bool>>& assignments) {
  BooleanTheoryAssignmentConversion step;
  step.assignments.reserve(assignments.size());
  for (const auto& [term, value] : assignments) {
    step.assignments.push_back(TheoryModelAssignment{term, value, false});
  }
  return conversions.appended(std::move(step));
}

ModelConversionChain append_equality_model_assignments(
    const ModelConversionChain& conversions,
    const std::vector<std::pair<core::TermId, core::TermId>>& equalities) {
  return conversions.appended(DirectedEqualitySubstitutionConversion{equalities});
}

bool accept_guarded_ground_substitution(const std::vector<core::TermId>& before,
                                        const std::vector<core::TermId>& after,
                                        const GroundEqualitySubstitutionResult& result,
                                        const core::TermManager& terms) {
  if (result.rewrites == 0 || after == before) {
    return false;
  }
  const std::size_t before_terms = reachable_term_count(terms, before);
  const std::size_t after_terms = reachable_term_count(terms, after);
  const bool shrank_by_five_percent =
      after_terms < before_terms && after_terms * 100 <= before_terms * 95;
  constexpr std::size_t kLargeSubstitutionRewriteFloor = 1500;
  constexpr std::size_t kLargeSubstitutionAssertionBudget = 1000;
  const bool large_bounded_substitution =
      result.rewrites >= kLargeSubstitutionRewriteFloor &&
      after.size() <= before.size() + kLargeSubstitutionAssertionBudget;
  return shrank_by_five_percent || large_bounded_substitution;
}

enum class RoundPhase {
  EqualityBridge,
  FiniteDomainEvaluation,
  CongruenceDefinitions,
  BooleanUnits,
  ForcedAtoms,
  SolvedConstants,
  GroundUnitSubstitution,
  UnitEqualities,
};

Goal run_phase(TacticContext& context, const Goal& goal, const PreprocessingRoundOptions& options,
               RoundPhase phase) {
  core::TermManager& terms = context.terms();
  util::Statistics& statistics = context.statistics();
  switch (phase) {
    case RoundPhase::EqualityBridge: {
      EqualityBridge bridge(terms);
      EqualityBridgeResult result = bridge.bridge(goal.assertions());
      statistics.increment(util::StatKey::EqBridgeEqualities, result.equalities.size());
      statistics.increment(util::StatKey::EqBridgeSkippedOrNodes, result.skipped_or_nodes);
      if (result.equalities.empty()) {
        return goal;
      }
      std::vector<core::TermId> assertions = goal.assertions();
      assertions.insert(assertions.end(), result.equalities.begin(), result.equalities.end());
      return goal.with_assertions(std::move(assertions));
    }

    case RoundPhase::FiniteDomainEvaluation: {
      FiniteDomainEvaluator evaluator(terms);
      FiniteDomainEvaluationResult result = evaluator.simplify(goal.assertions());
      statistics.increment(util::StatKey::FiniteDomainEvaluatedEqualities,
                           result.equalities_simplified);
      statistics.increment(util::StatKey::FiniteDomainEvaluationCutoffs,
                           result.enumeration_cutoffs);
      return goal.with_assertions(std::move(result.assertions));
    }

    case RoundPhase::CongruenceDefinitions: {
      CongruenceDefinitionGenerator generator(terms);
      CongruenceDefinitionResult result = generator.generate(goal.assertions());
      statistics.increment(util::StatKey::GroundCongruenceClauses, result.clauses.size());
      statistics.increment(util::StatKey::GroundCongruenceSkippedGroups, result.skipped_groups);
      if (result.clauses.empty()) {
        return goal;
      }
      std::vector<core::TermId> assertions = goal.assertions();
      assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
      return goal.with_assertions(std::move(assertions));
    }

    case RoundPhase::BooleanUnits: {
      BooleanUnitPropagationResult result = propagate_boolean_units(terms, goal.assertions());
      statistics.increment(util::StatKey::BooleanUnitFacts, result.units);
      statistics.increment(util::StatKey::BooleanUnitRewrites, result.rewrites);
      statistics.increment(util::StatKey::BooleanUnitMemoHits, result.memo_hits);
      statistics.increment(util::StatKey::BooleanUnitMemoMisses, result.memo_misses);
      Goal updated = goal.with_assertions(std::move(result.assertions));
      if (!context.produce_model() || result.assignments.empty()) {
        return updated;
      }
      ModelReconstruction reconstruction = goal.reconstruction();
      reconstruction.conversions =
          append_boolean_model_assignments(reconstruction.conversions, result.assignments);
      return updated.with_reconstruction(std::move(reconstruction));
    }

    case RoundPhase::ForcedAtoms: {
      ForcedAtomPropagationResult result = propagate_forced_atoms(terms, goal.assertions());
      statistics.increment(util::StatKey::ForcedAtomFacts, result.units);
      statistics.increment(util::StatKey::ForcedAtomDisequalities, result.disequalities);
      statistics.increment(util::StatKey::ForcedAtomRewrites, result.rewrites);
      statistics.increment(util::StatKey::ForcedAtomMemoHits, result.memo_hits);
      statistics.increment(util::StatKey::ForcedAtomMemoMisses, result.memo_misses);
      statistics.increment(util::StatKey::EqualityQueryNodes, result.query_nodes);
      statistics.increment(util::StatKey::EqualityQueryMemoHits, result.query_memo_hits);
      statistics.increment(util::StatKey::EqualityQueryAddTermRequests, result.add_term_requests);
      statistics.increment(util::StatKey::EqualityQueryAddTermSeenHits, result.add_term_seen_hits);
      Goal updated = goal.with_assertions(std::move(result.assertions));
      if (result.equality_pairs.empty() && result.disequality_pairs.empty() &&
          (!context.produce_model() || result.assignments.empty())) {
        return updated;
      }
      ModelReconstruction reconstruction = goal.reconstruction();
      if (context.produce_model()) {
        reconstruction.conversions =
            append_boolean_model_assignments(reconstruction.conversions, result.assignments);
      }
      reconstruction.forced_equalities.insert(reconstruction.forced_equalities.end(),
                                              result.equality_pairs.begin(),
                                              result.equality_pairs.end());
      reconstruction.forced_disequalities.insert(reconstruction.forced_disequalities.end(),
                                                 result.disequality_pairs.begin(),
                                                 result.disequality_pairs.end());
      return updated.with_reconstruction(std::move(reconstruction));
    }

    case RoundPhase::SolvedConstants: {
      SolvedConstantSubstitutionResult solved =
          substitute_solved_constants(terms, goal.assertions(), options.gaussian_elimination);
      statistics.increment(util::StatKey::SolvedConstantCandidates, solved.candidates);
      statistics.increment(util::StatKey::SolvedConstantAccepted, solved.accepted);
      statistics.increment(util::StatKey::SolvedConstantRewrites, solved.rewrites);
      statistics.increment(util::StatKey::SolvedConstantRejectedCycles, solved.rejected_cycles);
      statistics.increment(util::StatKey::SolvedConstantRejectedBlowups, solved.rejected_blowups);
      statistics.increment(util::StatKey::SolvedConstantGaussianCandidates,
                           solved.gaussian_candidates);
      Goal updated = goal.with_assertions(std::move(solved.assertions));
      if (!context.produce_model() || solved.substitutions.empty()) {
        return updated;
      }
      ModelReconstruction reconstruction = goal.reconstruction();
      reconstruction.conversions =
          append_equality_model_assignments(reconstruction.conversions, solved.substitutions);
      return updated.with_reconstruction(std::move(reconstruction));
    }

    case RoundPhase::GroundUnitSubstitution: {
      GroundEqualitySubstitutionResult substitution =
          substitute_ground_unit_equalities(terms, goal.assertions());
      const bool accept = options.use_ground_unit_substitution ||
                          accept_guarded_ground_substitution(
                              goal.assertions(), substitution.assertions, substitution, terms);
      if (!accept) {
        if (substitution.rewrites > 0) {
          statistics.increment(util::StatKey::UnitEqualitySubstitutionRejected);
        }
        return goal;
      }
      statistics.increment(util::StatKey::UnitEqualitySubstitutionUnits, substitution.units);
      statistics.increment(util::StatKey::UnitEqualitySubstitutions, substitution.rewrites);
      statistics.increment(util::StatKey::UnitEqualitySubstitutionAccepted);
      Goal updated = goal.with_assertions(std::move(substitution.assertions));
      if (!context.produce_model() || substitution.substitutions.empty()) {
        return updated;
      }
      ModelReconstruction reconstruction = goal.reconstruction();
      reconstruction.conversions = reconstruction.conversions.appended(
          DirectedEqualitySubstitutionConversion{std::move(substitution.substitutions), true});
      return updated.with_reconstruction(std::move(reconstruction));
    }

    case RoundPhase::UnitEqualities: {
      UnitEqualityPropagationStats result;
      std::vector<core::TermId> assertions =
          propagate_unit_equalities(terms, goal.assertions(), &result);
      statistics.increment(util::StatKey::UnitEqualityConsequences, result.consequences_added);
      statistics.increment(util::StatKey::EqualityQueryNodes, result.query_nodes);
      statistics.increment(util::StatKey::EqualityQueryMemoHits, result.query_memo_hits);
      statistics.increment(util::StatKey::EqualityQueryAddTermRequests, result.add_term_requests);
      statistics.increment(util::StatKey::EqualityQueryAddTermSeenHits, result.add_term_seen_hits);
      return goal.with_assertions(std::move(assertions));
    }
  }
  throw TacticError("unknown preprocessing phase");
}

Tactic make_phase_tactic(std::string name, RoundPhase phase, PreprocessingRoundOptions options) {
  return Tactic(std::move(name), [phase, options](TacticContext& context, const AnyOf& input) {
    std::vector<Goal> output;
    output.reserve(input.size());
    for (const Goal& goal : input.alternatives()) {
      output.push_back(run_phase(context, goal, options, phase));
    }
    return AnyOf(std::move(output));
  });
}

Tactic complete_sequence(std::string name, std::vector<Tactic> tactics) {
  return Tactic(std::move(name),
                [tactics = std::move(tactics)](TacticContext& context, const AnyOf& input) {
                  AnyOf current = input;
                  for (const Tactic& tactic : tactics) {
                    current = tactic(context, current);
                  }
                  return current;
                });
}

std::string bool_parameter(bool value) { return value ? "true" : "false"; }

std::string boolean_stage_parameter(BooleanNormalizationStage stage) {
  return stage == BooleanNormalizationStage::Initial ? "initial" : "final";
}

std::string arithmetic_stage_parameter(ArithmeticNormalizationStage stage) {
  switch (stage) {
    case ArithmeticNormalizationStage::Initial:
      return "initial";
    case ArithmeticNormalizationStage::Catchup:
      return "catchup";
    case ArithmeticNormalizationStage::PostPurification:
      return "post-purification";
  }
  throw TacticError("unknown arithmetic normalization stage");
}

void record_arithmetic_canonicalization(const ArithmeticAtomCanonicalizationResult& result,
                                        util::Statistics& statistics) {
  statistics.increment(util::StatKey::ArithmeticCanonicalizationAtomsVisited, result.atoms_visited);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationAtomsRewritten,
                       result.atoms_rewritten);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationComparisons, result.comparisons);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationEqualities, result.equalities);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationConstantFolds,
                       result.constant_folds);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationOrientationReversals,
                       result.orientation_reversals);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationAliasesMerged,
                       result.aliases_merged);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationDuplicateAssertionsRemoved,
                       result.duplicate_assertions_removed);
  statistics.increment(util::StatKey::ArithmeticCanonicalizationTermsCreated, result.terms_created);
}

}  // namespace

std::vector<StrategyParameter> PreprocessingRoundOptions::strategy_parameters() const {
  return {
      {"equality-bridge", bool_parameter(use_eq_bridge)},
      {"finite-domain-evaluation", bool_parameter(use_finite_domain_evaluation)},
      {"gaussian-elimination", bool_parameter(gaussian_elimination)},
      {"ground-congruence", bool_parameter(use_ground_congruence_definitions)},
      {"ground-unit-substitution", bool_parameter(use_ground_unit_substitution)},
      {"guarded-ground-unit-substitution", bool_parameter(use_guarded_ground_unit_substitution)},
      {"solved-constant-substitution", bool_parameter(solved_constant_substitution)}};
}

std::vector<StrategyParameter> SolveEqsOptions::strategy_parameters() const {
  return {
      {"gaussian-elimination", bool_parameter(gaussian_elimination)},
      {"ground-unit-substitution", bool_parameter(use_ground_unit_substitution)},
      {"guarded-ground-unit-substitution", bool_parameter(use_guarded_ground_unit_substitution)},
      {"solved-constant-substitution", bool_parameter(solved_constant_substitution)}};
}

std::vector<StrategyParameter> DeriveEqualitiesOptions::strategy_parameters() const {
  return {{"equality-bridge", bool_parameter(equality_bridge)},
          {"unit-consequences", bool_parameter(unit_consequences)}};
}

std::vector<StrategyParameter> FiniteDomainPreprocessOptions::strategy_parameters() const {
  return {
      {"evaluate-equalities", bool_parameter(evaluate_equalities)},
      {"equality-definition-max-clauses", std::to_string(equality_definition_max_clauses)},
      {"equality-definition-max-domain-size", std::to_string(equality_definition_max_domain_size)},
      {"equality-definitions", bool_parameter(generate_equality_definitions)},
      {"ground-congruence", bool_parameter(generate_ground_congruence)},
      {"post-at-most-one", bool_parameter(generate_post_at_most_one)},
      {"at-most-one", bool_parameter(generate_at_most_one)},
      {"symmetry-breaking", bool_parameter(symmetry_breaking)},
      {"value-precedence", bool_parameter(value_precedence)}};
}

std::vector<StrategyParameter> NormalizeBooleanOptions::strategy_parameters() const {
  const char* implication = implication_mode == BooleanImpliesRewriteMode::Off      ? "off"
                            : implication_mode == BooleanImpliesRewriteMode::Clause ? "clause"
                                                                                    : "all";
  const char* negation = negation_mode == BooleanNegationRewriteMode::Off ? "off" : "nnf";
  return {{"flatten-threshold", std::to_string(flatten_threshold)},
          {"implication-mode", implication},
          {"negation-mode", negation},
          {"stage", boolean_stage_parameter(stage)}};
}

std::vector<StrategyParameter> NormalizeArithmeticOptions::strategy_parameters() const {
  return {{"canonicalize", bool_parameter(canonicalize)},
          {"guarded-real-equality-lowering", bool_parameter(guarded_real_equality_lowering)},
          {"lower-real-equalities", bool_parameter(lower_real_equalities)},
          {"stage", arithmetic_stage_parameter(stage)}};
}

std::vector<StrategyParameter> PurifyMixedOptions::strategy_parameters() const {
  return {{"canonicalize-arithmetic-after", bool_parameter(canonicalize_arithmetic_after)}};
}

Tactic make_propagate_values(PropagateValuesOptions) {
  const PreprocessingRoundOptions options;
  return complete_sequence(
      "propagate-values",
      {make_phase_tactic("boolean-unit-propagation", RoundPhase::BooleanUnits, options),
       make_phase_tactic("forced-atom-propagation", RoundPhase::ForcedAtoms, options)});
}

Tactic make_solve_eqs(SolveEqsOptions options) {
  const PreprocessingRoundOptions round_options{
      .solved_constant_substitution = options.solved_constant_substitution,
      .gaussian_elimination = options.gaussian_elimination,
      .use_eq_bridge = false,
      .use_finite_domain_evaluation = false,
      .use_ground_congruence_definitions = false,
      .use_ground_unit_substitution = options.use_ground_unit_substitution,
      .use_guarded_ground_unit_substitution = options.use_guarded_ground_unit_substitution,
  };
  std::vector<Tactic> phases;
  if (options.solved_constant_substitution) {
    phases.push_back(make_phase_tactic("solved-constant-substitution", RoundPhase::SolvedConstants,
                                       round_options));
  }
  if (options.use_ground_unit_substitution || options.use_guarded_ground_unit_substitution) {
    phases.push_back(make_phase_tactic("ground-unit-substitution",
                                       RoundPhase::GroundUnitSubstitution, round_options));
  }
  return complete_sequence("solve-eqs", std::move(phases));
}

Tactic make_derive_equalities(DeriveEqualitiesOptions options) {
  const PreprocessingRoundOptions round_options;
  std::vector<Tactic> phases;
  if (options.equality_bridge) {
    phases.push_back(
        make_phase_tactic("equality-bridge", RoundPhase::EqualityBridge, round_options));
  }
  if (options.unit_consequences) {
    phases.push_back(
        make_phase_tactic("unit-equality-propagation", RoundPhase::UnitEqualities, round_options));
  }
  return complete_sequence("derive-equalities", std::move(phases));
}

Tactic make_finite_domain_preprocess(FiniteDomainPreprocessOptions options,
                                     FiniteDomainPreprocessInput input) {
  const PreprocessingRoundOptions round_options;
  std::vector<Tactic> phases;
  if (options.evaluate_equalities) {
    phases.push_back(make_phase_tactic("finite-domain-evaluation",
                                       RoundPhase::FiniteDomainEvaluation, round_options));
  }
  if (options.generate_ground_congruence) {
    phases.push_back(make_phase_tactic("congruence-definitions", RoundPhase::CongruenceDefinitions,
                                       round_options));
  }
  if (options.symmetry_breaking || options.generate_at_most_one ||
      options.generate_equality_definitions) {
    phases.push_back(make_goal_tactic(
        "finite-domain-constraints",
        [options, input = std::move(input)](TacticContext& context, const Goal& goal) {
          std::vector<core::TermId> assertions = goal.assertions();
          util::Statistics& statistics = context.statistics();
          if (options.symmetry_breaking) {
            UfSymmetryBreakingResult symmetry;
            if (input.analyzed_symmetry.has_value()) {
              symmetry = input.analyzed_symmetry.value();
            } else {
              UfSymmetryBreaker symmetry_breaker(context.terms());
              const std::vector<core::TermId>& source = input.symmetry_source_assertions.empty()
                                                            ? assertions
                                                            : input.symmetry_source_assertions;
              symmetry = symmetry_breaker.break_symmetries(
                  source, UfSymmetryBreakingOptions{.value_precedence = options.value_precedence});
            }
            assertions.insert(assertions.end(), symmetry.constraints.begin(),
                              symmetry.constraints.end());
            statistics.increment(util::StatKey::UfSymmetryAcceptedSets, symmetry.accepted_sets);
            statistics.increment(util::StatKey::UfSymmetryValues, symmetry.values);
            statistics.increment(util::StatKey::UfSymmetryTerms, symmetry.terms);
            statistics.increment(util::StatKey::UfSymmetryConstraints, symmetry.constraints.size());
            statistics.increment(util::StatKey::UfSymmetryValuePrecedenceTerms,
                                 symmetry.value_precedence_terms);
            statistics.increment(util::StatKey::UfSymmetryValuePrecedenceConstraints,
                                 symmetry.value_precedence_constraints);
            statistics.increment(util::StatKey::UfSymmetryRejectedNoninvariant,
                                 symmetry.rejected_noninvariant);
          }
          if (options.generate_at_most_one) {
            FiniteDomainAtMostOneResult post;
            if (options.generate_post_at_most_one) {
              FiniteDomainAtMostOne generator(context.terms());
              post = generator.generate(assertions, goal.reconstruction().forced_equalities,
                                        goal.reconstruction().forced_disequalities);
            }
            statistics.increment(util::StatKey::FiniteDomainPostAmoClauses, post.clauses.size());
            std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
            std::size_t added = 0;
            std::size_t deduped = 0;
            const auto emit = [&](const std::vector<core::TermId>& clauses) {
              for (const core::TermId clause : clauses) {
                if (emitted.insert(clause).second) {
                  assertions.push_back(clause);
                  ++added;
                } else {
                  ++deduped;
                }
              }
            };
            emit(input.raw_at_most_one->clauses);
            emit(post.clauses);
            statistics.increment(util::StatKey::FiniteDomainDomains,
                                 std::max(input.raw_at_most_one->domains, post.domains));
            statistics.set(":preprocessing.finite-domain.explicit-disequality-choices",
                           std::max(input.raw_at_most_one->explicit_disequality_choices,
                                    post.explicit_disequality_choices));
            statistics.set(":preprocessing.finite-domain.integer-numeral-choices",
                           std::max(input.raw_at_most_one->integer_numeral_choices,
                                    post.integer_numeral_choices));
            statistics.set(":preprocessing.finite-domain.rejected-missing-distinctness",
                           std::max(input.raw_at_most_one->rejected_missing_distinctness,
                                    post.rejected_missing_distinctness));
            statistics.increment(util::StatKey::FiniteDomainAmoClauses, added);
            statistics.increment(util::StatKey::FiniteDomainDedupedAmoClauses, deduped);
          }
          if (options.generate_equality_definitions) {
            FiniteDomainEqualityDefinitions definitions(context.terms());
            const FiniteDomainEqualityDefinitionsResult result = definitions.generate(
                assertions, FiniteDomainEqualityDefinitionsOptions{
                                .max_domain_size = options.equality_definition_max_domain_size,
                                .max_clauses = options.equality_definition_max_clauses});
            assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
            statistics.increment(util::StatKey::EqualityDefinitions, result.clauses.size());
            statistics.increment(util::StatKey::EqualityDefinitionCandidatePairs,
                                 result.candidate_term_pairs);
            statistics.increment(util::StatKey::EqualityDefinitionAcceptedPairs, result.term_pairs);
            statistics.increment(util::StatKey::EqualityDefinitionPredictedClauses,
                                 result.predicted_clauses);
            statistics.increment(util::StatKey::EqualityDefinitionRejectedDomainSize,
                                 result.rejected_domain_size);
            statistics.increment(util::StatKey::EqualityDefinitionRejectedClauseLimit,
                                 result.rejected_clause_limit);
          }
          return AnyOf::single(goal.with_assertions(std::move(assertions)));
        }));
  }
  return complete_sequence("finite-domain-preprocess", std::move(phases));
}

Tactic make_normalize_boolean(NormalizeBooleanOptions options) {
  return make_goal_tactic(
      "normalize-boolean",
      [options](TacticContext& context, const Goal& goal) {
        std::vector<core::TermId> assertions = goal.assertions();
        util::Statistics& statistics = context.statistics();
        if (options.negation_mode != BooleanNegationRewriteMode::Off) {
          const BooleanNegationRewriteResult negations =
              rewrite_boolean_negations_to_nnf(context.terms(), assertions, options.negation_mode);
          assertions = negations.assertions;
          statistics.set(":preprocessing.boolean-nnf.visited", negations.visited);
          statistics.set(":preprocessing.boolean-nnf.rewritten", negations.rewritten);
          statistics.set(":preprocessing.boolean-nnf.terms-created", negations.terms_created);
        }
        if (options.stage == BooleanNormalizationStage::Final) {
          const BooleanImpliesRewriteResult implication = rewrite_boolean_implications(
              context.terms(), assertions, options.implication_mode, options.flatten_threshold);
          assertions = implication.assertions;
          statistics.set(":preprocessing.boolean-implies.visited", implication.visited);
          statistics.set(":preprocessing.boolean-implies.clause-candidates",
                         implication.clause_candidates);
          statistics.set(":preprocessing.boolean-implies.rewritten", implication.rewritten);
          statistics.set(":preprocessing.boolean-implies.all-mode-rewrites",
                         implication.all_mode_rewrites);
          statistics.set(":preprocessing.boolean-implies.widened-operands",
                         implication.widened_operands);
          statistics.set(":preprocessing.boolean-implies.threshold-stops",
                         implication.threshold_stops);
          statistics.set(":preprocessing.boolean-implies.terms-created", implication.terms_created);
        }
        const BooleanFlatteningResult flattened =
            flatten_associative_booleans(context.terms(), assertions, options.flatten_threshold);
        const std::string prefix = options.stage == BooleanNormalizationStage::Initial
                                       ? ":preprocessing.boolean-flatten."
                                       : ":preprocessing.boolean-flatten-post.";
        statistics.set(prefix + "attempts", flattened.attempts);
        statistics.set(prefix + "accepted", flattened.accepted);
        statistics.set(prefix + "threshold-stops", flattened.threshold_stops);
        if (options.stage == BooleanNormalizationStage::Initial) {
          statistics.set(prefix + "operands-copied", flattened.operands_copied);
        }
        statistics.set(prefix + "rewrites", flattened.rewrites);
        statistics.set(prefix + "max-arity", flattened.max_arity);
        return AnyOf::single(goal.with_assertions(std::move(flattened.assertions)));
      },
      IncrementalSafety::ContextIndependent);
}

Tactic make_simplify_constant_ites() {
  return make_goal_tactic("simplify-constant-ites", [](TacticContext& context, const Goal& goal) {
    SimplifyConstantItesResult simplified =
        simplify_constant_ites(context.terms(), goal.assertions());
    util::Statistics& statistics = context.statistics();
    statistics.increment(util::StatKey::CiteAtomsVisited, simplified.atoms_visited);
    statistics.increment(util::StatKey::CiteAtomsSimplified, simplified.atoms_simplified);
    statistics.increment(util::StatKey::CiteBranchesCollapsed, simplified.branches_collapsed);
    statistics.increment(util::StatKey::CiteUniformityChecks, simplified.uniformity_checks);
    statistics.increment(util::StatKey::CiteLeafComparisons, simplified.leaf_comparisons);
    statistics.increment(util::StatKey::CiteLeafScanEquivalent, simplified.leaf_scan_equivalent);
    return AnyOf::single(goal.with_assertions(std::move(simplified.assertions)));
  });
}

Tactic make_simplify_extended_terms(const SimplifyExtendedTermsOptions& options) {
  return make_goal_tactic(
      "simplify-extended-terms",
      [options](TacticContext& context, const Goal& goal) {
        SimplifyExtendedTermsResult simplified =
            simplify_extended_terms(context.terms(), goal.assertions(), options);
        util::Statistics& statistics = context.statistics();
        statistics.increment(util::StatKey::SimplifiedIteRewrites, simplified.ite_rewrites);
        statistics.increment(util::StatKey::SimplifiedEqualityDistributions,
                             simplified.equalities_distributed);
        statistics.increment(util::StatKey::SimplifiedExtendedArithmetic,
                             simplified.arithmetic_rewrites);
        statistics.increment(util::StatKey::SimplifiedEqualityDistributionsBlocked,
                             simplified.equality_distributions_blocked);
        statistics.increment(util::StatKey::SimplifiedExtendedRounds, simplified.rounds);
        statistics.increment(util::StatKey::SimplifiedEqualityDistributionsNested,
                             simplified.equality_distributions_nested);
        return AnyOf::single(goal.with_assertions(std::move(simplified.assertions)));
      },
      IncrementalSafety::ContextIndependent);
}

Tactic make_lower_extended_terms() {
  return make_goal_tactic(
      "lower-extended-terms",
      [](TacticContext& context, const Goal& goal) {
        LowerExtendedTermsResult lowered =
            lower_extended_terms(context.terms(), goal.assertions(),
                                 [&context](core::SortId sort, std::string_view prefix) {
                                   return context.fresh_constant(sort, prefix);
                                 });
        util::Statistics& statistics = context.statistics();
        statistics.increment(util::StatKey::LoweredBooleanItes, lowered.boolean_ites);
        statistics.increment(util::StatKey::LoweredAuxiliaryItes, lowered.auxiliary_ites);
        statistics.increment(util::StatKey::LoweredDivisionQuotients, lowered.quotients);
        statistics.increment(util::StatKey::LoweredAbsRewrites, lowered.abs_rewrites);
        return AnyOf::single(goal.with_assertions(std::move(lowered.assertions)));
      },
      IncrementalSafety::ConservativeExtension);
}

Tactic make_normalize_arithmetic(NormalizeArithmeticOptions options) {
  return make_goal_tactic(
      "normalize-arithmetic",
      [options](TacticContext& context, const Goal& goal) {
        std::vector<core::TermId> assertions = goal.assertions();
        util::Statistics& statistics = context.statistics();
        if (options.canonicalize) {
          const ArithmeticAtomCanonicalizationScope scope =
              options.stage == ArithmeticNormalizationStage::PostPurification
                  ? ArithmeticAtomCanonicalizationScope::ComparisonsOnly
                  : ArithmeticAtomCanonicalizationScope::All;
          ArithmeticAtomCanonicalizationResult canonicalized =
              canonicalize_arithmetic_atoms(context.terms(), assertions, scope);
          assertions = std::move(canonicalized.assertions);
          record_arithmetic_canonicalization(canonicalized, statistics);
          if (options.stage == ArithmeticNormalizationStage::Catchup) {
            statistics.increment(util::StatKey::ArithmeticCanonicalizationCatchupRewrites,
                                 canonicalized.atoms_rewritten);
          } else if (options.stage == ArithmeticNormalizationStage::PostPurification) {
            statistics.increment(util::StatKey::ArithmeticCanonicalizationPostPurificationRewrites,
                                 canonicalized.atoms_rewritten);
          }
        }
        if (options.lower_real_equalities) {
          RealEqualityLoweringResult lowered = lower_real_equalities(
              context.terms(), assertions, options.guarded_real_equality_lowering);
          assertions = std::move(lowered.assertions);
          statistics.increment(util::StatKey::RealEqualityLowered, lowered.lowered);
          statistics.increment(util::StatKey::GuardedRealEqualityDistributed,
                               lowered.guarded_distributed);
        }
        return AnyOf::single(goal.with_assertions(std::move(assertions)));
      },
      IncrementalSafety::ContextIndependent);
}

Tactic make_eliminate_monotone(EliminateMonotoneOptions) {
  return make_goal_tactic("eliminate-monotone", [](TacticContext& context, const Goal& goal) {
    MonotoneEliminationResult monotone =
        eliminate_monotone_variables(context.terms(), goal.assertions());
    context.statistics().increment(util::StatKey::MonotoneEliminationCandidates,
                                   monotone.candidates);
    context.statistics().increment(util::StatKey::MonotoneEliminationAtomsFolded,
                                   monotone.atoms_folded);
    context.statistics().increment(util::StatKey::MonotoneEliminationVariables,
                                   monotone.eliminated.size());
    Goal result = goal.with_assertions(std::move(monotone.assertions));
    if (!context.produce_model() || monotone.eliminated.empty()) {
      return AnyOf::single(std::move(result));
    }
    ModelReconstruction reconstruction = goal.reconstruction();
    reconstruction.conversions = reconstruction.conversions.appended(
        MonotoneEliminationConversion{std::move(monotone.eliminated)});
    return AnyOf::single(result.with_reconstruction(std::move(reconstruction)));
  });
}

Tactic make_preprocessing_round(PreprocessingRoundOptions options) {
  std::vector<Tactic> phases;
  if (options.use_eq_bridge) {
    phases.push_back(make_derive_equalities(
        DeriveEqualitiesOptions{.equality_bridge = true, .unit_consequences = false}));
  }
  if (options.use_finite_domain_evaluation || options.use_ground_congruence_definitions) {
    phases.push_back(make_finite_domain_preprocess(FiniteDomainPreprocessOptions{
        .evaluate_equalities = options.use_finite_domain_evaluation,
        .generate_ground_congruence = options.use_ground_congruence_definitions}));
  }
  phases.push_back(make_propagate_values());
  phases.push_back(make_solve_eqs(SolveEqsOptions{
      .solved_constant_substitution = options.solved_constant_substitution,
      .gaussian_elimination = options.gaussian_elimination,
      .use_ground_unit_substitution = options.use_ground_unit_substitution,
      .use_guarded_ground_unit_substitution = options.use_guarded_ground_unit_substitution,
  }));
  phases.push_back(make_derive_equalities(
      DeriveEqualitiesOptions{.equality_bridge = false, .unit_consequences = true}));
  return complete_sequence("preprocessing-round", std::move(phases));
}

}  // namespace primo::smt
