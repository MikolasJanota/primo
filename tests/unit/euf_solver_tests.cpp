#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "theory/equality/euf_solver.hpp"
#include "theory/equality/explained_congruence_closure.hpp"
#include "theory/equality/flat_congruence_closure.hpp"
#include "theory/equality/flat_terms.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"
#include "theory/equality/proof_forest.hpp"

using primo::theory::equality::EufSolver;
using primo::theory::equality::ExplainedCongruenceClosure;
using primo::theory::equality::FlatCongruenceClosure;
using primo::theory::equality::FlatCongruenceClosureTestAccess;
using primo::theory::equality::FlatExplanationSearchMode;
using primo::theory::equality::FlatTermId;
using primo::theory::equality::FlatTermIndex;
using primo::theory::equality::IncrementalCongruenceClosure;
using primo::theory::equality::ProofForest;
using primo::theory::equality::ProofForestLabel;
using primo::theory::equality::ProofForestObligation;

namespace primo::theory::equality {
class FlatCongruenceClosureTestAccess {
 public:
  static std::size_t disequality_undo_entries(const FlatCongruenceClosure& closure) {
    return closure.size_vector_trail_.size();
  }
  static std::size_t trace_capacity(const FlatCongruenceClosure& closure) {
    return closure.conflict_trace_->current.steps.capacity();
  }
  static std::size_t representative_proof_storage(const FlatCongruenceClosure& closure) {
    return closure.proof_parent_.capacity() + closure.proof_parent_label_endpoint_.capacity() +
           closure.proof_parent_edge_.capacity();
  }

  static bool explain_pair_with_bound(FlatCongruenceClosure& closure, FlatTermId lhs,
                                      FlatTermId rhs, std::vector<primo::sat::Lit>& reason,
                                      std::size_t max_edge_index) {
    return closure.explain_equal_into(lhs, rhs, reason, max_edge_index);
  }

  static void begin_reason(FlatCongruenceClosure& closure, std::vector<primo::sat::Lit>& reason) {
    closure.begin_reason_collection(reason);
  }

  // The generation rebase is only reachable after 2^64 drains, so a test has
  // to put the counter there directly.
  static void set_changed_terms_generation(FlatCongruenceClosure& closure, std::uint64_t value) {
    closure.changed_terms_generation_ = value;
  }

  static std::uint64_t changed_terms_generation(const FlatCongruenceClosure& closure) {
    return closure.changed_terms_generation_;
  }
};
}  // namespace primo::theory::equality

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

class NaiveFlatOracle {
 public:
  explicit NaiveFlatOracle(std::size_t node_count) : parent_(node_count), size_(node_count, 1) {
    for (std::size_t i = 0; i < node_count; ++i) {
      parent_[i] = i;
    }
  }

  std::size_t push() {
    snapshots_.push_back(Snapshot{parent_, size_});
    return snapshots_.size() - 1;
  }

  void pop(std::size_t checkpoint) {
    const Snapshot snapshot = snapshots_.at(checkpoint);
    snapshots_.resize(checkpoint);
    parent_ = snapshot.parent;
    size_ = snapshot.size;
  }

  void add_definition(primo::theory::equality::FlatDefinition definition) {
    definitions_.push_back(definition);
    saturate();
  }

  void assert_equal(std::size_t lhs, std::size_t rhs) {
    merge(lhs, rhs);
    saturate();
  }

  bool are_equal(std::size_t lhs, std::size_t rhs) const { return find(lhs) == find(rhs); }

 private:
  struct Snapshot {
    std::vector<std::size_t> parent;
    std::vector<std::size_t> size;
  };

  std::size_t find(std::size_t value) const {
    while (parent_[value] != value) {
      value = parent_[value];
    }
    return value;
  }

  bool merge(std::size_t lhs, std::size_t rhs) {
    std::size_t lhs_repr = find(lhs);
    std::size_t rhs_repr = find(rhs);
    if (lhs_repr == rhs_repr) {
      return false;
    }
    if (size_[lhs_repr] > size_[rhs_repr]) {
      std::swap(lhs_repr, rhs_repr);
    }
    parent_[lhs_repr] = rhs_repr;
    size_[rhs_repr] += size_[lhs_repr];
    size_[lhs_repr] = 0;
    return true;
  }

  void saturate() {
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t i = 0; i < definitions_.size(); ++i) {
        for (std::size_t j = i + 1; j < definitions_.size(); ++j) {
          const auto& lhs = definitions_[i];
          const auto& rhs = definitions_[j];
          if (find(lhs.function) == find(rhs.function) &&
              find(lhs.argument) == find(rhs.argument)) {
            changed = merge(lhs.result, rhs.result) || changed;
          }
        }
      }
    }
  }

  std::vector<std::size_t> parent_;
  std::vector<std::size_t> size_;
  std::vector<primo::theory::equality::FlatDefinition> definitions_;
  std::vector<Snapshot> snapshots_;
};

bool explanation_entails_equality(
    std::size_t node_count, const std::vector<primo::theory::equality::FlatDefinition>& definitions,
    const std::vector<std::pair<std::size_t, std::size_t>>& labelled_equalities,
    const std::vector<primo::sat::Lit>& reason, std::size_t lhs, std::size_t rhs) {
  NaiveFlatOracle oracle(node_count);
  for (const auto& definition : definitions) {
    oracle.add_definition(definition);
  }
  for (const primo::sat::Lit lit : reason) {
    if (lit.value >= 0) {
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(-lit.value);
    if (index == 0 || index > labelled_equalities.size()) {
      continue;
    }
    const auto& equality = labelled_equalities[index - 1];
    oracle.assert_equal(equality.first, equality.second);
  }
  return oracle.are_equal(lhs, rhs);
}

void congruent_applications_are_merged() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  EufSolver solver(terms);
  solver.assert_equal(a_term, b_term);
  solver.assert_distinct(f_a, f_b);
  check(!solver.is_satisfiable());
}

void distinct_unrelated_terms_are_satisfiable() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);

  EufSolver solver(terms);
  solver.assert_distinct(a_term, b_term);
  check(solver.is_satisfiable());
}

void explained_closure_explains_explicit_chain() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);

  ExplainedCongruenceClosure closure(terms);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});
  closure.assert_equal(b_term, c_term, {primo::sat::Lit{-2}});
  const auto reason = closure.explain_equal(a_term, c_term);

  check(reason.has_value());
  check(reason->size() == 2);
}

void explained_closure_explains_congruence() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  ExplainedCongruenceClosure closure(terms);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});
  const auto reason = closure.explain_equal(f_a, f_b);

  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -1);
}

void incremental_closure_discovers_unary_congruence_with_lookup() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(f_a);
  closure.add_term(f_b);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});

  check(closure.are_equal(f_a, f_b));
  check(closure.stats().lookup_hits >= 1);
  check(closure.stats().congruence_merges >= 1);
}

void incremental_closure_discovers_binary_shared_subterm_congruence() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);
  const auto g_a_c = terms.mk_app(g, {a_term, c_term});
  const auto g_b_c = terms.mk_app(g, {b_term, c_term});

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(g_a_c);
  closure.add_term(g_b_c);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-2}});

  check(closure.are_equal(g_a_c, g_b_c));
  const auto reason = closure.explain_equal(g_a_c, g_b_c);
  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -2);
}

