#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "theory/arithmetic/lia_cube.hpp"
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
  LiaCubeMode cube_mode = LiaCubeMode::Off;
  std::size_t cube_steps = 1000;
};

struct LiaCompletionResult {
  enum class Status { Sat, Unsat, ResourceLimit };
  Status status = Status::ResourceLimit;
  LiaExplanation explanation;
  LiaPatchingStats patch_stats;
  std::size_t patched_variables = 0;
  LiaCubeResult cube;
  bool cube_called = false;
  std::size_t branch_nodes = 0;
  std::uint64_t cube_microseconds = 0;
};

// Primo waterfall (docs/implementation/lia.md): patch, GCD, optional cube
// model attempt, bounded Gomory cuts, then branch-and-bound. The caller supplies a rationally
// feasible working tableau and the ordered, unique set of integer variables. The optional cube
// problem owns original semantic definitions; its private solve can replace the tableau only with a
// checked integer model. SAT retains the winning assignment; other results retain cuts but no
// speculative branch bounds, as in branch_and_bound. One explanation spans all refutation stages so
// later branches retain cut antecedents. SMT integration owns translation to a blocking clause.
LiaCompletionResult complete_integer_assignment(LraTableau& tableau,
                                                const std::vector<LraVarId>& integer_vars,
                                                LiaCompletionLimits limits,
                                                const LiaCubeProblem* cube_problem = nullptr);

}  // namespace primo::theory::arithmetic
