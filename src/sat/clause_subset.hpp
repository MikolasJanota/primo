#pragma once

#include <cstddef>
#include <vector>

#include "sat/cnf.hpp"
#include "sat/ipasir_up.hpp"
#include "util/minimal_set.hpp"

namespace primo::sat {

enum class ClauseSubsetMode { Core, Mus };

struct ClauseSubsetResult {
  // Unsat means the requested reduction completed. Sat means that hard plus
  // all candidates was SAT. Unknown/Unsupported do not establish minimality.
  SatResult result = SatResult::Unknown;
  std::vector<std::size_t> selected;
  std::size_t sat_calls = 0;
  std::size_t core_removed = 0;
  util::ProgressionStatistics progression;
};

// Owns a fresh incremental-safe SAT instance for the duration of the call.
// Hard clauses are never removed. Every candidate gets a private selector.
// Only the returned indices escape; selectors and SAT search state do not.
// The optional terminator must outlive this call. No SMT/theory checks occur.
ClauseSubsetResult select_unsat_subset(const Cnf& hard, const Cnf& candidates,
                                       ClauseSubsetMode mode, Terminator* terminator = nullptr);

}  // namespace primo::sat