void incremental_closure_reports_disequality_conflict() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);

  IncrementalCongruenceClosure closure(terms);
  closure.assert_distinct(a_term, b_term, {primo::sat::Lit{-10}});
  check(!closure.conflict().has_value());
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-11}});

  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(conflict->size() == 2);
  check(closure.stats().disequality_checks >= 2);
  check(closure.stats().conflicts == 1);
  check(closure.stats().conflict_reason_max_length == 2);
  check(closure.stats().conflict_reason_length_le_4 == 1);
}

void incremental_closure_checks_only_inserted_disequality_until_a_merge() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);
  primo::core::TermManager terms(symbols);
  const auto lhs = terms.mk_constant(a);
  const auto rhs = terms.mk_constant(b);
  const auto f_lhs = terms.mk_app(f, {lhs});
  const auto f_rhs = terms.mk_app(f, {rhs});
  IncrementalCongruenceClosure closure(terms);
  for (std::size_t i = 0; i < 2048; ++i) {
    closure.assert_distinct(lhs, rhs, {primo::sat::Lit{-10}});
  }
  check(!closure.conflict());
  check(closure.stats().disequality_checks == 2048);
  const auto checkpoint = closure.push();
  closure.assert_equal(lhs, rhs, {primo::sat::Lit{-11}});
  check(closure.conflict().has_value());
  check(closure.conflict()->size() == 2);
  closure.pop(checkpoint);
  check(!closure.conflict());
  const auto next = closure.push();
  closure.assert_distinct(lhs, lhs, {primo::sat::Lit{-12}});
  check(closure.conflict().has_value());
  check(closure.conflict()->size() == 1);
  check(closure.conflict()->front().value == -12);
  closure.pop(next);
  check(!closure.conflict());

  IncrementalCongruenceClosure queued(terms);
  queued.assert_equal(lhs, rhs, {primo::sat::Lit{-21}});
  const auto before_terms = queued.push();
  // Endpoint registration processes congruence before appending the new pair.
  queued.assert_distinct(f_lhs, f_rhs, {primo::sat::Lit{-22}});
  check(queued.conflict().has_value());
  check(queued.conflict()->size() == 2);
  queued.pop(before_terms);
  check(!queued.conflict());
  queued.assert_distinct(f_lhs, f_lhs, {primo::sat::Lit{-23}});
  check(queued.conflict().has_value());
}

void incremental_closure_backtracks_and_rediscoveres_congruence() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(f_a);
  closure.add_term(f_b);
  const std::size_t checkpoint = closure.push();
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-3}});
  check(closure.are_equal(f_a, f_b));
  closure.pop(checkpoint);
  check(!closure.are_equal(f_a, f_b));
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-4}});
  check(closure.are_equal(f_a, f_b));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_pop_preserves_optional_proof_storage() {
  for (const FlatExplanationSearchMode mode :
       {FlatExplanationSearchMode::OneSidedBfs, FlatExplanationSearchMode::BidirectionalBfs,
        FlatExplanationSearchMode::ProofForest, FlatExplanationSearchMode::ReorientedProofForest,
        FlatExplanationSearchMode::StructuralProofForest}) {
    for (const bool diagnostics : {false, true}) {
      FlatCongruenceClosure closure(16, mode, diagnostics);
      const bool enabled = diagnostics || mode == FlatExplanationSearchMode::ProofForest;
      const auto capacity = FlatCongruenceClosureTestAccess::representative_proof_storage(closure);
      check((capacity != 0) == enabled);
      const auto outer = closure.push();
      closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
      const auto inner = closure.push();
      closure.ensure_node_count(32);
      closure.assert_equal(1, 31, {primo::sat::Lit{-2}});
      check(closure.are_equal(0, 31));
      closure.pop(inner);
      check(closure.are_equal(0, 1));
      if (!enabled) {
        check(FlatCongruenceClosureTestAccess::representative_proof_storage(closure) == 0);
      }
      closure.pop(outer);
      check(!closure.are_equal(0, 1));
      if (!enabled) {
        check(FlatCongruenceClosureTestAccess::representative_proof_storage(closure) == 0);
      }
    }
  }
}

void flat_congruence_closure_protects_borrowed_checkpoint() {
  FlatCongruenceClosure closure(2);
  const std::size_t borrowed = closure.push();
  closure.protect_checkpoint(borrowed);
  const std::size_t temporary = closure.push();
  closure.assert_equal(0, 1);
  closure.pop(temporary);

  bool rejected = false;
  try {
    closure.pop(borrowed);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  check(rejected);
  check(closure.checkpoint_depth() == borrowed + 1);

  closure.release_protected_checkpoint(borrowed);
  closure.pop(borrowed);
  check(closure.checkpoint_depth() == borrowed);
}

void flat_terms_curry_nary_applications_deterministically() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto g_ab = terms.mk_app(g, {a_term, b_term});

  FlatTermIndex flat(terms);
  const auto first = flat.flatten(g_ab);
  const auto second = flat.flatten(g_ab);

  check(first == second);
  check(flat.definitions().size() == 2);
  check(flat.existing_flat_term(g_ab).value() == first);
}

void flat_terms_share_nested_subterms_and_flatten_equalities() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);
  const auto h = symbols.declare_function("h", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto g_ab = terms.mk_app(g, {a_term, b_term});
  const auto h_shared = terms.mk_app(h, {g_ab, g_ab});
  const auto eq = terms.mk_eq(h_shared, g_ab);

  FlatTermIndex flat(terms);
  const auto flat_eq = flat.flatten_equality_atom(eq);

  check(flat_eq.has_value());
  check(flat_eq->lhs == flat.existing_flat_term(h_shared).value());
  check(flat_eq->rhs == flat.existing_flat_term(g_ab).value());
  check(flat.definitions().size() == 4);
}

void flat_congruence_closure_uses_curried_definitions() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);
  const auto g_a_c = terms.mk_app(g, {a_term, c_term});
  const auto g_b_c = terms.mk_app(g, {b_term, c_term});

  FlatTermIndex flat(terms);
  const auto flat_g_a_c = flat.flatten(g_a_c);
  const auto flat_g_b_c = flat.flatten(g_b_c);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-5}});

  check(closure.are_equal(flat_g_a_c, flat_g_b_c));
  check(closure.stats().lookup_hits >= 1);
  check(closure.stats().congruence_merges >= 1);
}

void flat_congruence_closure_drains_changed_terms_from_merges() {
  FlatCongruenceClosure closure(4);
  std::vector<primo::theory::equality::FlatTermId> changed;

  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.drain_changed_terms(changed);
  check(changed.size() == 1);
  check(changed.front() == 0 || changed.front() == 1);
  changed.clear();

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(2, 3, {primo::sat::Lit{-2}});
  closure.pop(checkpoint);
  closure.drain_changed_terms(changed);
  check(changed.empty());
}

void flat_congruence_closure_counts_selected_classes_across_backtracking() {
  FlatCongruenceClosure closure(5);
  const std::vector<FlatTermId> selected{0, 1, 2, 2};
  check(closure.class_count(selected) == 3);

  closure.assert_equal(0, 1);
  check(closure.class_count(selected) == 2);

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(1, 2);
  check(closure.class_count(selected) == 1);
  closure.pop(checkpoint);
  check(closure.class_count(selected) == 2);
}

