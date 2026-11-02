#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "core/ids.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::sat {
class IpasirUpBackend;
}

namespace primo::smt {

// Boolean abstraction and Tseitin clausification for one SAT backend. The SAT
// layer sees only Lit/Clause values; theory atoms retain the TermId mapping in
// this SMT-owned object.
class BooleanClausifier {
 public:
  struct TheoryAtom {
    sat::Lit lit;
    core::TermId term;
    bool occurs_positive = false;
    bool occurs_negative = false;
    bool theory_relevant_at_both_polarities = false;
  };

  // Called synchronously for every emitted clause before backend insertion.
  // The observer must copy retained data and must not mutate the clausifier,
  // term manager, or backend, or reenter clausification.
  using ClauseObserver = std::function<void(const sat::Clause&)>;
  BooleanClausifier(sat::IpasirUpBackend& backend, core::TermManager& terms,
                    bool top_level_or_tseitin, ClauseObserver observer = {});
  ~BooleanClausifier();

  BooleanClausifier(const BooleanClausifier&) = delete;
  BooleanClausifier& operator=(const BooleanClausifier&) = delete;

  void assert_formula(core::TermId term);
  // Full unconditional definitions, with only the root guarded by selector.
  void assert_guarded_formula(core::TermId term, sat::Lit selector);
  const TheoryAtom& ensure_theory_atom(core::TermId term);
  const TheoryAtom& ensure_generated_theory_atom_with_lit(core::TermId term, sat::Lit lit);
  const std::vector<TheoryAtom>& theory_atoms() const;
  // Scan only new atom roots, including atoms appended by registration itself.
  std::size_t register_boolean_argument_terms();
  // Cumulative completed roots; subtract successive values for per-query work.
  std::size_t boolean_argument_scan_count() const;
  std::size_t boolean_argument_registration_count() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace primo::smt
