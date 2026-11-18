#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"
#include "sat/clause.hpp"
#include "smt/clausifier.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

// Query-owned safe-boundary growth; callbacks only enqueue copied semantic IDs.
// Dutertre/de Moura (2006), Section 2 p.3, with Primo's bounded admission policy.
class AckermannGrowthState {
 public:
  AckermannGrowthState(core::TermManager& terms, util::Statistics& statistics,
                       std::size_t atom_limit, bool emit)
      : terms_(terms), statistics_(statistics), atom_limit_(atom_limit), emit_(emit) {}
  // New backend: SAT identities/snapshots restart; semantic queue/budget survives.
  void begin_registry() { known_equalities_.clear(); }
  void register_atom(core::TermId atom);
  void request(core::TermId lhs, core::TermId rhs);
  bool pending() const { return !requests_.empty(); }
  bool ready() const { return ready_ && pending(); }
  void clause_finished() { ready_ = true; }
  // Called only after detaching callbacks. The optional bridge handles eligible
  // shared pairs; all other equalities use ordinary clausifier registration.
  void drain(BooleanClausifier& clausifier,
             const std::function<void(core::TermId, core::TermId)>& bridge = {});

 private:
  struct Request {
    core::TermId lhs, rhs;
    std::vector<std::pair<core::TermId, core::TermId>> arguments;
  };
  core::TermManager& terms_;
  util::Statistics& statistics_;
  std::size_t atom_limit_, fresh_atoms_ = 0;
  bool emit_, ready_ = false;
  std::unordered_set<std::uint64_t> known_equalities_;
  std::unordered_set<std::uint64_t> settled_;
  std::vector<Request> requests_;
};

// Bounded existing-atom subset of Dutertre/de Moura (2006), Section 2 p.3.
// Selection comes from conflict witnesses, not an all-pairs application scan.
class ConflictAckermannSelector {
 public:
  ConflictAckermannSelector(const core::TermManager& terms, util::Statistics& statistics,
                            bool detailed_diagnostics = true)
      : terms_(terms), statistics_(statistics), detailed_diagnostics_(detailed_diagnostics) {}
  void register_atom(core::TermId atom, sat::Lit literal);
  // Backend-owned queue survives execution rebuilds along with admission.
  void enqueue(sat::Clause clause);
  std::size_t pending_clauses() const { return pending_.size(); }
  sat::Clause take_clause();
  // Set only for a newly examined valid-shaped pair missing equality atoms;
  // cache/admission hits must not schedule redundant growth or SAT resumes.
  std::optional<sat::Clause> consider(core::TermId lhs, core::TermId rhs,
                                      bool* missing_atoms = nullptr);

 private:
  static std::uint64_t pair_key(core::TermId lhs, core::TermId rhs);
  const core::TermManager& terms_;
  util::Statistics& statistics_;
  std::unordered_map<std::uint64_t, sat::Lit> equalities_;
  std::deque<sat::Clause> pending_;
  void reject(std::uint64_t key);
  bool detailed_diagnostics_;
  std::unordered_set<std::uint64_t> admitted_;
  std::unordered_set<std::uint64_t> rejected_;
  std::deque<std::uint64_t> rejected_order_;
};

}  // namespace primo::smt
