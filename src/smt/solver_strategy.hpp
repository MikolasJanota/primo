#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "smt/check_sat.hpp"
#include "smt/lia_settings.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

struct SolverTacticSet {
  Tactic boolean_solver;
  Tactic euf_solver;
  Tactic lra_solver;
  Tactic lra_model_only_solver;
  Tactic mixed_solver;
  Tactic lia_solver;
  Tactic uflia_solver;
};

struct SolverStrategyOptions {
  std::optional<std::string> logic;
  MixedDispatchMode mixed_dispatch = MixedDispatchMode::Narrow;
  bool has_declared_real_constants = false;
  // The resolved settings of the LIA engine on each route. They are part of
  // the dispatch leaf's identity, so the canonical form and fingerprint record
  // the configuration each route runs.
  LiaSettings qf_lia_settings;
  LiaSettings qf_uflia_settings;

  std::vector<StrategyParameter> strategy_parameters() const;
};

// Renders `settings` as strategy parameters whose keys start with `prefix`.
std::vector<StrategyParameter> lia_strategy_parameters(std::string_view prefix,
                                                       const LiaSettings& settings);

// Builds the formula-feature probe and solver-family selection strategy. Each
// alternative is classified independently before it is passed to a leaf.
Tactic make_solver_dispatch(SolverStrategyOptions options, SolverTacticSet solvers);

}  // namespace primo::smt
