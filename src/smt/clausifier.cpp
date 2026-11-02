#include "smt/clausifier.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/clause.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "smt/solver_error.hpp"

namespace primo::smt {

struct BooleanClausifier::Implementation {
  using TheoryAtom = BooleanClausifier::TheoryAtom;

  Implementation(sat::IpasirUpBackend& backend, core::TermManager& terms, bool top_level_or_tseitin,
                 BooleanClausifier::ClauseObserver observer)
      : backend_(backend),
        terms_(terms),
        top_level_or_tseitin_(top_level_or_tseitin),
        observer_(std::move(observer)),
        lit_by_term_(terms.size()),
        theory_atom_by_term_(terms.size(), kNoTheoryAtom),
        polarity_seen_by_term_(terms.size(), 0) {}

  void assert_formula(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("assertion is not Boolean");
    }
    collect_theory_atom_polarities(term, true);
    assert_positive(term);
  }

  const TheoryAtom& ensure_theory_atom(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("generated theory atom is not Boolean");
    }
    return theory_atom_for_term(term);
  }

  void assert_guarded_formula(core::TermId term, sat::Lit selector) {
    if (selector.value == 0 || terms_.get(term).sort != terms_.symbols().bool_sort())
      throw UnsupportedInput("invalid guarded assertion");
    // Conservative Tseitin extensions survive pop; the scope constrains only
    // their root. See docs/assumption-incremental-lra.md.
    collect_theory_atom_polarities(term, true);
    add_clause({negate(selector), encode(term)});
  }

  const TheoryAtom& ensure_generated_theory_atom_with_lit(core::TermId term, sat::Lit lit) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("generated theory atom is not Boolean");
    }
    if (lit.value == 0) {
      throw UnsupportedInput("generated theory atom literal is invalid");
    }
    ensure_term_index(term);
    const std::size_t existing = theory_atom_by_term_[term];
    if (existing != kNoTheoryAtom) {
      if (theory_atoms_[existing].lit.value != lit.value) {
        throw UnsupportedInput("generated theory atom literal mismatch");
      }
      return theory_atoms_[existing];
    }
    if (lit_by_term_[term].value != 0 && lit_by_term_[term].value != lit.value) {
      throw UnsupportedInput("generated theory atom literal mismatch");
    }
    lit_by_term_[term] = lit;
    theory_atom_by_term_[term] = theory_atoms_.size();
    theory_atoms_.push_back(TheoryAtom{lit, term, false, false});
    return theory_atoms_.back();
  }

  const std::vector<TheoryAtom>& theory_atoms() const { return theory_atoms_; }

  // Registers Bool-sorted terms that occur as ARGUMENTS of uninterpreted
  // applications as theory atoms. Registration is all that is needed: once a
  // term is an atom, `add_explained_theory_equality` and
  // `flatten_assigned_atom` already assert `t = true` / `t = false` into the
  // closure with the term's own SAT literal as the reason.
  //
  // This is the cardinality axiom for Bool. Congruence closure is complete
  // only for sorts whose domain may be as large as it needs; Bool has exactly
  // two elements, so without this it may place an argument in a third class
  // and report sat for an unsatisfiable formula -- see
  // bugs/qf-uf-boolean-argument-congruence.
  //
  // Reuses the argument's literal, or allocates its first one if not encoded
  // yet. It creates no separate equality variable or linking clause.
  //
  // Deliberately NOT registered:
  //   * Bool-VALUED applications -- already theory atoms, and their branch in
  //     add_explained_theory_equality already links them to the constants.
  //   * operands of Bool equality/disequality -- lowered to iff/XOR before
  //     Boolean abstraction, so the closure never sees them as equalities.
  //   * true/false themselves -- they ARE the canonical constants.
  //
  // Compound arguments need no special case: the frontend replaces
  // `(f (and p q))` with a fresh Bool Constant plus a propositional
  // equivalence, and that constant is what sits in argument position.
  std::size_t register_boolean_argument_terms() {
    const std::size_t before = registered_boolean_argument_terms_.size();
    std::unordered_set<core::TermId> visited;
    // The registry is append-only and terms are immutable. Completed roots need
    // no rescan after pop or polarity changes (docs/assumption-incremental-uf.md).
    // Registration may grow theory_atoms_; snapshot IDs, re-read size(), and
    // advance only after completing the root. Keep DAG scratch local to this call.
    for (; boolean_argument_scan_count_ < theory_atoms_.size(); ++boolean_argument_scan_count_) {
      std::vector<core::TermId> pending{theory_atoms_[boolean_argument_scan_count_].term};
      while (!pending.empty()) {
        const core::TermId term_id = pending.back();
        pending.pop_back();
        if (!visited.insert(term_id).second) {
          continue;
        }
        const core::TermKind kind = terms_.get(term_id).kind;
        if (kind == core::TermKind::Forall || kind == core::TermKind::Exists) continue;
        const std::vector<core::TermId> children = terms_.get(term_id).children;
        if (kind == core::TermKind::Application) {
          for (const core::TermId child : children) {
            register_boolean_argument_term(child);
          }
        }
        pending.insert(pending.end(), children.begin(), children.end());
      }
    }
    return registered_boolean_argument_terms_.size() - before;
  }

  std::size_t boolean_argument_registration_count() const {
    return registered_boolean_argument_terms_.size();
  }

  std::size_t boolean_argument_scan_count() const { return boolean_argument_scan_count_; }

 private:
  void add_clause(const sat::Clause& clause) {
    if (observer_) observer_(clause);
    backend_.add_clause(clause);
  }

  void register_boolean_argument_term(core::TermId term_id) {
    const core::Term& term = terms_.get(term_id);
    if (term.sort != terms_.symbols().bool_sort()) {
      return;
    }
    // Only nullary Bool constants: applications are already atoms, and
    // BoolValue terms are the canonical true/false the others are linked to.
    if (term.kind != core::TermKind::Constant) {
      return;
    }
    if (!registered_boolean_argument_terms_.insert(term_id).second) {
      return;
    }
    // May return an atom that already exists with polarity flags set from the
    // formula structure; the flag has to be set either way.
    theory_atom_for_term(term_id).theory_relevant_at_both_polarities = true;
  }

  void collect_theory_atom_polarities(core::TermId term_id, bool positive) {
    struct Pending {
      core::TermId term;
      bool positive;
    };

    std::vector<Pending> pending{{term_id, positive}};
    while (!pending.empty()) {
      const Pending current = pending.back();
      pending.pop_back();
      ensure_term_index(current.term);
      const std::uint8_t polarity_bit =
          current.positive ? kPositivePolaritySeen : kNegativePolaritySeen;
      if ((polarity_seen_by_term_[current.term] & polarity_bit) != 0) {
        continue;
      }
      polarity_seen_by_term_[current.term] |= polarity_bit;
      const core::Term& term = terms_.get(current.term);
      switch (term.kind) {
        case core::TermKind::BoolValue:
          break;
        case core::TermKind::And:
        case core::TermKind::Or:
          for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
            pending.push_back(Pending{*child, current.positive});
          }
          break;
        case core::TermKind::Implies:
          pending.push_back(Pending{term.children.at(1), current.positive});
          pending.push_back(Pending{term.children.at(0), !current.positive});
          break;
        case core::TermKind::Not:
          pending.push_back(Pending{term.children.at(0), !current.positive});
          break;
        case core::TermKind::Constant:
          if (term.sort == terms_.symbols().bool_sort()) {
            record_theory_atom_polarity(current.term, current.positive);
          }
          break;
        case core::TermKind::Application:
          if (term.sort == terms_.symbols().bool_sort()) {
            record_theory_atom_polarity(current.term, current.positive);
          }
          break;
        case core::TermKind::Equality:
        case core::TermKind::Le:
        // Lowered by the preprocessing leaf before this runs.
        case core::TermKind::Ite:
        case core::TermKind::IntDiv:
        case core::TermKind::IntMod:
        case core::TermKind::Abs:
        case core::TermKind::Lt:
        // A quantified formula is an opaque atom owned by the quantifier
        // component; its body is never part of the Boolean structure.
        case core::TermKind::Forall:
        case core::TermKind::Exists:
          record_theory_atom_polarity(current.term, current.positive);
          break;
        case core::TermKind::Rational:
        case core::TermKind::Plus:
        case core::TermKind::Times:
          throw UnsupportedInput("Real-sorted term used in a Boolean context");
        case core::TermKind::BoundVariable:
        case core::TermKind::Pattern:
          throw UnsupportedInput("bound variable or pattern used in a Boolean context");
      }
    }
  }

  void record_theory_atom_polarity(core::TermId term_id, bool positive) {
    TheoryAtom& atom = theory_atom_for_term(term_id);
    if (positive) {
      atom.occurs_positive = true;
    } else {
      atom.occurs_negative = true;
    }
  }

  TheoryAtom& theory_atom_for_term(core::TermId term_id) {
    ensure_term_index(term_id);
    const std::size_t existing = theory_atom_by_term_[term_id];
    if (existing != kNoTheoryAtom) {
      return theory_atoms_[existing];
    }
    const sat::Lit lit = encode_theory_atom_lit(term_id);
    theory_atom_by_term_[term_id] = theory_atoms_.size();
    theory_atoms_.push_back(TheoryAtom{lit, term_id, false, false});
    return theory_atoms_.back();
  }

  void assert_positive(core::TermId term_id) {
    std::vector<core::TermId> pending{term_id};
    std::unordered_set<core::TermId> seen;
    while (!pending.empty()) {
      const core::TermId current = pending.back();
      pending.pop_back();
      if (!seen.insert(current).second) {
        continue;
      }
      const core::Term& term = terms_.get(current);
      switch (term.kind) {
        case core::TermKind::BoolValue:
          if (!term.bool_value) {
            add_clause(sat::Clause{});
          }
          break;
        case core::TermKind::And:
          for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
            pending.push_back(*child);
          }
          break;
        case core::TermKind::Or: {
          if (top_level_or_tseitin_) {
            add_clause(sat::Clause{encode(current)});
            break;
          }
          sat::Clause clause;
          clause.reserve(term.children.size());
          bool tautology = false;
          for (const core::TermId child : term.children) {
            const core::Term& child_term = terms_.get(child);
            if (child_term.kind == core::TermKind::BoolValue) {
              if (child_term.bool_value) {
                tautology = true;
                break;
              }
              continue;
            }
            clause.push_back(encode(child));
          }
          if (!tautology) {
            add_clause(clause);
          }
          break;
        }
        case core::TermKind::Implies:
          add_clause(sat::Clause{negate(encode(term.children.at(0))), encode(term.children.at(1))});
          break;
        case core::TermKind::Not:
        case core::TermKind::Constant:
        case core::TermKind::Application:
        case core::TermKind::Equality:
        case core::TermKind::Le:
        // Lowered by the preprocessing leaf before this runs.
        case core::TermKind::Ite:
        case core::TermKind::IntDiv:
        case core::TermKind::IntMod:
        case core::TermKind::Abs:
        case core::TermKind::Lt:
        case core::TermKind::Forall:
        case core::TermKind::Exists:
          add_clause(sat::Clause{encode(current)});
          break;
        case core::TermKind::Rational:
        case core::TermKind::Plus:
        case core::TermKind::Times:
        case core::TermKind::BoundVariable:
        case core::TermKind::Pattern:
          throw UnsupportedInput("unsupported Boolean assertion");
      }
    }
  }

  sat::Lit encode(core::TermId term_id) {
    struct Visit {
      core::TermId term;
      bool expanded;
    };

    std::vector<Visit> pending{{term_id, false}};
    while (!pending.empty()) {
      const Visit visit = pending.back();
      pending.pop_back();
      ensure_term_index(visit.term);
      const core::Term& term = terms_.get(visit.term);

      if (visit.expanded) {
        const std::vector<core::TermId> children = term.children;
        switch (term.kind) {
          case core::TermKind::Not:
            lit_by_term_[visit.term] = negate(encoded_lit(children.at(0)));
            break;
          case core::TermKind::And:
            encode_and(lit_by_term_[visit.term], children);
            break;
          case core::TermKind::Or:
            encode_or(lit_by_term_[visit.term], children);
            break;
          case core::TermKind::Implies:
            encode_implies(lit_by_term_[visit.term], encoded_lit(children.at(0)),
                           encoded_lit(children.at(1)));
            break;
          case core::TermKind::BoolValue:
          case core::TermKind::Constant:
          case core::TermKind::Application:
          case core::TermKind::Equality:
          case core::TermKind::Rational:
          case core::TermKind::Plus:
          case core::TermKind::Times:
          case core::TermKind::Le:
          // Lowered by the preprocessing leaf before this runs.
          case core::TermKind::Ite:
          case core::TermKind::IntDiv:
          case core::TermKind::IntMod:
          case core::TermKind::Abs:
          case core::TermKind::Lt:
          case core::TermKind::BoundVariable:
          case core::TermKind::Forall:
          case core::TermKind::Exists:
          case core::TermKind::Pattern:
            assert(false && "atom reached Boolean clausifier post-order encoding");
            break;
        }
        continue;
      }

      if (lit_by_term_[visit.term].value != 0) {
        continue;
      }
      if (term.kind == core::TermKind::BoolValue) {
        (void)true_lit();
        continue;
      }
      if (term.kind == core::TermKind::Constant) {
        (void)atom_lit(visit.term);
        continue;
      }
      if (term.kind == core::TermKind::Not) {
        pending.push_back(Visit{visit.term, true});
        pending.push_back(Visit{term.children.at(0), false});
        continue;
      }

      lit_by_term_[visit.term] = sat::Lit{backend_.new_var()};
      switch (term.kind) {
        case core::TermKind::And:
        case core::TermKind::Or:
        case core::TermKind::Implies:
          pending.push_back(Visit{visit.term, true});
          for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
            pending.push_back(Visit{*child, false});
          }
          break;
        case core::TermKind::Application:
          if (term.sort != terms_.symbols().bool_sort()) {
            throw UnsupportedInput("non-Boolean function application in Boolean context");
          }
          (void)theory_atom_for_term(visit.term);
          break;
        case core::TermKind::Equality:
        case core::TermKind::Le:
        // Lowered by the preprocessing leaf before this runs.
        case core::TermKind::Ite:
        case core::TermKind::IntDiv:
        case core::TermKind::IntMod:
        case core::TermKind::Abs:
        case core::TermKind::Lt:
        case core::TermKind::Forall:
        case core::TermKind::Exists:
          (void)theory_atom_for_term(visit.term);
          break;
        case core::TermKind::BoolValue:
        case core::TermKind::Constant:
        case core::TermKind::Not:
        case core::TermKind::Rational:
        case core::TermKind::Plus:
        case core::TermKind::Times:
        case core::TermKind::BoundVariable:
        case core::TermKind::Pattern:
          throw UnsupportedInput("unsupported Boolean term");
      }
    }
    return encoded_lit(term_id);
  }

  sat::Lit atom_lit(core::TermId term_id) {
    ensure_term_index(term_id);
    if (lit_by_term_[term_id].value != 0) {
      return lit_by_term_[term_id];
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_[term_id] = lit;
    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::Constant && term.sort == terms_.symbols().bool_sort()) {
      return theory_atom_for_term(term_id).lit;
    }
    return lit;
  }

  sat::Lit encode_theory_atom_lit(core::TermId term_id) {
    ensure_term_index(term_id);
    if (lit_by_term_[term_id].value != 0) {
      return lit_by_term_[term_id];
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_[term_id] = lit;
    return lit;
  }

  sat::Lit true_lit() {
    if (true_lit_.value == 0) {
      true_lit_ = sat::Lit{backend_.new_var()};
      add_clause(sat::Clause{true_lit_});
    }
    return true_lit_;
  }

  static sat::Lit negate(sat::Lit lit) { return sat::Lit{-lit.value}; }

  void ensure_term_index(core::TermId term_id) {
    if (term_id >= lit_by_term_.size()) {
      lit_by_term_.resize(term_id + 1);
      theory_atom_by_term_.resize(term_id + 1, kNoTheoryAtom);
      polarity_seen_by_term_.resize(term_id + 1, 0);
    }
  }

  void encode_and(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause reverse_clause{output};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encoded_lit(child);
      add_clause(sat::Clause{negate(output), child_lit});
      reverse_clause.push_back(negate(child_lit));
    }
    add_clause(reverse_clause);
  }

  void encode_or(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause forward_clause{negate(output)};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encoded_lit(child);
      add_clause(sat::Clause{negate(child_lit), output});
      forward_clause.push_back(child_lit);
    }
    add_clause(forward_clause);
  }

  void encode_implies(sat::Lit output, sat::Lit lhs, sat::Lit rhs) {
    add_clause(sat::Clause{lhs, output});
    add_clause(sat::Clause{negate(rhs), output});
    add_clause(sat::Clause{negate(output), negate(lhs), rhs});
  }

  sat::Lit encoded_lit(core::TermId term_id) {
    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::BoolValue) {
      return term.bool_value ? true_lit() : negate(true_lit());
    }
    assert(term_id < lit_by_term_.size());
    assert(lit_by_term_[term_id].value != 0);
    return lit_by_term_[term_id];
  }

  sat::IpasirUpBackend& backend_;
  core::TermManager& terms_;
  bool top_level_or_tseitin_ = true;
  BooleanClausifier::ClauseObserver observer_;
  static constexpr std::size_t kNoTheoryAtom = static_cast<std::size_t>(-1);
  static constexpr std::uint8_t kPositivePolaritySeen = 1;
  static constexpr std::uint8_t kNegativePolaritySeen = 2;
  std::vector<sat::Lit> lit_by_term_;
  std::vector<std::size_t> theory_atom_by_term_;
  std::vector<std::uint8_t> polarity_seen_by_term_;
  std::vector<TheoryAtom> theory_atoms_;
  std::size_t boolean_argument_scan_count_ = 0;
  std::unordered_set<core::TermId> registered_boolean_argument_terms_;
  sat::Lit true_lit_{0};
};

