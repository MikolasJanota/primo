#pragma once

#include <cstddef>
#include <optional>

#include "smt/lia_settings.hpp"

namespace primo::smt {

// The two solver routes that run the LIA engine. The declared logic selects
// the route (solver_strategy.cpp), so a route default is a per-logic default.
enum class LiaRoute { QfLia, QfUflia };

// LIA settings a user requested explicitly, for example on the command line.
// An unset field leaves the route's default in place.
struct LiaSettingOverrides {
  std::optional<std::size_t> branch_limit;
  std::optional<std::size_t> gomory_cut_limit;
  std::optional<std::size_t> patch_limit;
  std::optional<bool> conflict_generalization;
  std::optional<bool> round_integer_bounds;
  std::optional<bool> primitive_int_rows;
  std::optional<bool> integer_slacks;
  std::optional<bool> patch_integral_moves;
};

// The settings a route runs with when nothing is overridden. This is the one
// place that decides route defaults; both routes currently use LiaSettings{}.
LiaSettings default_lia_settings(LiaRoute route);

// The route's default with every set override applied. An override always
// wins over the default.
LiaSettings resolve_lia_settings(LiaRoute route, const LiaSettingOverrides& overrides);

}  // namespace primo::smt
