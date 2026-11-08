#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// Owning snapshot of the pristine preparation basis. Every nonbasic variable
// must be a semantic Int coordinate; every row must use only those coordinates.
// No terms, SAT atoms, or references into preparation storage are retained.
struct LiaCubeProblem {
  using Definition = std::vector<std::pair<LraVarId, util::Rational>>;
  std::vector<LraVarId> coordinates;
  std::vector<Definition> definitions;
  std::vector<bool> basic;
  static LiaCubeProblem from_tableau(const LraTableau& base, std::vector<LraVarId> coordinates);
};

struct LiaCubeResult {
  enum class Status { Skipped, Infeasible, LimitReached, CandidateRejected, Sat };
  Status status = Status::Skipped;
  std::size_t constraints = 0;
  std::size_t fixed_coordinates = 0;
  std::size_t difference_exemptions = 0;
  std::size_t pivots = 0;
  bool center_found = false;
};

// Bjorner and Nachmanson, Arithmetic Solving in Z3, CAV 2024, Sect. 4.2;
// docs/leads/lia-cube-test.md. Failure is never a refutation. Only a checked
// integer model replaces tableau; every other result leaves it unchanged.
LiaCubeResult try_cube_model(LraTableau& tableau, const LiaCubeProblem& problem,
                             const std::vector<LraVarId>& integer_vars, LiaCubeMode mode,
                             std::size_t max_steps);

}  // namespace primo::theory::arithmetic