void flat_congruence_closure_projects_selected_classes_deterministically() {
  FlatCongruenceClosure closure(7);
  closure.assert_equal(4, 1);
  closure.assert_equal(5, 2);
  closure.assert_equal(2, 6);

  const std::vector<FlatTermId> selected{6, 4, 0, 2, 1, 2, 5};
  const std::vector<std::vector<FlatTermId>> expected{{0}, {1, 4}, {2, 5, 6}};
  check(closure.equivalence_classes(selected) == expected);

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(1, 2);
  check((closure.equivalence_classes(selected) ==
         std::vector<std::vector<FlatTermId>>{{0}, {1, 2, 4, 5, 6}}));
  closure.pop(checkpoint);
  check(closure.equivalence_classes(selected) == expected);
}

void flat_congruence_closure_pop_does_not_restore_drained_changed_terms() {
  FlatCongruenceClosure closure(8);
  closure.assert_equal(5, 6);
  const std::size_t checkpoint = closure.push();

  std::vector<primo::theory::equality::FlatTermId> changed;
  closure.drain_changed_terms(changed);
  check(!changed.empty());

  changed.clear();
  closure.assert_equal(1, 2);
  closure.pop(checkpoint);
  closure.drain_changed_terms(changed);
  check(changed.empty());
}

// When the generation counter saturates, drain_changed_terms rebases it and
// re-stamps every open checkpoint so a later pop cannot resurrect a consumed
// prefix. The stamp must be a value the live counter can never take again; a
// stamp the very next drain reaches would let pop resize changed_terms_ to a
// stale, larger size and deliver default-constructed entries as changed terms.
void flat_congruence_closure_rebases_the_changed_term_generation_safely() {
  FlatCongruenceClosure closure(16);
  closure.assert_equal(0, 1);
  const std::size_t checkpoint = closure.push();
  closure.assert_equal(2, 3);

  primo::theory::equality::FlatCongruenceClosureTestAccess::set_changed_terms_generation(
      closure, std::numeric_limits<std::uint64_t>::max());

  std::vector<FlatTermId> changed;
  closure.drain_changed_terms(changed);
  // One notification per merge, as flat_congruence_closure_drain_consumes_
  // all_checkpoint_prefixes shows.
  check(changed.size() == 2);
  check(closure.stats().changed_term_generation_rebases == 1);

  // The drain that rebases is followed by an ordinary drain, which advances
  // the live counter onto whatever the checkpoints were stamped with.
  changed.clear();
  closure.assert_equal(4, 5);
  closure.drain_changed_terms(changed);
  check(changed.size() == 1);

  changed.clear();
  closure.pop(checkpoint);
  closure.drain_changed_terms(changed);
  check(changed.empty());
}

void flat_congruence_closure_preserves_undrained_parent_changes() {
  FlatCongruenceClosure closure(8);
  closure.assert_equal(0, 1);
  const std::size_t outer = closure.push();
  closure.assert_equal(2, 3);
  const std::size_t inner = closure.push();
  closure.assert_equal(4, 5);

  closure.pop(inner);
  closure.pop(outer);
  std::vector<FlatTermId> changed;
  closure.drain_changed_terms(changed);
  check(changed.size() == 1);
  check(changed.front() == 0 || changed.front() == 1);
}

void flat_congruence_closure_drain_consumes_all_checkpoint_prefixes() {
  FlatCongruenceClosure closure(16);
  closure.assert_equal(0, 1);
  const std::size_t outer = closure.push();
  closure.assert_equal(2, 3);
  const std::size_t inner = closure.push();
  closure.assert_equal(4, 5);

  std::vector<FlatTermId> changed;
  closure.drain_changed_terms(changed);
  check(changed.size() == 3);
  check(closure.stats().changed_term_drains == 1);
  check(closure.stats().changed_terms_delivered == 3);

  changed.clear();
  closure.assert_equal(6, 7);
  closure.pop(inner);
  closure.pop(outer);
  closure.drain_changed_terms(changed);
  check(changed.empty());
}

void flat_congruence_closure_adds_self_application_to_one_use_list() {
  FlatCongruenceClosure closure(6);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 0, 5});

  check(closure.stats().use_list_appends == 1);
  check(closure.stats().use_list_duplicate_skips == 1);
  check(closure.stats().max_use_list_size == 1);
}

void flat_congruence_closure_skips_unrelated_disequality_scans() {
  FlatCongruenceClosure closure(6);
  closure.assert_distinct(0, 1, {primo::sat::Lit{-1}});
  closure.assert_distinct(2, 3, {primo::sat::Lit{-2}});
  check(closure.stats().disequality_checks == 2);

  closure.assert_equal(4, 5, {primo::sat::Lit{-3}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 2);

  closure.assert_equal(0, 1, {primo::sat::Lit{-4}});
  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(closure.stats().disequality_checks == 3);
  check(closure.stats().conflicts == 1);
}

void flat_congruence_closure_scans_only_crossing_disequalities() {
  FlatCongruenceClosure closure(6);
  closure.assert_distinct(0, 2, {primo::sat::Lit{-1}});
  closure.assert_distinct(1, 3, {primo::sat::Lit{-2}});
  closure.assert_distinct(4, 5, {primo::sat::Lit{-3}});
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(0, 1, {primo::sat::Lit{-4}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(2, 3, {primo::sat::Lit{-5}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(0, 2, {primo::sat::Lit{-6}});
  check(closure.conflict().has_value());
  check(closure.stats().disequality_checks == 4);
  check(closure.stats().conflict_reason_length_le_4 == 1);
}

void flat_congruence_closure_reports_direct_conflict_before_use_list_scan() {
  FlatCongruenceClosure closure(8);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 2, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 3, 5});
  closure.add_definition(primo::theory::equality::FlatDefinition{1, 2, 6});
  closure.add_definition(primo::theory::equality::FlatDefinition{1, 3, 7});
  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(0, 1, {primo::sat::Lit{-1}});
  const std::size_t lookup_probes_before_conflict = closure.stats().lookup_probes;

  closure.assert_equal(0, 1, {primo::sat::Lit{-2}});

  check(closure.conflict().has_value());
  check(closure.stats().conflicts == 1);
  check(closure.stats().lookup_probes == lookup_probes_before_conflict);

  closure.pop(checkpoint);
  closure.assert_equal(0, 1, {primo::sat::Lit{-3}});
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_keeps_signed_reason_literals_distinct() {
  FlatCongruenceClosure closure(3);
  closure.assert_equal(0, 1, {primo::sat::Lit{1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-1}});

  const auto reason = closure.explain_equal(0, 2);
  check(reason.has_value());
  check(reason->size() == 2);
  check(closure.stats().explain_bfs_searches >= 1);
  check(closure.stats().explain_bfs_visited >= 1);
  check(closure.stats().explain_bfs_path_edges >= 1);
  check(closure.stats().explain_bfs_max_queue >= 1);

  bool has_positive = false;
  bool has_negative = false;
  for (const primo::sat::Lit lit : reason.value()) {
    has_positive = has_positive || lit.value == 1;
    has_negative = has_negative || lit.value == -1;
  }
  check(has_positive);
  check(has_negative);
}

void flat_congruence_closure_reuses_repeated_sub_explanations() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto h = symbols.declare_function("h", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto h_aa = terms.mk_app(h, {a_term, a_term});
  const auto h_bb = terms.mk_app(h, {b_term, b_term});

  FlatTermIndex flat(terms);
  const auto flat_h_aa = flat.flatten(h_aa);
  const auto flat_h_bb = flat.flatten(h_bb);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-8}});

  const auto reason = closure.explain_equal(flat_h_aa, flat_h_bb);
  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -8);
  check(closure.stats().explain_cache_hits >= 1);
  check(closure.stats().explain_top_level_queries >= 1);
  check(closure.stats().explain_recursive_queries >= 1);
  check(closure.stats().explain_repeated_pair_queries >= 1);
  check(closure.stats().explain_max_recursion_depth >= 1);
  check(closure.stats().explain_bfs_searches >= 1);
  check(closure.stats().explain_bfs_visited >= 1);
  check(closure.stats().explain_bfs_path_edges >= 1);
  check(closure.stats().explain_bfs_max_queue >= 1);
  check(closure.stats().explain_direct_edges >= 1);
  check(closure.stats().explain_congruence_edges >= 1);
}

