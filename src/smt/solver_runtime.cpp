#include "smt/solver_runtime.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include "smt/check_sat.hpp"
#include "smt/inconclusive_reason.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
bool theory_conflict_limit_reached(const util::Statistics& statistics,
                                   std::optional<std::size_t> limit) {
  return limit && statistics.get(util::StatKey::TheoryConflictCount) >= *limit;
}
namespace {
CheckSatOutcome limit_outcome(const util::Statistics& statistics) {
  return {CheckSatResult::Unknown, "theory conflict limit reached", statistics, "",
          InconclusiveReason::ResourceLimit};
}
}  // namespace

void record_theory_conflict(util::Statistics& statistics) {
  statistics.increment(util::StatKey::TheoryConflictCount);
}

CheckSatOutcome enforce_theory_conflict_limit(CheckSatOutcome outcome,
                                              const CheckSatOptions& options,
                                              const util::Statistics& statistics) {
  return theory_conflict_limit_reached(statistics, options.max_theory_conflicts)
             ? limit_outcome(statistics)
             : std::move(outcome);
}

SolveTerminator::SolveTerminator(std::function<bool()> cancelled, util::Statistics& statistics,
                                 std::optional<std::size_t> conflict_limit)
    : cancelled_(std::move(cancelled)), statistics_(statistics), conflict_limit_(conflict_limit) {
  if (!statistics_.contains(util::StatKey::TheoryConflictCount))
    statistics_.set(util::StatKey::TheoryConflictCount, 0);
}

bool SolveTerminator::conflict_limit_reached() const {
  return theory_conflict_limit_reached(statistics_, conflict_limit_);
}

std::optional<CheckSatOutcome> SolveTerminator::stop_outcome() {
  // Deliberately exclude the internal growth yield from cancellation outcomes.
  if (conflict_limit_reached()) return limit_outcome(statistics_);
  if (!(cancelled_reached_ || (cancelled_ && cancelled_()))) return std::nullopt;
  cancelled_reached_ = true;
  return CheckSatOutcome{CheckSatResult::Unknown, "cancelled", statistics_, "",
                         InconclusiveReason::Cancelled};
}

bool SolveTerminator::terminate() {
  if (cancelled_ && cancelled_()) {
    cancelled_reached_ = true;
    return true;
  }
  if (conflict_limit_reached()) return true;
  return yield_ && yield_();
}

ScopedTerminatorConnection::ScopedTerminatorConnection(sat::IpasirUpBackend& backend,
                                                       sat::Terminator& terminator)
    : backend_(backend) {
  backend_.connect_terminator(&terminator);
}

ScopedTerminatorConnection::~ScopedTerminatorConnection() { backend_.disconnect_terminator(); }

}  // namespace primo::smt
