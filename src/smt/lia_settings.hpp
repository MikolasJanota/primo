#pragma once

#include <cstddef>

#include "theory/arithmetic/lia_cube_mode.hpp"

namespace primo::smt {

// Resolved settings of the LIA engine: the integer completion waterfall
// (docs/implementation/lia.md) and the integer preparation of the tableau
// (docs/papers/mip-presolve-integrality.md). The engine reads only this
// value, held by value in each LIA theory component; it does not know which
// logic, command line or strategy chose it. lia_settings_policy.hpp decides
// the values per solver route.
struct LiaSettings {
  // Branch-and-bound node cap. Plain branch-and-bound has no completeness
  // argument of its own (an unbounded integer-infeasible polytope can branch
  // forever), so reaching the cap reports Unknown instead of hanging. Not
  // empirically tuned.
  std::size_t branch_limit = 100000;
  // Gomory cut-and-recheck rounds per candidate model before falling through
  // to branch-and-bound. Gomory's method has no anti-cycling guarantee as
  // implemented, so the loop must be bounded. Not empirically tuned.
  std::size_t gomory_cut_limit = 100;
  // Patching budget (Bjorner and Nachmanson, Sect. 4.1): nonbasic moves per
  // round that try to find an integer model. 0 disables patching.
  std::size_t patch_limit = 100;
  // Emit the integer stages' derivation-based explanation as the theory lemma
  // instead of the coarse clause over every arithmetic atom, which excludes
  // only one Boolean assignment per round.
  bool conflict_generalization = true;
  // Gap 1 of docs/papers/mip-presolve-integrality.md: round non-strict
  // thresholds on integer-valued tableau variables inward (King thesis
  // Sect. 4.3). Strict integer thresholds are rounded regardless.
  bool round_integer_bounds = false;
  // Gap 2: store Int atom rows with coprime integer coefficients rather than
  // a leading coefficient of one, so every Int slack is integer-valued and its
  // thresholds can be rounded (King thesis Sect. 4.3).
  bool primitive_int_rows = false;
  // Gap 3: treat slacks whose rows are integer polynomials over Int variables
  // as integer obligations of completion (King thesis Sect. 4.3, footnote 4).
  // Most useful with primitive_int_rows.
  bool integer_slacks = false;
  // Gap 4: keep integer nonbasic variables integral while patching and
  // propose the integer values of a row's nonbasic variable that make its
  // basic variable integral (docs/papers/mip-presolve-integrality.md).
  bool patch_integral_moves = false;

  // Experimental standalone QF_LIA cube test (CAV 2024, Sect. 4.2).
  theory::arithmetic::LiaCubeMode cube_mode = theory::arithmetic::LiaCubeMode::Off;
  std::size_t cube_steps = 1000;

  bool operator==(const LiaSettings&) const = default;
};

}  // namespace primo::smt
