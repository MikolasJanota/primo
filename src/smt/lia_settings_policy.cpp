#include "smt/lia_settings_policy.hpp"

#include "smt/lia_settings.hpp"

namespace primo::smt {

LiaSettings default_lia_settings(LiaRoute route) {
  // Both routes currently run the engine defaults. The decision to give a
  // route its own default, such as the integrality options on QF_LIA only,
  // is made here; the --help defaults in main.cpp must then follow it.
  static_cast<void>(route);
  return LiaSettings{};
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
  return settings;
}

}  // namespace primo::smt
