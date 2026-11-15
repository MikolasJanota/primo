#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <span>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "util/statistics.hpp"

namespace primo::smt::detail {

// Signed first-occurrence queue coalescing, motivated by event scheduling in
// Schulte--Stuckey (TOPLAS 2008, Section 5). This is not their domain fixpoint
// algorithm. See docs/leads/bound-propagation-improvements.md for the proof
// obligations: retain the first reason/order and clear markers on backtracking.
class ArithmeticPropagationQueue {
 public:
  struct Candidate {
    int lit;
    std::vector<sat::Lit> reasons;
    bool row_based;
    bool bidirectional;
    bool variable_equality;
  };

  void push(int lit, std::span<const sat::Lit> reasons, bool row_based, bool bidirectional,
            bool variable_equality, util::Statistics& statistics) {
    assert(lit != 0);
    statistics.increment(util::StatKey::LraPropagationCandidateAttempts);
    const auto var = static_cast<std::size_t>(std::abs(lit));
    if (var >= pending_.size()) pending_.resize(std::max(var + 1, pending_.size() * 2));
    auto& marked = pending_[var][lit > 0 ? 1U : 0U];
    if (marked) {
      statistics.increment(util::StatKey::LraPropagationSuppressedDuplicates);
      return;
    }
    // The incoming span is consumed now; the queue never borrows its storage.
    entries_.push_back(Candidate{
        lit, {reasons.begin(), reasons.end()}, row_based, bidirectional, variable_equality});
    marked = true;
    statistics.increment(util::StatKey::LraPropagationCandidates);
    statistics.increment(util::StatKey::LraPropagationUniqueCandidates);
    statistics.increment(util::StatKey::LraPropagationReasonLiterals, reasons.size());
  }

  bool empty() const { return entries_.empty(); }

  Candidate pop() {
    assert(!entries_.empty());
    Candidate candidate = std::move(entries_.front());
    entries_.pop_front();
    removed(candidate.lit);
    return candidate;
  }

  void clear(util::Statistics& statistics) {
    statistics.increment(util::StatKey::LraPropagationBacktrackDiscarded, entries_.size());
    for (const auto& entry : entries_) removed(entry.lit);
    entries_.clear();
  }

 private:
  void removed(int lit) {
    auto& marked = pending_.at(static_cast<std::size_t>(std::abs(lit)))[lit > 0 ? 1U : 0U];
    assert(marked);
    marked = false;
  }

  std::vector<std::array<bool, 2>> pending_;
  std::deque<Candidate> entries_;
};

}  // namespace primo::smt::detail