void flat_congruence_closure_uses_causal_edges_for_congruence_reasons() {
  FlatCongruenceClosure closure(11);
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 0, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 2, 5});

  closure.assert_equal(0, 6, {primo::sat::Lit{-1}});
  closure.assert_equal(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(7, 2, {primo::sat::Lit{-3}});
  check(closure.are_equal(4, 5));

  closure.assert_equal(6, 4, {primo::sat::Lit{-4}});
  const auto reason = closure.explain_equal(4, 5);

  check(reason.has_value());
  bool uses_later_edge = false;
  for (const primo::sat::Lit lit : reason.value()) {
    uses_later_edge = uses_later_edge || lit.value == -4;
  }
  check(!uses_later_edge);
}

void flat_congruence_closure_bidirectional_uses_causal_edges_for_congruence_reasons() {
  FlatCongruenceClosure closure(11, FlatExplanationSearchMode::BidirectionalBfs);
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 0, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 2, 5});

  closure.assert_equal(0, 6, {primo::sat::Lit{-1}});
  closure.assert_equal(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(7, 2, {primo::sat::Lit{-3}});
  check(closure.are_equal(4, 5));

  closure.assert_equal(6, 4, {primo::sat::Lit{-4}});
  const auto reason = closure.explain_equal(4, 5);

  check(reason.has_value());
  bool uses_later_edge = false;
  for (const primo::sat::Lit lit : reason.value()) {
    uses_later_edge = uses_later_edge || lit.value == -4;
  }
  check(!uses_later_edge);
  check(closure.stats().explain_bfs_searches >= 1);
}

void flat_congruence_closure_explanation_cache_respects_edge_bounds() {
  FlatCongruenceClosure closure(3);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t bfs_after_broad_query = closure.stats().explain_bfs_searches;
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_cache_bound_misses >= 1);
  check(closure.stats().explain_bfs_searches > bfs_after_broad_query);
}

void flat_congruence_closure_bidirectional_explanation_cache_respects_edge_bounds() {
  FlatCongruenceClosure closure(3, FlatExplanationSearchMode::BidirectionalBfs);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t bfs_after_broad_query = closure.stats().explain_bfs_searches;
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_cache_bound_misses >= 1);
  check(closure.stats().explain_bfs_searches > bfs_after_broad_query);
}

void flat_congruence_closure_stops_at_chronological_explanation_bound() {
  FlatCongruenceClosure closure(4);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(0, 2, {primo::sat::Lit{-2}});
  closure.assert_equal(0, 3, {primo::sat::Lit{-3}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_causal_edge_skips == 1);
}

void flat_congruence_closure_bidirectional_backtracks_nested_merges() {
  FlatCongruenceClosure closure(4, FlatExplanationSearchMode::BidirectionalBfs);

  const std::size_t outer = closure.push();
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  check(closure.are_equal(0, 1));

  const std::size_t inner = closure.push();
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});
  check(closure.are_equal(0, 2));
  const auto reason = closure.explain_equal(0, 2);
  check(reason.has_value());
  check(closure.stats().explain_bfs_searches >= 1);

  closure.pop(inner);
  check(closure.are_equal(0, 1));
  check(!closure.are_equal(0, 2));

  closure.pop(outer);
  check(!closure.are_equal(0, 1));
  check(!closure.are_equal(1, 2));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_backtracks_and_rediscoveres_congruence() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  FlatTermIndex flat(terms);
  const auto flat_f_a = flat.flatten(f_a);
  const auto flat_f_b = flat.flatten(f_b);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-6}});
  check(closure.are_equal(flat_f_a, flat_f_b));
  closure.pop(checkpoint);
  check(!closure.are_equal(flat_f_a, flat_f_b));
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-7}});
  check(closure.are_equal(flat_f_a, flat_f_b));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_backtracks_nested_merges() {
  FlatCongruenceClosure closure(4);

  const std::size_t outer = closure.push();
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  check(closure.are_equal(0, 1));

  const std::size_t inner = closure.push();
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});
  check(closure.are_equal(0, 2));

  closure.pop(inner);
  check(closure.are_equal(0, 1));
  check(!closure.are_equal(0, 2));

  closure.pop(outer);
  check(!closure.are_equal(0, 1));
  check(!closure.are_equal(1, 2));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_batches_parent_restore_with_class_members() {
  FlatCongruenceClosure closure(6);
  closure.assert_equal(0, 1);
  closure.assert_equal(2, 3);

  const std::size_t checkpoint = closure.push();
  const std::size_t trail_before = closure.stats().trail_entries;
  closure.assert_equal(0, 2);
  check(closure.are_equal(0, 3));
  const std::size_t new_trail_entries = closure.stats().trail_entries - trail_before;
  check(new_trail_entries <= 8);

  closure.pop(checkpoint);
  check(closure.are_equal(0, 1));
  check(closure.are_equal(2, 3));
  check(!closure.are_equal(0, 2));
  check(!closure.are_equal(1, 3));
}

void flat_congruence_closure_proof_diagnostics_backtrack() {
  FlatCongruenceClosure closure(4, FlatExplanationSearchMode::OneSidedBfs, true);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  const std::size_t checkpoint = closure.push();
  closure.assert_equal(2, 3, {primo::sat::Lit{-2}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-3}});

  const auto before_pop = closure.explain_equal(0, 3);
  check(before_pop.has_value());
  check(closure.stats().explain_proof_forest_queries >= 1);
  check(closure.stats().explain_proof_forest_path_edges >= 1);
  check(closure.stats().explain_proof_forest_missing_lca == 0);
  check(closure.stats().explain_proof_forest_shorter_than_bfs +
            closure.stats().explain_proof_forest_equal_to_bfs +
            closure.stats().explain_proof_forest_longer_than_bfs >=
        1);

  closure.pop(checkpoint);
  check(!closure.are_equal(0, 3));
  const auto after_pop = closure.explain_equal(0, 1);
  check(after_pop.has_value());
  check(closure.stats().explain_proof_forest_queries >= 1);
  check(closure.stats().explain_proof_forest_missing_lca == 0);
}

void flat_congruence_closure_proof_diagnostics_respect_causal_bounds() {
  FlatCongruenceClosure closure(3, FlatExplanationSearchMode::OneSidedBfs, true);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t missing_lca_before = closure.stats().explain_proof_forest_missing_lca;

  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_proof_forest_missing_lca > missing_lca_before);
}

