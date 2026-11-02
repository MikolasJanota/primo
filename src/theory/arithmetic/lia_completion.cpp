#include "theory/arithmetic/lia_completion.hpp"

#include <vector>

#include "theory/arithmetic/lia_branch_and_bound.hpp"
#include "theory/arithmetic/lia_gcd_consistency.hpp"
#include "theory/arithmetic/lia_gomory_cuts.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

LiaCompletionResult complete_integer_assignment(LraTableau& tableau,
                                                const std::vector<LraVarId>& integer_vars,
                                                LiaCompletionLimits limits) {
  LiaCompletionResult result;
  result.patched_variables = patch_integer_variables(
      tableau, integer_vars, limits.patch_moves, &result.patch_stats, limits.integral_patch_moves);
  if (gcd_inconsistent_row(tableau, integer_vars, &result.explanation).has_value() ||
      apply_gomory_cuts(tableau, integer_vars, limits.gomory_rounds, &result.explanation)) {
    result.status = LiaCompletionResult::Status::Unsat;
    return result;
  }
  switch (
      branch_and_bound(tableau, integer_vars, limits.branch_nodes, &result.explanation).status) {
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
