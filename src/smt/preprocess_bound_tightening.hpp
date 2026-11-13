#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "core/ids.hpp"
#include "smt/strategy.hpp"
#include "util/rational.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

struct PreprocessBoundEndpoint {
  core::TermId term;
  util::Rational value;
  bool upper;
  bool strict;
};

// All values are owned. IDs belong to the unchanged input term manager.
struct PreprocessBoundResult {
  std::vector<PreprocessBoundEndpoint> bounds;
  std::size_t work = 0;
  std::size_t normalized_constraints = 0;
  std::size_t endpoint_updates = 0;
  std::size_t row_visits = 0;
  std::size_t newly_finite = 0;
  std::size_t emission_truncated = 0;
  bool contradiction = false;
  bool exhausted = false;
  bool cancelled = false;
};

struct PreprocessBoundOptions {
  std::size_t work_limit = 100000;
  std::vector<StrategyParameter> strategy_parameters() const;
};

PreprocessBoundResult analyze_preprocessing_bounds(const core::TermManager& terms,
                                                   std::span<const core::TermId> assertions,
                                                   std::size_t work_limit = 100000,
                                                   const std::function<bool()>& cancelled = {});

// Create terms only after analysis is complete. Returns just the new conjuncts.
std::vector<core::TermId> materialize_preprocessing_bounds(core::TermManager& terms,
                                                           const PreprocessBoundResult& result);
Tactic make_preprocess_bound_tightening(PreprocessBoundOptions options);

}  // namespace primo::smt
