#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include "sat/ipasir_up.hpp"
#include "smt/check_sat.hpp"

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// Original reporting/budget contract: docs/reference/statistics.md.
void record_theory_conflict(util::Statistics& statistics);
bool theory_conflict_limit_reached(const util::Statistics& statistics,
                                   std::optional<std::size_t> limit);
CheckSatOutcome enforce_theory_conflict_limit(CheckSatOutcome outcome,
                                              const CheckSatOptions& options,
                                              const util::Statistics& statistics);

// Per-solve cancellation/resource terminator. The object and its connection
// must be owned in the same stack frame as the SAT backend.
class SolveTerminator final : public sat::Terminator {
 public:
  SolveTerminator(std::function<bool()> cancelled, util::Statistics& statistics,
                  std::optional<std::size_t> conflict_limit = std::nullopt);

  // Internal safe-boundary yield is distinct from user cancellation/resources.
  void set_yield(std::function<bool()> yield) { yield_ = std::move(yield); }
  bool terminate() override;
  bool cancelled_reached() const { return cancelled_reached_; }
  bool conflict_limit_reached() const;
  std::optional<CheckSatOutcome> stop_outcome();

 private:
  std::function<bool()> cancelled_;
  std::function<bool()> yield_;
  util::Statistics& statistics_;
  std::optional<std::size_t> conflict_limit_;
  bool cancelled_reached_ = false;
};

class ScopedTerminatorConnection {
 public:
  ScopedTerminatorConnection(sat::IpasirUpBackend& backend, sat::Terminator& terminator);
  ~ScopedTerminatorConnection();

  ScopedTerminatorConnection(const ScopedTerminatorConnection&) = delete;
  ScopedTerminatorConnection& operator=(const ScopedTerminatorConnection&) = delete;

 private:
  sat::IpasirUpBackend& backend_;
};

}  // namespace primo::smt
