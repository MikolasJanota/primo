#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace primo::smt::detail {

// Exact input stamps for the bounded row scan; no endpoints or explanations
// survive a scan. Schulte--Stuckey (TOPLAS 2008, Section 5) motivates relevant
// events; this is conservative engineering memoization, not domain fixpoints.
// See docs/leads/bound-propagation-improvements.md for the invalidation proof.
class ArithmeticRowScanMemo {
 public:
  bool seen(std::size_t variable, std::size_t trail, std::size_t pivots) const {
    if (variable >= stamps_.size()) return false;
    const auto& stamp = stamps_[variable];
    return stamp.generation == generation_ && stamp.trail == trail && stamp.pivots == pivots;
  }

  void remember(std::size_t variable, std::size_t trail, std::size_t pivots) {
    if (variable >= stamps_.size()) stamps_.resize(std::max(variable + 1, stamps_.size() * 2));
    stamps_[variable] = Stamp{generation_, trail, pivots};
  }

  void invalidate() {
    if (++generation_ == 0) {
      std::fill(stamps_.begin(), stamps_.end(), Stamp{});
      generation_ = 1;
    }
  }

 private:
  struct Stamp {
    std::uint64_t generation = 0;
    std::size_t trail = 0;
    std::size_t pivots = 0;
  };
  std::uint64_t generation_ = 1;
  std::vector<Stamp> stamps_;
};

}  // namespace primo::smt::detail