void flat_congruence_closure_backtracks_disequality_conflict() {
  FlatCongruenceClosure closure(2);

  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(0, 1, {primo::sat::Lit{-3}});
  closure.assert_equal(0, 1, {primo::sat::Lit{4}});
  check(closure.conflict().has_value());

  closure.pop(checkpoint);
  check(!closure.conflict().has_value());
  closure.assert_equal(0, 1, {primo::sat::Lit{5}});
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_conflict_reason_entails_nested_congruence() {
  constexpr std::size_t kNodeCount = 8;
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {4, 0, 2},
      {4, 1, 3},
      {5, 2, 6},
      {5, 3, 7},
  };
  FlatCongruenceClosure closure(8);
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
  }

  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});

  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(explanation_entails_equality(kNodeCount, definitions, {{0, 1}}, conflict.value(), 6, 7));
  bool has_disequality_literal = false;
  for (const primo::sat::Lit lit : conflict.value()) {
    has_disequality_literal = has_disequality_literal || lit.value == -2;
  }
  check(has_disequality_literal);

  closure.pop(checkpoint);
  check(!closure.conflict().has_value());
  check(!closure.are_equal(6, 7));
}

void flat_congruence_closure_backtracks_post_checkpoint_definitions() {
  FlatCongruenceClosure closure(4);

  const std::size_t checkpoint = closure.push();
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 3});
  check(closure.are_equal(2, 3));

  closure.pop(checkpoint);
  check(!closure.are_equal(2, 3));
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 3});
  check(closure.are_equal(2, 3));
}

void flat_congruence_closure_lookup_hit_definition_keeps_future_congruence() {
  FlatCongruenceClosure closure(8);

  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 5});
  check(closure.are_equal(4, 5));

  closure.add_definition(primo::theory::equality::FlatDefinition{2, 1, 6});
  check(!closure.are_equal(5, 6));

  closure.assert_equal(0, 2, {primo::sat::Lit{-1}});
  check(closure.are_equal(4, 6));
  check(closure.are_equal(5, 6));
  check(closure.stats().lookup_hits >= 2);
  check(closure.stats().congruence_merges >= 2);
}

void flat_congruence_closure_proof_forest_explains_nonrepresentative_merge_endpoint() {
  constexpr std::size_t kNodeCount = 5;
  FlatCongruenceClosure closure(kNodeCount, FlatExplanationSearchMode::ProofForest);
  std::vector<std::pair<std::size_t, std::size_t>> labelled_equalities;
  auto add_equal = [&](std::size_t lhs, std::size_t rhs) {
    labelled_equalities.emplace_back(lhs, rhs);
    closure.assert_equal(lhs, rhs,
                         {primo::sat::Lit{-static_cast<int>(labelled_equalities.size())}});
  };

  add_equal(0, 1);
  add_equal(2, 3);
  add_equal(3, 4);
  add_equal(0, 2);

  const auto reason = closure.explain_equal(1, 2);
  check(reason.has_value());
  check(explanation_entails_equality(kNodeCount, {}, labelled_equalities, reason.value(), 1, 2));
  bool has_first_merge = false;
  bool has_bridge_merge = false;
  for (const primo::sat::Lit lit : reason.value()) {
    has_first_merge = has_first_merge || lit.value == -1;
    has_bridge_merge = has_bridge_merge || lit.value == -4;
  }
  check(has_first_merge);
  check(has_bridge_merge);
  check(closure.stats().explain_proof_forest_queries >= 1);
  check(closure.stats().explain_proof_forest_path_edges >= 1);
}

void flat_congruence_closure_proof_forest_conflict_reason_entails_nested_congruence() {
  constexpr std::size_t kNodeCount = 8;
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {4, 0, 2},
      {4, 1, 3},
      {5, 2, 6},
      {5, 3, 7},
  };
  FlatCongruenceClosure closure(kNodeCount, FlatExplanationSearchMode::ProofForest);
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
  }

  closure.assert_distinct(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});

  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(explanation_entails_equality(kNodeCount, definitions, {{0, 1}}, conflict.value(), 6, 7));
}

void flat_congruence_closure_rejects_conflicting_result_definitions() {
  FlatCongruenceClosure closure(5);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  const std::size_t definitions_after_first = closure.stats().flat_definitions;

  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  check(closure.stats().flat_definitions == definitions_after_first);

  bool rejected = false;
  try {
    closure.add_definition(primo::theory::equality::FlatDefinition{3, 4, 2});
  } catch (const std::logic_error&) {
    rejected = true;
  }
  check(rejected);
}

void flat_congruence_closure_deduplicates_affected_use_lists_after_backtrack() {
  FlatCongruenceClosure closure(8);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  const std::size_t checkpoint = closure.push();

  closure.assert_equal(0, 1);
  check(closure.stats().use_list_appends >= 3);
  closure.assert_equal(3, 4);
  closure.assert_equal(3, 5);
  closure.assert_equal(1, 3);
  check(closure.stats().affected_use_list_dedup >= 1);
  check(!closure.conflict().has_value());

  closure.pop(checkpoint);
  check(!closure.are_equal(0, 1));
  closure.assert_equal(0, 1);
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_matches_naive_oracle_under_backtracking() {
  constexpr std::size_t kNodeCount = 30;
  FlatCongruenceClosure closure(kNodeCount);
  NaiveFlatOracle oracle(kNodeCount);
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {6, 0, 9},   {6, 1, 10},  {6, 2, 11},  {6, 3, 12},  {6, 4, 13},  {6, 5, 14},  {7, 0, 15},
      {7, 1, 16},  {7, 2, 17},  {7, 3, 18},  {7, 4, 19},  {7, 5, 20},  {8, 9, 21},  {8, 10, 22},
      {8, 11, 23}, {8, 12, 24}, {8, 13, 25}, {8, 14, 26}, {8, 15, 27}, {8, 16, 28}, {8, 17, 29},
  };
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
    oracle.add_definition(definition);
  }

  std::vector<std::size_t> closure_checkpoints;
  std::vector<std::size_t> oracle_checkpoints;
  std::size_t state = 0x9e3779b97f4a7c15ULL;
  auto next = [&state]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
  };
  auto compare_all_pairs = [&]() {
    for (std::size_t lhs = 0; lhs < kNodeCount; ++lhs) {
      for (std::size_t rhs = 0; rhs < kNodeCount; ++rhs) {
        check(closure.are_equal(lhs, rhs) == oracle.are_equal(lhs, rhs));
      }
    }
  };

  compare_all_pairs();
  for (std::size_t step = 0; step < 220; ++step) {
    const std::size_t choice = next() % 11;
    if (choice == 0 && closure_checkpoints.size() < 8) {
      closure_checkpoints.push_back(closure.push());
      oracle_checkpoints.push_back(oracle.push());
    } else if (choice == 1 && !closure_checkpoints.empty()) {
      const std::size_t index = next() % closure_checkpoints.size();
      closure.pop(closure_checkpoints[index]);
      oracle.pop(oracle_checkpoints[index]);
      closure_checkpoints.resize(index);
      oracle_checkpoints.resize(index);
    } else {
      const std::size_t lhs = next() % 18;
      const std::size_t rhs = next() % 18;
      closure.assert_equal(lhs, rhs, {primo::sat::Lit{-static_cast<int>(step + 1)}});
      oracle.assert_equal(lhs, rhs);
    }
    compare_all_pairs();
  }
}

void flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
    FlatExplanationSearchMode explanation_mode) {
  constexpr std::size_t kNodeCount = 30;
  FlatCongruenceClosure closure(kNodeCount, explanation_mode);
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {6, 0, 9},   {6, 1, 10},  {6, 2, 11},  {6, 3, 12},  {6, 4, 13},  {6, 5, 14},  {7, 0, 15},
      {7, 1, 16},  {7, 2, 17},  {7, 3, 18},  {7, 4, 19},  {7, 5, 20},  {8, 9, 21},  {8, 10, 22},
      {8, 11, 23}, {8, 12, 24}, {8, 13, 25}, {8, 14, 26}, {8, 15, 27}, {8, 16, 28}, {8, 17, 29},
  };
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
  }

  struct Snapshot {
    std::vector<std::pair<std::size_t, std::size_t>> labelled_equalities;
  };
  std::vector<std::size_t> closure_checkpoints;
  std::vector<Snapshot> snapshots;
  std::vector<std::pair<std::size_t, std::size_t>> labelled_equalities;
  std::size_t state = 0x517cc1b727220a95ULL;
  auto next = [&state]() {
    state = state * 2862933555777941757ULL + 3037000493ULL;
    return state;
  };
  auto check_explanations = [&]() {
    for (std::size_t lhs = 9; lhs < kNodeCount; ++lhs) {
      for (std::size_t rhs = lhs + 1; rhs < kNodeCount; ++rhs) {
        const bool equal = closure.are_equal(lhs, rhs);
        const std::optional<std::vector<primo::sat::Lit>> reason = closure.explain_equal(lhs, rhs);
        check(reason.has_value() == equal);
        if (reason.has_value()) {
          check(explanation_entails_equality(kNodeCount, definitions, labelled_equalities,
                                             reason.value(), lhs, rhs));
        }
      }
    }
  };

  for (std::size_t step = 0; step < 180; ++step) {
    const std::size_t choice = next() % 10;
    if (choice == 0 && closure_checkpoints.size() < 8) {
      closure_checkpoints.push_back(closure.push());
      snapshots.push_back(Snapshot{labelled_equalities});
    } else if (choice == 1 && !closure_checkpoints.empty()) {
      const std::size_t index = next() % closure_checkpoints.size();
      closure.pop(closure_checkpoints[index]);
      labelled_equalities = snapshots[index].labelled_equalities;
      closure_checkpoints.resize(index);
      snapshots.resize(index);
    } else {
      const std::size_t lhs = next() % 18;
      const std::size_t rhs = next() % 18;
      labelled_equalities.emplace_back(lhs, rhs);
      closure.assert_equal(lhs, rhs,
                           {primo::sat::Lit{-static_cast<int>(labelled_equalities.size())}});
    }
    check_explanations();
  }
}

void flat_congruence_closure_explanations_match_naive_oracle_under_backtracking() {
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
      FlatExplanationSearchMode::OneSidedBfs);
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
      FlatExplanationSearchMode::BidirectionalBfs);
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
      FlatExplanationSearchMode::ProofForest);
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
      FlatExplanationSearchMode::ReorientedProofForest);
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking(
      FlatExplanationSearchMode::StructuralProofForest);
}

ProofForestLabel proof_forest_direct_reason(std::size_t reason) {
  ProofForestLabel label;
  label.reason = reason;
  return label;
}

// Hand-derived from Nieuwenhuis and Oliveras Section 4's Union procedure:
// "reverse all edges on the path between e and the root of its tree, [then]
// add an edge e -> e'." Sequence: 0=1 (r10), 2=0 (r11), 1=3 (r12), 0=4 (r13).
// Union now additionally rebalances by real tree size (see union_nodes's
// comment -- required for Lemma 13's amortized bound once structural-class
// coalescing can make live class size diverge from real tree size), so
// which physical node actually gets its path reversed at each step is no
// longer exactly the naive "e is whatever the caller passed" reading of
// this sequence: with all nodes starting at equal size, rebalancing prefers
// reversing whichever side is currently smallest, which is not always the
// side named `e` above -- and since a tree edge's label proves its two
// endpoints equal regardless of which physical node carries it, there is no
// way to observe through Explain's output alone whether a given step
// reversed the "expected" side or the rebalanced one; both are equally
// sound. So this test pins down the one thing that must hold regardless of
// which internal shape rebalancing picks: Explain(2, 3) is sound and
// includes exactly the reasons that actually justify it, not r13 (0=4),
// which is irrelevant to 2=3.
void proof_forest_reverses_path_on_non_root_union() {
  ProofForest forest(5);
  forest.union_nodes(0, 1, proof_forest_direct_reason(10));
  forest.union_nodes(2, 0, proof_forest_direct_reason(11));
  forest.union_nodes(1, 3, proof_forest_direct_reason(12));
  forest.union_nodes(0, 4, proof_forest_direct_reason(13));

  const std::optional<std::vector<std::size_t>> result = forest.explain(2, 3);
  check(result.has_value());
  std::vector<std::size_t> reasons = result.value();
  std::sort(reasons.begin(), reasons.end());
  check((reasons == std::vector<std::size_t>{10, 11, 12}));
}

// Section 5.2: a congruence-step edge is labelled with a pair of child
// obligations that Explain resolves recursively within the same call
// (the paper's PendingProofs loop), not by the caller.
void proof_forest_recursively_explains_congruence_obligations() {
  ProofForest forest(6);
  // Function-argument equalities that justify the congruence step.
  forest.union_nodes(0, 1, proof_forest_direct_reason(20));  // fn1 = fn2
  forest.union_nodes(2, 3, proof_forest_direct_reason(21));  // arg1 = arg2
  ProofForestLabel congruence;
  congruence.congruence = std::make_pair(ProofForestObligation{0, 1}, ProofForestObligation{2, 3});
  forest.union_nodes(4, 5, std::move(congruence));  // f(fn1,arg1) = f(fn2,arg2)

  const std::optional<std::vector<std::size_t>> result = forest.explain(4, 5);
  check(result.has_value());
  std::vector<std::size_t> reasons = result.value();
  std::sort(reasons.begin(), reasons.end());
  check((reasons == std::vector<std::size_t>{20, 21}));
}

void proof_forest_backtracks_union() {
  ProofForest forest(3);
  const std::size_t checkpoint = forest.push();
  forest.union_nodes(0, 1, proof_forest_direct_reason(30));
  check(forest.explain(0, 1).has_value());

  forest.pop(checkpoint);
  check(!forest.explain(0, 1).has_value());

  // The forest must still be usable (and correctly rooted) after rollback.
  forest.union_nodes(1, 2, proof_forest_direct_reason(31));
  const std::optional<std::vector<std::size_t>> result = forest.explain(1, 2);
  check(result.has_value());
  check((result.value() == std::vector<std::size_t>{31}));
  check(!forest.explain(0, 1).has_value());
}

void proof_forest_explain_returns_nullopt_when_unconnected() {
  ProofForest forest(4);
  forest.union_nodes(0, 1, proof_forest_direct_reason(40));
  check(!forest.explain(0, 2).has_value());
  check(!forest.explain(2, 3).has_value());
}

void proof_forest_explain_same_node_is_trivial() {
  ProofForest forest(2);
  const std::optional<std::vector<std::size_t>> result = forest.explain(0, 0);
  check(result.has_value());
  check(result.value().empty());
}

