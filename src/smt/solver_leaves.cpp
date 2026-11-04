#include "smt/solver_leaves.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "smt/boolean_solver.hpp"
#include "smt/check_sat.hpp"
#include "smt/experimental_tactics.hpp"
#include "smt/frontend.hpp"
#include "smt/solver_engines.hpp"
#include "smt/solver_strategy.hpp"
#include "smt/solver_tactic.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

Strategy make_session_solver_strategy(Frontend& frontend, CheckSatOptions options,
                                      util::Statistics& statistics, const TacticContext& context,
                                      CnfConflictCollector* collector) {
  options.produce_model = context.produce_model();
  const ExperimentalSplitBoolsOptions split_options{.max_bools = options.experimental_split_bools};
  const auto immutable_options = std::make_shared<const CheckSatOptions>(std::move(options));
  const Tactic boolean_solver = make_solver_tactic(
      "solve-boolean", frontend.terms(), [&, immutable_options, collector](const Goal& goal) {
        return solver_answer_from_outcome(
            solve_boolean_conjunction(frontend.terms(), goal.assertions(), *immutable_options,
                                      statistics, goal.reconstruction().conversions, collector));
      });
  const Tactic euf_solver = make_solver_tactic(
      "solve-qf-uf", frontend.terms(), [&, immutable_options, collector](const Goal& goal) {
        return solver_answer_from_outcome(
            solve_qf_uf_engine(frontend.terms(), goal.assertions(), *immutable_options, statistics,
                               goal.reconstruction().conversions, collector));
      });
  const Tactic lra_solver = make_solver_tactic(
      "solve-qf-lra", frontend.terms(), [&, immutable_options, collector](const Goal& goal) {
        const std::vector<core::TermId> model_terms = context.produce_model()
                                                          ? frontend.declared_real_constant_terms()
                                                          : std::vector<core::TermId>{};
        return solver_answer_from_outcome(solve_qf_lra_engine(
            frontend.terms(), goal.assertions(), model_terms, goal.reconstruction().conversions,
            *immutable_options, statistics, collector));
      });
  const Tactic lra_model_only_solver = make_solver_tactic(
      "solve-qf-lra-model-only", frontend.terms(),
      [&, immutable_options, collector](const Goal& goal) {
        return solver_answer_from_outcome(solve_qf_lra_engine(
            frontend.terms(), goal.assertions(), frontend.declared_real_constant_terms(),
            goal.reconstruction().conversions, *immutable_options, statistics, collector));
      });
  const Tactic mixed_solver = make_solver_tactic(
      "solve-qf-uflra", frontend.terms(), [&, immutable_options](const Goal& goal) {
        const std::vector<core::TermId> model_terms = context.produce_model()
                                                          ? frontend.declared_real_constant_terms()
                                                          : std::vector<core::TermId>{};
        return solver_answer_from_outcome(solve_qf_uflra_engine(
            frontend.terms(), goal.assertions(), model_terms, goal.reconstruction().conversions,
            *immutable_options, statistics));
      });
  const Tactic lia_solver = make_solver_tactic(
      "solve-qf-lia", frontend.terms(), [&, immutable_options](const Goal& goal) {
        const std::vector<core::TermId> model_terms = context.produce_model()
                                                          ? frontend.declared_int_constant_terms()
                                                          : std::vector<core::TermId>{};
        return solver_answer_from_outcome(
            solve_qf_lia_engine(frontend.terms(), goal.assertions(), model_terms,
                                *immutable_options, statistics, goal.reconstruction().conversions));
      });
  const Tactic uflia_solver = make_solver_tactic(
      "solve-qf-uflia", frontend.terms(), [&, immutable_options](const Goal& goal) {
        const std::vector<core::TermId> model_terms = context.produce_model()
                                                          ? frontend.declared_int_constant_terms()
                                                          : std::vector<core::TermId>{};
        return solver_answer_from_outcome(solve_qf_uflia_engine(
            frontend.terms(), goal.assertions(), model_terms, goal.reconstruction().conversions,
            *immutable_options, statistics));
      });
  const SolverStrategyOptions strategy_options{
      .logic = frontend.logic(),
      .mixed_dispatch = immutable_options->mixed_dispatch,
      .has_declared_real_constants = frontend.has_declared_real_constants()};
  Strategy dispatch = Strategy::leaf(
      "solver-dispatch", strategy_options,
      make_solver_dispatch(strategy_options,
                           SolverTacticSet{.boolean_solver = boolean_solver,
                                           .euf_solver = euf_solver,
                                           .lra_solver = lra_solver,
                                           .lra_model_only_solver = lra_model_only_solver,
                                           .mixed_solver = mixed_solver,
                                           .lia_solver = lia_solver,
                                           .uflia_solver = uflia_solver}));
  if (split_options.max_bools == 0) {
    return dispatch;
  }
  return Strategy::sequence({Strategy::leaf("experimental-split-bools", split_options,
                                            make_experimental_split_bools(split_options)),
                             std::move(dispatch)});
}

}  // namespace primo::smt
