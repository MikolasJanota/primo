#include "theory/arithmetic/lia_completion.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "theory/arithmetic/lia_branch_and_bound.hpp"
#include "theory/arithmetic/lia_cube.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lia_gcd_consistency.hpp"
#include "theory/arithmetic/lia_gomory_cuts.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

LiaCompletionResult complete_integer_assignment(LraTableau& tableau,
                                                const std::vector<LraVarId>& integer_vars,
                                                LiaCompletionLimits limits,
                                                const LiaCubeProblem* cube_problem) {
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
  const auto branches =
      branch_and_bound(tableau, integer_vars, limits.branch_nodes, &result.explanation);
  result.branch_nodes = branches.nodes;
  switch (branches.status) {
    case LiaBranchAndBoundResult::Status::Sat:
      result.status = LiaCompletionResult::Status::Sat;
      break;
    case LiaBranchAndBoundResult::Status::Unsat:
      result.status = LiaCompletionResult::Status::Unsat;
      break;
    case LiaBranchAndBoundResult::Status::LimitReached:
      result.status = LiaCompletionResult::Status::ResourceLimit;
      break;
  }
  return result;
}

}  // namespace primo::theory::arithmetic