// f(x,x)-shaped congruence bridge: both obligation slots equal, matching
// how Nieuwenhuis and Oliveras Examples 22-23 apply a single-argument
// function to the same argument twice.
ProofForestLabel proof_forest_congruence_label(FlatTermId member_arg, FlatTermId canonical_arg) {
  ProofForestLabel label;
  const ProofForestObligation obligation{member_arg, canonical_arg};
  label.congruence = std::make_pair(obligation, obligation);
  return label;
}

// Hand-derived from Section 5.5's Example 22: input equations a1=b1, a1=c1,
// f(a1,a1)=a, f(b1,b1)=b, f(c1,c1)=c. The paper: "the proof forest will
// consist of the two trees ... [a, b, c] and b1 -> a1 <- c1" -- a, b, c
// collapse into a single structural-class node (no edge among them), and
// "Explain(a=c) will return only the structural equations involving a and c
// and the corresponding recursive explanation that a1=c1."
// Nodes: a1=0, b1=1, c1=2, a=3 (canonical), b=4, c=5.
void proof_forest_structural_class_coalesces_congruence_members() {
  ProofForest forest(6);
  forest.union_nodes(1, 0, proof_forest_direct_reason(200));  // a1=b1: parent(b1)=a1
  forest.union_nodes(2, 0, proof_forest_direct_reason(201));  // a1=c1: parent(c1)=a1
  forest.coalesce_into_structural_class(4, 3, proof_forest_congruence_label(1, 0));  // b joins [a]
  forest.coalesce_into_structural_class(5, 3, proof_forest_congruence_label(2, 0));  // c joins [a]

  check(forest.resolve(3) == 3);
  check(forest.resolve(4) == 3);
  check(forest.resolve(5) == 3);

  // Explain(a, c): both resolve to the same node, so no tree walk at all is
  // needed -- only c's bridge to the shared canonical, i.e. a1=c1.
  const std::optional<std::vector<std::size_t>> a_c = forest.explain(3, 5);
  check(a_c.has_value());
  std::vector<std::size_t> a_c_reasons = a_c.value();
  std::sort(a_c_reasons.begin(), a_c_reasons.end());
  a_c_reasons.erase(std::unique(a_c_reasons.begin(), a_c_reasons.end()), a_c_reasons.end());
  check((a_c_reasons == std::vector<std::size_t>{201}));

  // Explain(b, c): neither is canonical; both bridges fire (a1=b1, a1=c1).
  const std::optional<std::vector<std::size_t>> b_c = forest.explain(4, 5);
  check(b_c.has_value());
  std::vector<std::size_t> b_c_reasons = b_c.value();
  std::sort(b_c_reasons.begin(), b_c_reasons.end());
  b_c_reasons.erase(std::unique(b_c_reasons.begin(), b_c_reasons.end()), b_c_reasons.end());
  check((b_c_reasons == std::vector<std::size_t>{200, 201}));
}

// Hand-derived from Section 5.5's Example 23 (the redundancy example),
// restricted to the five structural (non-definitional) input equations:
// 5. a1=b1  6. c1=d1  7. a1=c  8. a1=a  9. d=d1
// with structural classes [a,b] (canonical a) and [c,d] (canonical c)
// formed as a side effect of equations 5 and 6. The paper's own final tree
// is "[a, b] -> a1 <- [c, d] <- d1 <- c1" with b1 also parented at a1;
// equation 9 ("d=d1") is a direct equation whose own endpoint d is a
// non-canonical structural-class member, exercising resolve()-before-Union.
// Nodes: a1=0, b1=1, c1=2, d1=3, a=4 (canonical [a,b]), b=5,
//        c=6 (canonical [c,d]), d=7.
void proof_forest_structural_class_resolves_direct_equation_endpoint() {
  ProofForest forest(8);
  forest.union_nodes(1, 0, proof_forest_direct_reason(105));  // eq5 a1=b1: parent(b1)=a1
  forest.coalesce_into_structural_class(5, 4, proof_forest_congruence_label(1, 0));  // b joins [a]
  forest.union_nodes(2, 3, proof_forest_direct_reason(106));  // eq6 c1=d1: parent(c1)=d1
  forest.coalesce_into_structural_class(7, 6, proof_forest_congruence_label(3, 2));  // d joins [c]
  forest.union_nodes(forest.resolve(6), forest.resolve(0),
                     proof_forest_direct_reason(107));  // eq7 a1=c: parent(c)=a1
  forest.union_nodes(forest.resolve(4), forest.resolve(0),
                     proof_forest_direct_reason(108));  // eq8 a1=a: parent(a)=a1
  // eq9 "d=d1": d (7) is a non-canonical member of [c,d] -- resolve before
  // Union, exactly as FlatCongruenceClosure's merge_classes now does for
  // direct-equation endpoints in structural-proof-forest mode.
  forest.union_nodes(forest.resolve(3), forest.resolve(7),
                     proof_forest_direct_reason(109));  // parent(d1)=c

  check(forest.resolve(5) == 4);
  check(forest.resolve(7) == 6);

  const std::optional<std::vector<std::size_t>> result = forest.explain(5, 3);  // Explain(b, d1)
  check(result.has_value());
  std::vector<std::size_t> reasons = result.value();
  std::sort(reasons.begin(), reasons.end());
  reasons.erase(std::unique(reasons.begin(), reasons.end()), reasons.end());
  // 106 (c1=d1) is not needed to connect b to d1 here -- it only bridges
  // d's own coalescing, which this path never traverses; 108 (a1=a)
  // appearing is exactly the paper's point that structural-class forests
  // can still be redundant (Theorem 24), not a bug in this construction.
  check((reasons == std::vector<std::size_t>{105, 107, 108, 109}));
}

void proof_forest_structural_class_coalescing_backtracks() {
  ProofForest forest(3);
  const std::size_t checkpoint = forest.push();
  forest.coalesce_into_structural_class(0, 1, proof_forest_congruence_label(2, 2));
  check(forest.resolve(0) == 1);
  check(forest.explain(0, 1).has_value());

  forest.pop(checkpoint);
  check(forest.resolve(0) == 0);
  check(!forest.explain(0, 1).has_value());
}

}  // namespace

