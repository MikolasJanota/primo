#pragma once

#include <functional>
#include <span>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}
namespace primo::util {
class Statistics;
}

namespace primo::smt {

// One-pass, input-only interval opportunity counts. Does not mutate terms or
// solver state. See docs/leads/root-bound-detection.md for the exact subset.
void record_root_bound_diagnostics(const core::TermManager& terms,
                                   std::span<const core::TermId> assertions,
                                   util::Statistics& statistics,
                                   const std::function<bool()>& cancelled = {});

}  // namespace primo::smt
