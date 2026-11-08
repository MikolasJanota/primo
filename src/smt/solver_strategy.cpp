#include "smt/solver_strategy.hpp"

#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"
#include "smt/formula_analysis.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/lia_settings.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

std::vector<StrategyParameter> lia_strategy_parameters(std::string_view prefix,
                                                       const LiaSettings& settings) {
  const auto flag = [](bool value) { return std::string(value ? "true" : "false"); };
  const std::string key(prefix);
  using theory::arithmetic::LiaCubeMode;
  const std::string cube = settings.cube_mode == LiaCubeMode::Uniform      ? "uniform"
                           : settings.cube_mode == LiaCubeMode::Difference ? "difference"
                                                                           : "off";
  return {{key + "branch-limit", std::to_string(settings.branch_limit)},
          {key + "cube", cube},
          {key + "cube-steps", std::to_string(settings.cube_steps)},
          {key + "conflict-generalization", flag(settings.conflict_generalization)},
          {key + "gomory-cut-limit", std::to_string(settings.gomory_cut_limit)},
          {key + "integer-slacks", flag(settings.integer_slacks)},
          {key + "patch-integral-moves", flag(settings.patch_integral_moves)},
          {key + "patch-limit", std::to_string(settings.patch_limit)},
          {key + "primitive-int-rows", flag(settings.primitive_int_rows)},
          {key + "round-integer-bounds", flag(settings.round_integer_bounds)}};
}

std::vector<StrategyParameter> SolverStrategyOptions::strategy_parameters() const {
  const std::string dispatch = mixed_dispatch == MixedDispatchMode::Narrow ? "narrow" : "wide";
  std::vector<StrategyParameter> parameters{
      {"has-declared-real-constants", has_declared_real_constants ? "true" : "false"},
      {"logic", logic.value_or("")},
      {"mixed-dispatch", dispatch}};
  const auto append = [&parameters](std::vector<StrategyParameter> route) {
    parameters.insert(parameters.end(), std::make_move_iterator(route.begin()),
                      std::make_move_iterator(route.end()));
  };
  append(lia_strategy_parameters("qf-lia.", qf_lia_settings));
  append(lia_strategy_parameters("qf-uflia.", qf_uflia_settings));
  return parameters;
}

Tactic make_solver_dispatch(SolverStrategyOptions options, SolverTacticSet solvers) {
  const auto scheduler = std::make_shared<SequentialTacticScheduler>();
  const Tactic boolean_then_euf =
      first_terminal("boolean-then-euf", scheduler, {solvers.boolean_solver, solvers.euf_solver});

  return Tactic("solver-dispatch", [options = std::move(options), solvers = std::move(solvers),
                                    boolean_then_euf](TacticContext& context, const AnyOf& input) {
    std::vector<Goal> inconclusive;
    inconclusive.reserve(input.size());
    for (const Goal& goal : input.alternatives()) {
      if (goal.reason() != InconclusiveReason::None) {
        if (goal.reason() == InconclusiveReason::Cancelled) {
          return AnyOf::single(goal);
        }
        inconclusive.push_back(goal);
        continue;
      }
      const GoalTruth goal_truth = truth(goal, context);
      if (goal_truth == GoalTruth::True &&
          !(options.logic == "QF_UFLIA" && context.produce_model())) {
        return AnyOf::single(goal);
      }
      if (goal_truth == GoalTruth::False) {
        continue;
      }

      FormulaFeatureSummary features;
      std::unordered_map<core::TermId, FormulaFeatureSummary> memo;
      for (const core::TermId assertion : goal.assertions()) {
        features |=
            summarize_formula_features(context.terms(), assertion, memo, options.mixed_dispatch);
      }

      const Tactic* selected = nullptr;
      if (options.logic == "QF_LIA") {
        selected = &solvers.lia_solver;
      } else if (options.logic == "QF_UFLIA") {
        selected = &solvers.uflia_solver;
      } else if (((features.has_equality || features.has_euf_predicate) &&
                  features.has_arithmetic) ||
                 (context.produce_model() && options.logic == "QF_UFLRA" &&
                  options.has_declared_real_constants &&
                  (features.has_equality || features.has_application_term))) {
        // A transformation such as monotone elimination can remove the final arithmetic atom
        // while leaving EUF structure. The mixed model builder is still required to combine that
        // EUF interpretation with values reconstructed for declared Real constants.
        selected = &solvers.mixed_solver;
      } else if (features.has_arithmetic) {
        selected = &solvers.lra_solver;
      } else if (context.produce_model() && options.has_declared_real_constants &&
                 !features.has_equality && !features.has_euf_predicate) {
        selected = &solvers.lra_model_only_solver;
      } else if (features.has_equality || features.has_application_term) {
        selected = &solvers.euf_solver;
      } else {
        selected = &boolean_then_euf;
      }

      Goal solver_goal = goal;
      if (selected == &solvers.uflia_solver && goal.assertions().empty() &&
          context.produce_model()) {
        solver_goal = goal.with_assertions({context.terms().mk_bool(true)});
      }
      AnyOf result = (*selected)(context, AnyOf::single(std::move(solver_goal)));
      const GoalTruth result_truth = truth(result, context);
      if (result_truth == GoalTruth::True) {
        return result;
      }
      if (result_truth == GoalTruth::False) {
        continue;
      }
      for (const Goal& alternative : result.alternatives()) {
        if (alternative.reason() == InconclusiveReason::None &&
            truth(alternative, context) == GoalTruth::False) {
          continue;
        }
        if (alternative.reason() == InconclusiveReason::Cancelled) {
          return AnyOf::single(alternative);
        }
        inconclusive.push_back(alternative);
      }
    }

    if (inconclusive.empty()) {
      return AnyOf::single(Goal({context.terms().mk_bool(false)}));
    }
    return AnyOf(std::move(inconclusive));
  });
}

}  // namespace primo::smt
