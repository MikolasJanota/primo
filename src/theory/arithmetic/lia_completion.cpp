#include "theory/arithmetic/lia_completion.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "theory/arithmetic/lia_branch_and_bound.hpp"
#include "theory/arithmetic/lia_cube.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lia_gcd_consistency.hpp"
#include "theory/arithmetic/lia_gomory_cuts.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

LiaCompletionResult complete_integer_assignment(LraTableau& tableau,
                                                const std::vector<LraVarId>& integer_vars,
                                                LiaCompletionLimits limits,
                                                const LiaCubeProblem* cube_problem,
                                                LiaBranchHistory* history,
                                                const std::vector<LraVarId>* original_vars) {
  LiaCompletionResult result;
  result.patched_variables = patch_integer_variables(
      tableau, integer_vars, limits.patch_moves, &result.patch_stats, limits.integral_patch_moves);
  if (gcd_inconsistent_row(tableau, integer_vars, &result.explanation).has_value()) {
    result.status = LiaCompletionResult::Status::Unsat;
    return result;
  }
  if (cube_problem && limits.cube_mode != LiaCubeMode::Off) {
    result.cube_called = true;
    const auto start = std::chrono::steady_clock::now();
    result.cube =
        try_cube_model(tableau, *cube_problem, integer_vars, limits.cube_mode, limits.cube_steps);
    result.cube_microseconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count());
    if (result.cube.status == LiaCubeResult::Status::Sat) {
      result.status = LiaCompletionResult::Status::Sat;
      return result;
    }
  }
  if (apply_gomory_cuts(tableau, integer_vars, limits.gomory_rounds, &result.explanation)) {
    result.status = LiaCompletionResult::Status::Unsat;
    return result;
  }
  if (limits.hybrid && (!history || !original_vars))
    throw std::logic_error("hybrid completion requires solve-scoped history and original terms");
  const auto budget =
      limits.hybrid ? lia_private_branch_budget(limits.branch_nodes, limits.private_branch_factor,
                                                original_vars->size())
                    : limits.branch_nodes;
  const auto branches = branch_and_bound(tableau, integer_vars, budget, &result.explanation,
                                         limits.hybrid ? history : nullptr);
  result.branch_nodes = branches.nodes;
  result.branch_conflicts = branches.conflicts;
  switch (branches.status) {
    case LiaBranchAndBoundResult::Status::Sat:
      result.status = LiaCompletionResult::Status::Sat;
      break;
    case LiaBranchAndBoundResult::Status::Unsat:
      result.status = LiaCompletionResult::Status::Unsat;
      break;
    case LiaBranchAndBoundResult::Status::LimitReached:
      result.branch_budget_exit = true;
      result.status = LiaCompletionResult::Status::ResourceLimit;
      if (limits.hybrid) {
        // Sparse pop restores bounds, not beta. Repair before reading a semantic
        // split or accepting the now-unrestricted model (Griggio Section 5 handoff).
        if (const auto conflict = tableau.check()) {
          result.explanation.add_all(*conflict);
          result.status = LiaCompletionResult::Status::Unsat;
          break;
        }
        if (std::ranges::all_of(integer_vars, [&](LraVarId var) {
              return is_integer_valued(tableau.assignment(var));
            })) {
          result.status = LiaCompletionResult::Status::Sat;
          break;
        }
        result.split_variable = history->select(tableau, *original_vars);
        if (!result.split_variable)
          throw std::logic_error("fractional integer slack with integral original terms");
        result.split_floor = delta_floor(tableau.assignment(*result.split_variable));
        result.status = LiaCompletionResult::Status::Split;
      }
      break;
  }
  return result;
}

}  // namespace primo::theory::arithmetic
