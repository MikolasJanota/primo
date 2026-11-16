#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "core/term_manager.hpp"
#include "sat/clause.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

// Bounded existing-atom subset of Dutertre/de Moura (2006), Section 2 p.3.
// Selection comes from conflict witnesses, not an all-pairs application scan.
class ConflictAckermannSelector {
 public:
  ConflictAckermannSelector(const core::TermManager& terms, util::Statistics& statistics)
      : terms_(terms), statistics_(statistics) {}
  void register_atom(core::TermId atom, sat::Lit literal);
  std::optional<sat::Clause> consider(core::TermId lhs, core::TermId rhs);

 private:
  struct Visit {
    std::size_t generation = 0;
    bool ready = false;
  };
  static std::uint64_t pair_key(core::TermId lhs, core::TermId rhs);
  void count(const char* suffix);
  const core::TermManager& terms_;
  util::Statistics& statistics_;
  std::unordered_map<std::uint64_t, sat::Lit> equalities_;
  std::unordered_map<std::uint64_t, Visit> pairs_;
};

}  // namespace primo::smt