BooleanClausifier::BooleanClausifier(sat::IpasirUpBackend& backend, core::TermManager& terms,
                                     bool top_level_or_tseitin, ClauseObserver observer)
    : implementation_(std::make_unique<Implementation>(backend, terms, top_level_or_tseitin,
                                                       std::move(observer))) {}

BooleanClausifier::~BooleanClausifier() = default;

void BooleanClausifier::assert_guarded_formula(core::TermId term, sat::Lit selector) {
  implementation_->assert_guarded_formula(term, selector);
}

void BooleanClausifier::assert_formula(core::TermId term) { implementation_->assert_formula(term); }

const BooleanClausifier::TheoryAtom& BooleanClausifier::ensure_theory_atom(core::TermId term) {
  return implementation_->ensure_theory_atom(term);
}

const BooleanClausifier::TheoryAtom& BooleanClausifier::ensure_generated_theory_atom_with_lit(
    core::TermId term, sat::Lit lit) {
  return implementation_->ensure_generated_theory_atom_with_lit(term, lit);
}

const std::vector<BooleanClausifier::TheoryAtom>& BooleanClausifier::theory_atoms() const {
  return implementation_->theory_atoms();
}

std::size_t BooleanClausifier::register_boolean_argument_terms() {
  return implementation_->register_boolean_argument_terms();
}

std::size_t BooleanClausifier::boolean_argument_registration_count() const {
  return implementation_->boolean_argument_registration_count();
}

std::size_t BooleanClausifier::boolean_argument_scan_count() const {
  return implementation_->boolean_argument_scan_count();
}

}  // namespace primo::smt
