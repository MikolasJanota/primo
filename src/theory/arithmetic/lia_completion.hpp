#pragma once

#include <cstddef>
#include <vector>

#include "theory/arithmetic/lia_explanation.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

struct LiaCompletionLimits {
  std::size_t patch_moves;
  std::size_t gomory_rounds;
  std::size_t branch_nodes;
  // Keep integer nonbasic variables integral while patching.
  bool integral_patch_moves = false;
};

struct LiaCompletionResult {
  enum class Status { Sat, Unsat, ResourceLimit };
  Status status = Status::ResourceLimit;
  LiaExplanation explanation;
  LiaPatchingStats patch_stats;
  std::size_t patched_variables = 0;
};

// Existing Primo waterfall (docs/implementation/lia.md): patch, GCD, bounded
// Gomory cuts, then branch-and-bound. The caller supplies a rationally feasible
// working tableau and the ordered, unique set of integer variables. No copy or
// preparation occurs here. SAT retains the winning assignment; other results
// retain cuts but no speculative branch bounds, as in branch_and_bound.
// One explanation spans all refutation stages so later branches retain cut
// antecedents. SMT integration owns translation to a blocking clause.
LiaCompletionResult complete_integer_assignment(LraTableau& tableau,
                                                const std::vector<LraVarId>& integer_vars,
                                                LiaCompletionLimits limits);

}  // namespace primo::theory::arithmetic
