#include "smt/lia_settings_policy.hpp"

#include "smt/lia_settings.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"

namespace primo::smt {

LiaSettings default_lia_settings(LiaRoute route) {
  LiaSettings settings;
  if (route == LiaRoute::QfLia) {
    // Owner-selected M124 + uniform cubes + hybrid profile. Existing algorithm
    // subsets and qualification: docs/evidence/qf-lia-defaults-20261119.md.
    settings.round_integer_bounds = true;
    settings.primitive_int_rows = true;
    settings.integer_slacks = false;
    settings.patch_integral_moves = true;
    settings.hybrid_branching = true;
    settings.cube_mode = theory::arithmetic::LiaCubeMode::Uniform;
  }
  return settings;
}

LiaSettings resolve_lia_settings(LiaRoute route, const LiaSettingOverrides& overrides) {
  LiaSettings settings = default_lia_settings(route);
  settings.hybrid_branching = overrides.hybrid_branching.value_or(settings.hybrid_branching);
  settings.private_branch_factor =
      overrides.private_branch_factor.value_or(settings.private_branch_factor);
  settings.split_atom_limit = overrides.split_atom_limit.value_or(settings.split_atom_limit);
  settings.branch_limit = overrides.branch_limit.value_or(settings.branch_limit);
  settings.gomory_cut_limit = overrides.gomory_cut_limit.value_or(settings.gomory_cut_limit);
  settings.patch_limit = overrides.patch_limit.value_or(settings.patch_limit);
  settings.conflict_generalization =
      overrides.conflict_generalization.value_or(settings.conflict_generalization);
  settings.round_integer_bounds =
      overrides.round_integer_bounds.value_or(settings.round_integer_bounds);
  settings.primitive_int_rows = overrides.primitive_int_rows.value_or(settings.primitive_int_rows);
  settings.integer_slacks = overrides.integer_slacks.value_or(settings.integer_slacks);
  settings.patch_integral_moves =
      overrides.patch_integral_moves.value_or(settings.patch_integral_moves);
  // Cube qualification currently covers only pure QF_LIA.
  if (route == LiaRoute::QfLia) {
    settings.cube_mode = overrides.cube_mode.value_or(settings.cube_mode);
    settings.cube_steps = overrides.cube_steps.value_or(settings.cube_steps);
  }
  return settings;
}

}  // namespace primo::smt