void conflict_congruence_trace_selects_only_explained_steps() {
  for (auto mode :
       {FlatExplanationSearchMode::OneSidedBfs, FlatExplanationSearchMode::BidirectionalBfs,
        FlatExplanationSearchMode::ProofForest}) {
    FlatCongruenceClosure closure(8, mode);
    closure.enable_conflict_congruence_trace();
    closure.add_definition({0, 1, 3});
    closure.add_definition({0, 2, 4});
    closure.add_definition({5, 1, 6});
    closure.add_definition({5, 2, 7});
    const auto checkpoint = closure.push();
    closure.assert_equal(1, 2, {{1}});
    check(closure.explain_equal(6, 7).has_value());
    check(closure.conflict_congruence_trace() == nullptr);
    closure.assert_distinct(3, 4, {{-2}});
    const auto* trace = closure.conflict_congruence_trace();
    check(trace != nullptr && trace->visited_steps == 1 && trace->steps.size() == 1);
    check((trace->steps[0].lhs == 3 && trace->steps[0].rhs == 4) ||
          (trace->steps[0].lhs == 4 && trace->steps[0].rhs == 3));
    const auto nested = closure.push();
    closure.pop(nested);
    check(closure.conflict_congruence_trace()->steps.size() == 1);
    closure.pop(checkpoint);
    check(!closure.conflict() && closure.conflict_congruence_trace() == nullptr);
    closure.assert_distinct(3, 4, {{-3}});
    closure.assert_equal(1, 2, {{4}});
    check(closure.conflict_congruence_trace()->steps.size() == 1);
  }
  FlatCongruenceClosure limited(5);
  limited.enable_conflict_congruence_trace(0);
  limited.add_definition({0, 1, 3});
  limited.add_definition({0, 2, 4});
  limited.assert_equal(1, 2, {{1}});
  limited.assert_distinct(3, 4, {{-2}});
  check(limited.conflict()->size() == 2);
  check(limited.conflict_congruence_trace()->visited_steps == 1);
  check(limited.conflict_congruence_trace()->steps.empty());
  for (auto mode : {FlatExplanationSearchMode::ReorientedProofForest,
                    FlatExplanationSearchMode::StructuralProofForest}) {
    FlatCongruenceClosure unsupported(0, mode);
    bool rejected = false;
    try {
      unsupported.enable_conflict_congruence_trace();
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    check(rejected);
  }
}

void flat_terms_do_not_export_curry_prefixes() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort, sort}, sort);
  primo::core::TermManager terms(symbols);
  const auto ta = terms.mk_constant(a);
  const auto tb = terms.mk_constant(b);
  const auto fab = terms.mk_app(f, {ta, tb});
  const auto faa = terms.mk_app(f, {ta, ta});
  FlatTermIndex flat(terms);
  const auto full = flat.flatten(fab);
  flat.flatten(faa);
  check(flat.original_term(full).value() == fab);
  check(!flat.original_term(flat.definitions().front().result));
  check(!flat.original_term(flat.definitions().front().function));
  check(flat.original_term(flat.existing_flat_term(ta).value()).value() == ta);
}

void empty_losing_disequalities_preserve_undo_and_conflicts() {
  FlatCongruenceClosure closure(160);
  for (std::size_t i = 64; i < 128; ++i) closure.assert_distinct(0, i, {{-1}});
  auto root = closure.push();
  const auto before = FlatCongruenceClosureTestAccess::disequality_undo_entries(closure);
  for (std::size_t i = 1; i < 64; ++i) closure.assert_equal(i, 0, {{2}});
  check(FlatCongruenceClosureTestAccess::disequality_undo_entries(closure) == before);
  auto nested = closure.push();
  closure.assert_equal(1, 64, {{3}});
  check(closure.conflict().has_value());
  closure.pop(nested);
  check(!closure.conflict().has_value() && closure.are_equal(1, 0));
  closure.pop(root);
  check(!closure.are_equal(1, 0));
  closure.assert_equal(0, 65, {{4}});
  check(closure.conflict().has_value());
}

void conflict_trace_reuses_storage_after_rollback() {
  FlatCongruenceClosure closure(5);
  closure.enable_conflict_congruence_trace();
  closure.add_definition({0, 1, 3});
  closure.add_definition({0, 2, 4});
  std::size_t capacity = 0;
  for (int round = 0; round < 20; ++round) {
    auto checkpoint = closure.push();
    closure.assert_equal(1, 2, {{1}});
    closure.assert_distinct(3, 4, {{-2}});
    check(closure.conflict().has_value());
    if (round == 0) capacity = FlatCongruenceClosureTestAccess::trace_capacity(closure);
    check(capacity > 0 && FlatCongruenceClosureTestAccess::trace_capacity(closure) == capacity);
    closure.pop(checkpoint);
    check(FlatCongruenceClosureTestAccess::trace_capacity(closure) == capacity);
  }
}

int main() {
  empty_losing_disequalities_preserve_undo_and_conflicts();
  conflict_trace_reuses_storage_after_rollback();
  conflict_congruence_trace_selects_only_explained_steps();
  flat_terms_do_not_export_curry_prefixes();
  congruent_applications_are_merged();
  distinct_unrelated_terms_are_satisfiable();
  explained_closure_explains_explicit_chain();
  explained_closure_explains_congruence();
  incremental_closure_discovers_unary_congruence_with_lookup();
  incremental_closure_discovers_binary_shared_subterm_congruence();
  incremental_closure_reports_disequality_conflict();
  incremental_closure_checks_only_inserted_disequality_until_a_merge();
  incremental_closure_backtracks_and_rediscoveres_congruence();
  flat_congruence_closure_pop_preserves_optional_proof_storage();
  flat_congruence_closure_protects_borrowed_checkpoint();
  flat_terms_curry_nary_applications_deterministically();
  flat_terms_share_nested_subterms_and_flatten_equalities();
  flat_congruence_closure_uses_curried_definitions();
  flat_congruence_closure_drains_changed_terms_from_merges();
  flat_congruence_closure_counts_selected_classes_across_backtracking();
  flat_congruence_closure_projects_selected_classes_deterministically();
  flat_congruence_closure_pop_does_not_restore_drained_changed_terms();
  flat_congruence_closure_rebases_the_changed_term_generation_safely();
  flat_congruence_closure_preserves_undrained_parent_changes();
  flat_congruence_closure_drain_consumes_all_checkpoint_prefixes();
  flat_congruence_closure_adds_self_application_to_one_use_list();
  flat_congruence_closure_skips_unrelated_disequality_scans();
  flat_congruence_closure_scans_only_crossing_disequalities();
  flat_congruence_closure_reports_direct_conflict_before_use_list_scan();
  flat_congruence_closure_keeps_signed_reason_literals_distinct();
  flat_congruence_closure_reuses_repeated_sub_explanations();
  flat_congruence_closure_uses_causal_edges_for_congruence_reasons();
  flat_congruence_closure_bidirectional_uses_causal_edges_for_congruence_reasons();
  flat_congruence_closure_explanation_cache_respects_edge_bounds();
  flat_congruence_closure_bidirectional_explanation_cache_respects_edge_bounds();
  flat_congruence_closure_stops_at_chronological_explanation_bound();
  flat_congruence_closure_bidirectional_backtracks_nested_merges();
  flat_congruence_closure_backtracks_and_rediscoveres_congruence();
  flat_congruence_closure_backtracks_nested_merges();
  flat_congruence_closure_batches_parent_restore_with_class_members();
  flat_congruence_closure_proof_diagnostics_backtrack();
  flat_congruence_closure_proof_diagnostics_respect_causal_bounds();
  flat_congruence_closure_backtracks_disequality_conflict();
  flat_congruence_closure_conflict_reason_entails_nested_congruence();
  flat_congruence_closure_backtracks_post_checkpoint_definitions();
  flat_congruence_closure_lookup_hit_definition_keeps_future_congruence();
  flat_congruence_closure_proof_forest_explains_nonrepresentative_merge_endpoint();
  flat_congruence_closure_proof_forest_conflict_reason_entails_nested_congruence();
  flat_congruence_closure_rejects_conflicting_result_definitions();
  flat_congruence_closure_deduplicates_affected_use_lists_after_backtrack();
  flat_congruence_closure_matches_naive_oracle_under_backtracking();
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking();
  proof_forest_reverses_path_on_non_root_union();
  proof_forest_recursively_explains_congruence_obligations();
  proof_forest_backtracks_union();
  proof_forest_explain_returns_nullopt_when_unconnected();
  proof_forest_explain_same_node_is_trivial();
  proof_forest_structural_class_coalesces_congruence_members();
  proof_forest_structural_class_resolves_direct_equation_endpoint();
  proof_forest_structural_class_coalescing_backtracks();
  return 0;
}
