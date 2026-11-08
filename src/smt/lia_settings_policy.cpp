#include "smt/lia_settings_policy.hpp"

#include "smt/lia_settings.hpp"

namespace primo::smt {

LiaSettings default_lia_settings(LiaRoute route) {
  LiaSettings settings;
  if (route == LiaRoute::QfLia) {
    // The four integrality switches of docs/papers/mip-presolve-integrality.md
    // together: in the paired 120 s rerun at 2e120fa they solve a net 53 more
    // QF_LIA files, cut at 60 s and at 120 s. QF_UFLIA keeps them off, since
    // there they lose 31 wisas files (search-order effect, same page).
    settings.round_integer_bounds = true;
    settings.primitive_int_rows = true;
    settings.integer_slacks = true;
    settings.patch_integral_moves = true;
  }
  return settings;
}

LiaSettings resolve_lia_settings(LiaRoute route, const LiaSettingOverrides& overrides) {
  LiaSettings settings = default_lia_settings(route);
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
