#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/preprocessing.hpp"
#include "util/rational.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

struct Fixture {
  primo::core::SymbolTable symbols;
  primo::core::SortId sort = symbols.declare_sort("U");
  primo::core::SymbolId a = symbols.declare_constant("a", sort);
  primo::core::SymbolId b = symbols.declare_constant("b", sort);
  primo::core::SymbolId c = symbols.declare_constant("c", sort);
  primo::core::SymbolId x = symbols.declare_constant("x", sort);
  primo::core::SymbolId y = symbols.declare_constant("y", sort);
  primo::core::SymbolId z = symbols.declare_constant("z", sort);
  primo::core::SymbolId f = symbols.declare_function("f", {sort}, sort);
  primo::core::SymbolId p = symbols.declare_predicate("P", {sort});
  primo::core::TermManager terms{symbols};

  primo::core::TermId constant(primo::core::SymbolId symbol) { return terms.mk_constant(symbol); }

  primo::core::TermId app(primo::core::SymbolId symbol, primo::core::TermId arg) {
    return terms.mk_app(symbol, {arg});
  }

  primo::core::TermId pred(primo::core::SymbolId symbol, primo::core::TermId arg) {
    return terms.mk_app(symbol, {arg});
  }

  primo::core::TermId neq(primo::core::TermId lhs, primo::core::TermId rhs) {
    return terms.mk_not(terms.mk_eq(lhs, rhs));
  }

  primo::core::TermId choices(primo::core::TermId term,
                              const std::vector<primo::core::TermId>& values) {
    std::vector<primo::core::TermId> equalities;
    for (const primo::core::TermId value : values) {
      equalities.push_back(terms.mk_eq(term, value));
    }
    return terms.mk_or(std::move(equalities));
  }

  std::vector<primo::core::TermId> value_disequalities() {
    const primo::core::TermId a_term = constant(a);
    const primo::core::TermId b_term = constant(b);
    const primo::core::TermId c_term = constant(c);
    return {neq(a_term, b_term), neq(a_term, c_term), neq(b_term, c_term)};
  }
};

struct RealFixture {
  primo::core::SymbolTable symbols;
  primo::core::SortId real_sort = symbols.real_sort();
  primo::core::SymbolId x = symbols.declare_constant("x", real_sort);
  primo::core::SymbolId y = symbols.declare_constant("y", real_sort);
  primo::core::SymbolId z = symbols.declare_constant("z", real_sort);
  primo::core::TermManager terms{symbols};

  primo::core::TermId constant(primo::core::SymbolId symbol) { return terms.mk_constant(symbol); }
  primo::core::TermId rational(std::int64_t value) { return terms.mk_rational(value); }
};

void arithmetic_atom_canonicalization_merges_scaled_aliases() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const auto scaled_atom = [&](std::int64_t factor) {
    return fixture.terms.mk_le(fixture.terms.mk_plus({fixture.terms.mk_times(factor, x),
                                                      fixture.terms.mk_times(factor, y)}),
                               fixture.terms.mk_times(factor, z));
  };
  const primo::core::TermId twice = scaled_atom(2);
  const primo::core::TermId four_times = scaled_atom(4);

  const primo::smt::ArithmeticAtomCanonicalizationResult result =
      primo::smt::canonicalize_arithmetic_atoms(fixture.terms, {twice, four_times});
  check(result.atoms_visited == 2);
  check(result.atoms_rewritten == 2);
  check(result.comparisons == 2);
  check(result.equalities == 0);
  check(result.aliases_merged == 1);
  check(result.duplicate_assertions_removed == 1);
  check(result.assertions.size() == 1);

  const primo::smt::ArithmeticAtomCanonicalizationResult original_duplicates =
      primo::smt::canonicalize_arithmetic_atoms(fixture.terms, {twice, twice});
  check(original_duplicates.assertions.size() == 2);
  check(original_duplicates.duplicate_assertions_removed == 0);

  const primo::smt::ArithmeticAtomCanonicalizationResult repeated =
      primo::smt::canonicalize_arithmetic_atoms(fixture.terms, result.assertions);
  check(repeated.assertions == result.assertions);
  check(repeated.atoms_rewritten == 0);
}

void arithmetic_atom_canonicalization_handles_sign_strictness_and_rationals() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId negative =
      fixture.terms.mk_le(fixture.terms.mk_times(-2, x), fixture.terms.mk_times(-2, y));
  const primo::core::TermId rational =
      fixture.terms.mk_lt(fixture.terms.mk_times(primo::util::Rational(2, 3), x),
                          fixture.terms.mk_times(primo::util::Rational(4, 5), y));

  const primo::smt::ArithmeticAtomCanonicalizationResult result =
      primo::smt::canonicalize_arithmetic_atoms(fixture.terms, {negative, rational});
  check(result.orientation_reversals == 1);
  check(result.assertions.at(0) == fixture.terms.mk_le(y, x));
  check(result.assertions.at(1) ==
        fixture.terms.mk_lt(fixture.terms.mk_times(5, x), fixture.terms.mk_times(6, y)));
}

void arithmetic_atom_canonicalization_orients_equalities_and_folds_constants() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId lhs =
      fixture.terms.mk_plus({fixture.terms.mk_times(2, x), fixture.terms.mk_times(2, y)});
  const primo::core::TermId rhs = fixture.terms.mk_rational(6);
  const primo::core::TermId equality = fixture.terms.mk_eq(lhs, rhs);
  const primo::core::TermId swapped = fixture.terms.mk_eq(rhs, lhs);
  const primo::core::TermId cancellation = fixture.terms.mk_le(
      fixture.terms.mk_plus({x, fixture.terms.mk_times(-1, x)}), fixture.terms.mk_rational(0));

  const primo::smt::ArithmeticAtomCanonicalizationResult result =
      primo::smt::canonicalize_arithmetic_atoms(fixture.terms, {equality, swapped, cancellation});
  check(result.equalities == 2);
  check(result.constant_folds == 1);
  check(result.aliases_merged == 1);
  check(result.duplicate_assertions_removed == 1);
  check(result.assertions.size() == 2);
  check(result.assertions.front() ==
        fixture.terms.mk_eq(fixture.terms.mk_plus({x, y}), fixture.terms.mk_rational(3)));
  check(result.assertions.back() == fixture.terms.mk_bool(true));
}

void arithmetic_atom_canonicalization_rewrites_nested_atoms_and_preserves_euf_equalities() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId scaled =
      fixture.terms.mk_le(fixture.terms.mk_times(3, x), fixture.terms.mk_times(3, y));
  const primo::core::TermId nested = fixture.terms.mk_not(fixture.terms.mk_not(scaled));
  const primo::core::TermId real_equality =
      fixture.terms.mk_eq(fixture.terms.mk_times(2, x), fixture.terms.mk_times(2, y));

  const primo::smt::ArithmeticAtomCanonicalizationResult comparisons_only =
      primo::smt::canonicalize_arithmetic_atoms(
          fixture.terms, {nested, real_equality},
          primo::smt::ArithmeticAtomCanonicalizationScope::ComparisonsOnly);
  check(comparisons_only.assertions.at(0) == fixture.terms.mk_le(x, y));
  check(comparisons_only.assertions.at(1) == real_equality);
  check(comparisons_only.comparisons == 1);
  check(comparisons_only.equalities == 0);
}

void bounded_boolean_flattening_preserves_threshold_and_handles_deep_dags() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId pb = fixture.pred(fixture.p, b);
  const primo::core::TermId pc = fixture.pred(fixture.p, c);
  const primo::core::TermId inner = fixture.terms.mk_and({pa, pb});
  const primo::core::TermId outer = fixture.terms.mk_and({inner, pc});

  const primo::smt::BooleanFlatteningResult flattened =
      primo::smt::flatten_associative_booleans(fixture.terms, {outer}, 3);
  check(flattened.attempts == 1);
  check(flattened.accepted == 1);
  check(flattened.threshold_stops == 0);
  check(flattened.operands_copied == 2);
  check(flattened.rewrites == 1);
  const primo::core::Term& flat_root = fixture.terms.get(flattened.assertions.front());
  check(flat_root.kind == primo::core::TermKind::And);
  check(flat_root.children == std::vector<primo::core::TermId>({pa, pb, pc}));
  check(flattened.max_arity == 3);

  const primo::smt::BooleanFlatteningResult bounded =
      primo::smt::flatten_associative_booleans(fixture.terms, {outer}, 2);
  check(bounded.attempts == 1);
  check(bounded.accepted == 0);
  check(bounded.threshold_stops == 1);
  check(bounded.rewrites == 0);
  check(bounded.assertions.front() == outer);

  primo::core::TermId chain = pa;
  for (std::size_t index = 0; index < 20000; ++index) {
    const primo::core::SymbolId symbol = fixture.symbols.declare_constant(
        "deep_bool_" + std::to_string(index), fixture.symbols.bool_sort());
    chain = fixture.terms.mk_and({chain, fixture.terms.mk_constant(symbol)});
  }
  const primo::smt::BooleanFlatteningResult deep =
      primo::smt::flatten_associative_booleans(fixture.terms, {chain}, 4);
  check(deep.assertions.size() == 1);
  check(deep.max_arity <= 4);
  check(deep.accepted > 0);
  check(deep.threshold_stops > 0);
}

void implicit_conjunction_analysis_ignores_assertion_packaging() {
  Fixture fixture;
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const primo::core::TermId pc = fixture.pred(fixture.p, fixture.constant(fixture.c));
  const primo::core::TermId shared = fixture.terms.mk_and({pa, pb});
  const primo::core::TermId nested = fixture.terms.mk_and({shared, pc});

  const primo::smt::ImplicitConjunctionAnalysis separate =
      primo::smt::analyze_implicit_conjunction(fixture.terms, {pa, pb, pc});
  const primo::smt::ImplicitConjunctionAnalysis packaged =
      primo::smt::analyze_implicit_conjunction(fixture.terms, {nested});
  check(separate.conjunct_occurrences == 3);
  check(packaged.conjunct_occurrences == separate.conjunct_occurrences);
  check(packaged.unique_conjuncts == separate.unique_conjuncts);
  check(packaged.clause_like_conjunct_occurrences == 3);
  check(packaged.all_clause_like());

  const primo::smt::ImplicitConjunctionAnalysis repeated =
      primo::smt::analyze_implicit_conjunction(fixture.terms, {nested, shared});
  check(repeated.conjunct_occurrences == 5);
  check(repeated.unique_conjuncts == 3);
  check(repeated.clause_like_conjunct_occurrences == 5);
}

void implicit_conjunction_analysis_recognizes_clause_implications() {
  Fixture fixture;
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const primo::core::TermId pc = fixture.pred(fixture.p, fixture.constant(fixture.c));
  const primo::core::TermId clause = fixture.terms.mk_implies(pa, fixture.terms.mk_or({pb, pc}));
  const primo::core::TermId compound = fixture.terms.mk_implies(fixture.terms.mk_and({pa, pb}), pc);

  const primo::smt::ImplicitConjunctionAnalysis candidate =
      primo::smt::analyze_implicit_conjunction(fixture.terms, {clause});
  check(candidate.conjunct_occurrences == 1);
  check(candidate.clause_like_conjunct_occurrences == 1);
  check(candidate.implication_clause_candidates == 1);
  check(candidate.all_clause_like());

  const primo::smt::ImplicitConjunctionAnalysis rejected =
      primo::smt::analyze_implicit_conjunction(fixture.terms, {compound});
  check(rejected.clause_like_conjunct_occurrences == 0);
  check(rejected.implication_clause_candidates == 0);
  check(!rejected.all_clause_like());
}

void implication_clause_rewrite_flattens_bounded_literal_consequents() {
  Fixture fixture;
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const primo::core::TermId pc = fixture.pred(fixture.p, fixture.constant(fixture.c));
  const primo::core::TermId implication =
      fixture.terms.mk_implies(pa, fixture.terms.mk_or({pb, pc}));

  const primo::smt::BooleanImpliesRewriteResult result = primo::smt::rewrite_boolean_implications(
      fixture.terms, {implication}, primo::smt::BooleanImpliesRewriteMode::Clause, 3);
  check(result.visited == 1);
  check(result.clause_candidates == 1);
  check(result.rewritten == 1);
  check(result.widened_operands == 2);
  check(result.threshold_stops == 0);
  const primo::core::Term& rewritten = fixture.terms.get(result.assertions.front());
  check(rewritten.kind == primo::core::TermKind::Or);
  check(rewritten.children == std::vector<primo::core::TermId>({fixture.terms.mk_not(pa), pb, pc}));

  const primo::smt::BooleanImpliesRewriteResult bounded = primo::smt::rewrite_boolean_implications(
      fixture.terms, {implication}, primo::smt::BooleanImpliesRewriteMode::Clause, 2);
  check(bounded.rewritten == 0);
  check(bounded.threshold_stops == 1);
  check(bounded.assertions.front() == implication);

  const primo::smt::BooleanImpliesRewriteResult disabled = primo::smt::rewrite_boolean_implications(
      fixture.terms, {implication}, primo::smt::BooleanImpliesRewriteMode::Off, 3);
  check(disabled.visited == 0);
  check(disabled.assertions.front() == implication);
}

void implication_rewrite_preserves_compound_clause_mode_and_shared_dags() {
  Fixture fixture;
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const primo::core::TermId pc = fixture.pred(fixture.p, fixture.constant(fixture.c));
  const primo::core::TermId implication =
      fixture.terms.mk_implies(fixture.terms.mk_and({pa, pb}), pc);
  const primo::core::TermId shared = fixture.terms.mk_and({implication, fixture.terms.mk_not(pc)});

  const primo::smt::BooleanImpliesRewriteResult clause = primo::smt::rewrite_boolean_implications(
      fixture.terms, {shared, implication}, primo::smt::BooleanImpliesRewriteMode::Clause, 8);
  check(clause.visited == 1);
  check(clause.clause_candidates == 0);
  check(clause.rewritten == 0);
  check(clause.assertions.at(1) == implication);

  const primo::smt::BooleanImpliesRewriteResult all = primo::smt::rewrite_boolean_implications(
      fixture.terms, {implication}, primo::smt::BooleanImpliesRewriteMode::All, 0);
  check(all.visited == 1);
  check(all.rewritten == 1);
  check(all.all_mode_rewrites == 1);
  const primo::core::Term& rewritten = fixture.terms.get(all.assertions.front());
  check(rewritten.kind == primo::core::TermKind::Or);
  check(rewritten.children.size() == 2);
  check(rewritten.children.at(0) == fixture.terms.mk_not(fixture.terms.mk_and({pa, pb})));
  check(rewritten.children.at(1) == pc);
}

void implication_rewrite_enters_boolean_equality_but_not_uf_application_arguments() {
  primo::core::SymbolTable symbols;
  const primo::core::SortId bool_sort = symbols.bool_sort();
  const primo::core::SymbolId p = symbols.declare_constant("p", bool_sort);
  const primo::core::SymbolId q = symbols.declare_constant("q", bool_sort);
  const primo::core::SymbolId r = symbols.declare_constant("r", bool_sort);
  const primo::core::SymbolId f = symbols.declare_function("f", {bool_sort}, bool_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId pt = terms.mk_constant(p);
  const primo::core::TermId qt = terms.mk_constant(q);
  const primo::core::TermId rt = terms.mk_constant(r);
  const primo::core::TermId implication = terms.mk_implies(pt, qt);
  const primo::core::TermId equality = terms.mk_eq(implication, rt);
  const primo::core::TermId application = terms.mk_app(f, {implication});

  const primo::smt::BooleanImpliesRewriteResult result = primo::smt::rewrite_boolean_implications(
      terms, {equality, application}, primo::smt::BooleanImpliesRewriteMode::Clause, 4);
  check(result.visited == 1);
  check(result.rewritten == 1);
  check(terms.get(result.assertions.at(0)).kind == primo::core::TermKind::Equality);
  check(terms.get(result.assertions.at(0)).children.at(0) != implication);
  check(result.assertions.at(1) == application);
}

void boolean_nnf_pushes_negations_without_distribution() {
  Fixture fixture;
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const primo::core::TermId pc = fixture.pred(fixture.p, fixture.constant(fixture.c));
  const primo::core::TermId lhs = fixture.terms.mk_and({pa, pb});
  const primo::core::TermId rhs = fixture.terms.mk_or({pb, pc});
  const primo::core::TermId assertion = fixture.terms.mk_not(fixture.terms.mk_implies(lhs, rhs));

  const primo::smt::BooleanNegationRewriteResult result =
      primo::smt::rewrite_boolean_negations_to_nnf(
          fixture.terms, {assertion}, primo::smt::BooleanNegationRewriteMode::NegationNormalForm);
  const primo::core::TermId expected_rhs =
      fixture.terms.mk_and({fixture.terms.mk_not(pb), fixture.terms.mk_not(pc)});
  check(result.assertions ==
        std::vector<primo::core::TermId>({fixture.terms.mk_and({lhs, expected_rhs})}));
  check(result.rewritten >= 3);
}

void boolean_nnf_memoizes_both_polarities_and_keeps_uf_atoms_opaque() {
  primo::core::SymbolTable symbols;
  const primo::core::SortId bool_sort = symbols.bool_sort();
  const primo::core::SymbolId p = symbols.declare_constant("p", bool_sort);
  const primo::core::SymbolId q = symbols.declare_constant("q", bool_sort);
  const primo::core::SymbolId f = symbols.declare_function("f", {bool_sort}, bool_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId pt = terms.mk_constant(p);
  const primo::core::TermId qt = terms.mk_constant(q);
  const primo::core::TermId implication = terms.mk_implies(pt, qt);
  const primo::core::TermId application = terms.mk_app(f, {implication});
  const primo::core::TermId negated_implication = terms.mk_not(implication);
  const primo::core::TermId negated_application = terms.mk_not(application);
  const std::size_t source_term_count = terms.size();

  const primo::smt::BooleanNegationRewriteResult result =
      primo::smt::rewrite_boolean_negations_to_nnf(
          terms, {implication, negated_implication, negated_application},
          primo::smt::BooleanNegationRewriteMode::NegationNormalForm);
  check(result.visited <= source_term_count * 2);
  check(result.assertions.at(2) == terms.mk_not(application));
  check(terms.get(application).children.at(0) == implication);
  check(terms.get(result.assertions.at(0)).kind == primo::core::TermKind::Or);
  check(terms.get(result.assertions.at(1)).kind == primo::core::TermKind::And);
}

void finite_domain_analysis_is_non_materializing() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, {a, b, c}));
  const std::size_t term_count = fixture.terms.size();

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAnalysis analysis = amo.analyze(assertions);
  check(analysis.domains == 1);
  check(analysis.choices == 1);
  check(analysis.predicted_amo_clauses == 3);
  check(fixture.terms.size() == term_count);

  Fixture observed_fixture;
  const primo::core::TermId observed_a = observed_fixture.constant(observed_fixture.a);
  const primo::core::TermId observed_b = observed_fixture.constant(observed_fixture.b);
  const primo::core::TermId observed_c = observed_fixture.constant(observed_fixture.c);
  const primo::core::TermId observed_x = observed_fixture.constant(observed_fixture.x);
  std::vector<primo::core::TermId> observed = observed_fixture.value_disequalities();
  observed.push_back(observed_fixture.terms.mk_eq(observed_x, observed_a));
  observed.push_back(observed_fixture.terms.mk_eq(observed_x, observed_b));
  observed.push_back(observed_fixture.terms.mk_eq(observed_x, observed_c));
  const std::size_t observed_term_count = observed_fixture.terms.size();
  primo::smt::FiniteDomainAtMostOne observed_amo(observed_fixture.terms);
  const primo::smt::FiniteDomainAnalysis observed_analysis = observed_amo.analyze(observed);
  check(observed_analysis.domains == 1);
  check(observed_analysis.choices == 0);
  check(observed_analysis.predicted_amo_clauses == 3);
  check(observed_fixture.terms.size() == observed_term_count);
}

void integer_numeral_domains_enable_amo_and_equality_channeling_without_symmetry() {
  primo::core::SymbolTable symbols;
  const primo::core::SortId int_sort = symbols.int_sort();
  const primo::core::SymbolId x_symbol = symbols.declare_constant("x", int_sort);
  const primo::core::SymbolId y_symbol = symbols.declare_constant("y", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);
  const primo::core::TermId one = terms.mk_rational(1, int_sort);
  const primo::core::TermId two = terms.mk_rational(2, int_sort);
  const primo::core::TermId x_choice = terms.mk_or({terms.mk_eq(x, one), terms.mk_eq(x, two)});
  const primo::core::TermId y_choice = terms.mk_or({terms.mk_eq(y, one), terms.mk_eq(y, two)});
  const std::vector<primo::core::TermId> assertions{x_choice, y_choice, terms.mk_eq(x, y)};

  primo::smt::FiniteDomainAtMostOne amo(terms);
  const primo::smt::FiniteDomainAnalysis analysis = amo.analyze(assertions);
  check(analysis.choices == 2);
  check(analysis.integer_numeral_choices == 2);
  check(analysis.explicit_disequality_choices == 0);
  check(analysis.predicted_amo_clauses == 2);
  const primo::smt::FiniteDomainAtMostOneResult generated = amo.generate(assertions);
  check(generated.domains == 2);
  check(generated.clauses.size() == 2);

  primo::smt::FiniteDomainEqualityDefinitions definitions(terms);
  const primo::smt::FiniteDomainEqualityDefinitionsResult channeling =
      definitions.generate(assertions);
  check(channeling.candidate_term_pairs == 1);
  check(channeling.term_pairs == 1);
  check(channeling.clauses.size() == 6);

  primo::smt::UfSymmetryBreaker symmetry(terms);
  const primo::smt::UfSymmetryBreakingResult symmetry_result =
      symmetry.break_symmetries(assertions);
  check(symmetry_result.accepted_sets == 0);
  check(symmetry_result.constraints.empty());
}

void real_or_mixed_value_lists_do_not_gain_semantic_distinctness() {
  primo::core::SymbolTable symbols;
  const primo::core::SortId real_sort = symbols.real_sort();
  const primo::core::SymbolId x_symbol = symbols.declare_constant("x", real_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId one = terms.mk_rational(1, real_sort);
  const primo::core::TermId two = terms.mk_rational(2, real_sort);
  const primo::core::TermId choice = terms.mk_or({terms.mk_eq(x, one), terms.mk_eq(x, two)});

  primo::smt::FiniteDomainAtMostOne amo(terms);
  const primo::smt::FiniteDomainAnalysis analysis = amo.analyze({choice});
  check(analysis.choices == 0);
  check(analysis.integer_numeral_choices == 0);
  check(analysis.rejected_missing_distinctness == 1);
}

// --- monotone (one-sided) variable elimination -------------------------
//
// Each test pins one soundness precondition. The pass is only correct
// because a disqualified variable stays in the problem, so every negative
// test here is load-bearing.

void monotone_elimination_discharges_one_sided_variable() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  // x is only ever bounded above, so an arbitrarily small x satisfies both.
  const primo::core::TermId first = fixture.terms.mk_le(x, fixture.rational(5));
  const primo::core::TermId second = fixture.terms.mk_le(x, fixture.rational(3));

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {first, second});

  check(result.candidates == 1);
  check(result.eliminated.size() == 1);
  check(result.eliminated.front().variable == x);
  check(result.eliminated.front().bounded_above);
  check(result.eliminated.front().constraints.size() == 2);
  check(result.atoms_folded == 2);
  const primo::core::TermId truth = fixture.terms.mk_bool(true);
  check(result.assertions.at(0) == truth);
  check(result.assertions.at(1) == truth);
}

void monotone_elimination_respects_polarity() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  // (<= x 5) positively and (not (<= x 3)) negatively push x in opposite
  // directions, so x is two-sided even though both atoms mention only x.
  const primo::core::TermId positive = fixture.terms.mk_le(x, fixture.rational(5));
  const primo::core::TermId negative =
      fixture.terms.mk_not(fixture.terms.mk_le(x, fixture.rational(3)));

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {positive, negative});

  check(result.candidates == 0);
  check(result.eliminated.empty());
  check(result.atoms_folded == 0);
  check(result.assertions.at(0) == positive);
  check(result.assertions.at(1) == negative);
}

void monotone_elimination_folds_negative_polarity_atom_to_false() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  // Only occurrence is negated, so the atom must become false, not true.
  const primo::core::TermId negated =
      fixture.terms.mk_not(fixture.terms.mk_le(x, fixture.rational(5)));

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {negated});

  check(result.eliminated.size() == 1);
  check(!result.eliminated.front().bounded_above);
  // mk_not folds the constant away, so the whole assertion collapses.
  check(result.assertions.at(0) == fixture.terms.mk_bool(true));
}

void monotone_elimination_keeps_variable_pinned_by_equality() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  // An equality pins x from both sides even though the inequality alone
  // would look one-sided.
  const primo::core::TermId bound = fixture.terms.mk_le(x, fixture.rational(5));
  const primo::core::TermId equality = fixture.terms.mk_eq(x, y);

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {bound, equality});

  check(result.eliminated.empty());
  check(result.atoms_folded == 0);
}

void monotone_elimination_keeps_variable_under_uninterpreted_application() {
  primo::core::SymbolTable symbols;
  const primo::core::SortId real_sort = symbols.real_sort();
  const primo::core::SortId u = symbols.declare_sort("U");
  const primo::core::SymbolId x = symbols.declare_constant("x", real_sort);
  const primo::core::SymbolId f = symbols.declare_function("f", {real_sort}, u);
  const primo::core::SymbolId a = symbols.declare_constant("a", u);
  primo::core::TermManager terms{symbols};
  const primo::core::TermId xt = terms.mk_constant(x);
  // f(x) constrains x in a way this pass cannot see, so x must survive.
  const primo::core::TermId bound = terms.mk_le(xt, terms.mk_rational(5));
  const primo::core::TermId application = terms.mk_eq(terms.mk_app(f, {xt}), terms.mk_constant(a));

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(terms, {bound, application});

  check(result.eliminated.empty());
  check(result.atoms_folded == 0);
}

void monotone_elimination_orders_dependent_variables() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  // x <= y bounds x above and y below; 1 <= y bounds y below again, so
  // both stay one-sided. They share an atom and so depend on each other,
  // which an ordering scheme would have called a cycle -- both must still
  // be eliminated.
  const primo::core::TermId first = fixture.terms.mk_le(x, y);
  const primo::core::TermId second = fixture.terms.mk_le(fixture.rational(1), y);

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {first, second});

  check(result.eliminated.size() == 2);
  std::size_t x_index = result.eliminated.size();
  std::size_t y_index = result.eliminated.size();
  for (std::size_t i = 0; i < result.eliminated.size(); ++i) {
    if (result.eliminated[i].variable == x) {
      x_index = i;
    }
    if (result.eliminated[i].variable == y) {
      y_index = i;
    }
  }
  check(x_index < result.eliminated.size() && y_index < result.eliminated.size());
}

void monotone_elimination_leaves_two_sided_variables_alone() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId lower = fixture.terms.mk_le(fixture.rational(1), x);
  const primo::core::TermId upper = fixture.terms.mk_le(x, fixture.rational(5));

  const primo::smt::MonotoneEliminationResult result =
      primo::smt::eliminate_monotone_variables(fixture.terms, {lower, upper});

  check(result.candidates == 0);
  check(result.eliminated.empty());
  check(result.assertions.at(0) == lower);
  check(result.assertions.at(1) == upper);
}

void symmetric_fixed_terms_get_value_precedence() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_or(
      {fixture.pred(fixture.p, a), fixture.pred(fixture.p, b), fixture.pred(fixture.p, c)}));
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 1);
  check(result.values == 3);
  check(result.terms == 3);
  check(result.rejected_noninvariant == 0);
  check(result.constraints.size() == 6);
}

void asymmetric_value_use_is_rejected() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.pred(fixture.p, a));
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 0);
  check(result.rejected_noninvariant == 1);
  check(result.constraints.empty());
}

void symmetry_comparison_preserves_numeric_payloads() {
  using primo::core::TermId;
  using primo::core::TermKind;
  using primo::util::Rational;
  struct PayloadCase {
    TermKind kind;
    Rational first;
    Rational second;
  };
  const Rational large = Rational::parse("123456789012345678901234567890").value();
  const std::vector<PayloadCase> cases{
      {TermKind::Rational, Rational(0), Rational(1)},
      {TermKind::Rational, Rational(1, 3), Rational(2, 3)},
      {TermKind::Rational, Rational(-1, 3), Rational(1, 3)},
      {TermKind::Rational, large, large + Rational(1)},
      {TermKind::Times, Rational(2), Rational(3)},
      {TermKind::Times, Rational(-2), Rational(2)},
      {TermKind::Times, Rational(1, 3), Rational(2, 3)},
      {TermKind::Times, large, large + Rational(1)},
      {TermKind::IntDiv, Rational(2), Rational(3)},
      {TermKind::IntMod, Rational(2), Rational(3)},
  };
  for (const PayloadCase& test : cases) {
    for (const bool symmetric : {false, true}) {
      Fixture fixture;
      const bool integer = test.kind == TermKind::IntDiv || test.kind == TermKind::IntMod;
      const auto result_sort = integer ? fixture.symbols.int_sort() : fixture.symbols.real_sort();
      const auto h = fixture.symbols.declare_function("h", {fixture.sort}, result_sort);
      const TermId a = fixture.constant(fixture.a);
      const TermId b = fixture.constant(fixture.b);
      const TermId x = fixture.constant(fixture.x);
      const auto atom = [&](TermId argument, const Rational& payload) {
        const TermId application = fixture.app(h, argument);
        TermId expression = application;
        switch (test.kind) {
          case TermKind::Rational:
            return fixture.terms.mk_eq(application,
                                       fixture.terms.mk_rational(payload, result_sort));
          case TermKind::Times:
            expression = fixture.terms.mk_times(payload, application);
            break;
          case TermKind::IntDiv:
            expression = fixture.terms.mk_int_div(payload, application);
            break;
          case TermKind::IntMod:
            expression = fixture.terms.mk_int_mod(payload, application);
            break;
          default:
            std::abort();
        }
        check(fixture.terms.get(expression).kind == test.kind);
        return fixture.terms.mk_eq(expression, fixture.terms.mk_rational(Rational(1), result_sort));
      };
      const std::vector<TermId> assertions{fixture.neq(a, b), fixture.choices(x, {a, b}),
                                           atom(a, test.first),
                                           atom(b, symmetric ? test.first : test.second)};
      for (const bool value_precedence : {false, true}) {
        primo::smt::UfSymmetryBreaker breaker(fixture.terms);
        const auto result = breaker.break_symmetries(assertions, {value_precedence});
        // Equal numeric payloads must still permit the finite-choice symmetry;
        // different payloads must reject it before either restriction generator.
        check(result.accepted_sets == (symmetric ? 1U : 0U));
        check(result.rejected_noninvariant == (symmetric ? 0U : 1U));
        check(result.constraints.size() == (symmetric ? 1U : 0U));
      }
    }
  }
}

void moving_only_symmetry_terms_are_rejected() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(fixture.app(fixture.f, a), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, b), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, c), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 0);
  check(result.values == 0);
  check(result.terms == 0);
  check(result.rejected_noninvariant == 1);
  check(result.constraints.empty());
}

void mixed_fixed_and_moving_terms_use_fixed_value_precedence_only() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, a), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, b), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, c), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 1);
  check(result.values == 3);
  check(result.terms == 3);
  check(result.rejected_noninvariant == 0);
  check(result.constraints.size() == 6);
}

void equality_bridge_derives_common_branch_consequence() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);

  const primo::core::TermId left =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, y), fixture.terms.mk_eq(y, z)});
  const primo::core::TermId right =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, a), fixture.terms.mk_eq(a, z)});
  const primo::core::TermId unrelated =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, b), fixture.terms.mk_eq(b, z)});
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_or({left, right, unrelated})};

  primo::smt::EqualityBridge bridge(fixture.terms);
  const primo::smt::EqualityBridgeResult result = bridge.bridge(assertions);

  check(result.skipped_or_nodes == 0);
  check(result.equalities.size() == 1);
  const primo::core::Term& derived = fixture.terms.get(result.equalities.front());
  check(derived.kind == primo::core::TermKind::Equality);
  check((derived.children == std::vector{x, z} || derived.children == std::vector{z, x}));
}

void unit_equality_propagation_simplifies_disequality() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);

  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_eq(x, y),
      fixture.terms.mk_eq(y, z),
      fixture.terms.mk_not(fixture.terms.mk_eq(x, z)),
  };

  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions);

  check(simplified.size() == assertions.size() + 1);
  check(simplified[0] == assertions[0]);
  check(simplified[1] == assertions[1]);
  check(simplified[2] == assertions[2]);
  check(fixture.terms.get(simplified[3]).kind == primo::core::TermKind::BoolValue);
  check(!fixture.terms.get(simplified[3]).bool_value);
}

void unit_equality_propagation_simplifies_nested_boolean_terms_after_interning() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId pb = fixture.pred(fixture.p, b);

  const primo::core::TermId inner_lhs = fixture.terms.mk_and({fixture.terms.mk_eq(x, y), pa});
  const primo::core::TermId inner = fixture.terms.mk_implies(inner_lhs, pb);
  const primo::core::TermId outer =
      fixture.terms.mk_implies(inner, fixture.terms.mk_not(fixture.terms.mk_eq(a, b)));
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_eq(x, y), outer};

  primo::smt::UnitEqualityPropagationStats stats;
  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions, &stats);

  check(!simplified.empty());
  check(stats.consequences_added >= 1);
}

void unit_equality_propagation_skips_without_nonunit_queries() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_eq(a, b),
      fixture.terms.mk_and({fixture.terms.mk_eq(b, c)}),
  };

  primo::smt::UnitEqualityPropagationStats stats;
  stats.consequences_added = 99;
  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions, &stats);

  check(stats.consequences_added == 0);
  check(simplified == assertions);
}

void unit_equality_propagation_visits_shared_query_dag_once() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId fb = fixture.app(fixture.f, b);
  const primo::core::TermId query = fixture.terms.mk_eq(fa, fb);
  const primo::core::TermId shared = fixture.terms.mk_or({query, fixture.pred(fixture.p, a)});
  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_eq(a, b), fixture.terms.mk_or({shared, fixture.pred(fixture.p, b)}),
      fixture.terms.mk_implies(shared, fixture.pred(fixture.p, c))};

  primo::smt::UnitEqualityPropagationStats stats;
  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions, &stats);

  check(!simplified.empty());
  check(stats.query_memo_hits >= 1);
  check(stats.query_nodes <= fixture.terms.size());
  check(stats.add_term_seen_hits >= 1);
  check(stats.add_term_requests < 20);
}

void ground_unit_substitution_rewrites_applications_and_preserves_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId pc = fixture.pred(fixture.p, c);
  const primo::core::TermId unit = fixture.terms.mk_eq(fa, b);
  const primo::core::TermId formula = fixture.terms.mk_or({fixture.terms.mk_eq(fa, c), pc});
  const std::vector<primo::core::TermId> assertions{unit, formula};

  const primo::smt::GroundEqualitySubstitutionResult result =
      primo::smt::substitute_ground_unit_equalities(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), unit) !=
        result.assertions.end());
  const primo::core::TermId rewritten = fixture.terms.mk_or({fixture.terms.mk_eq(b, c), pc});
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void ground_unit_substitution_uses_conjunctive_units_and_drops_true_parts() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId unit = fixture.terms.mk_eq(fa, b);
  const primo::core::TermId assertion =
      fixture.terms.mk_and({unit, fixture.terms.mk_not(fixture.terms.mk_eq(fa, c))});

  const primo::smt::GroundEqualitySubstitutionResult result =
      primo::smt::substitute_ground_unit_equalities(fixture.terms, {assertion});

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), unit) !=
        result.assertions.end());
  const primo::core::TermId rewritten = fixture.terms.mk_not(fixture.terms.mk_eq(b, c));
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void solved_constant_substitution_eliminates_acyclic_definition() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId definition = fixture.terms.mk_eq(x, fa);
  const primo::core::TermId formula = fixture.terms.mk_or(
      {fixture.terms.mk_eq(fixture.app(fixture.f, x), y), fixture.pred(fixture.p, x)});

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(fixture.terms, {definition, formula});

  check(result.candidates == 1);
  check(result.accepted == 1);
  check(result.rewrites >= 1);
  check(result.rejected_cycles == 0);
  check(result.rejected_blowups == 0);
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_bool(true)) != result.assertions.end());
  const primo::core::TermId rewritten = fixture.terms.mk_or(
      {fixture.terms.mk_eq(fixture.app(fixture.f, fa), y), fixture.pred(fixture.p, fa)});
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void solved_constant_substitution_rejects_cycles() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId fx = fixture.app(fixture.f, x);
  const primo::core::TermId definition = fixture.terms.mk_eq(x, fx);

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(fixture.terms, {definition});

  check(result.candidates == 1);
  check(result.accepted == 0);
  check(result.rewrites == 0);
  check(result.rejected_cycles == 1);
  check(result.assertions == std::vector{definition});
}

void solved_constant_substitution_eliminates_real_equality_definition() {
  // Real equality now reaches this pass as a genuine TermKind::Equality
  // node (mk_eq is called uniformly at parse time; lowering to Le/Lt
  // happens later, see lower_real_equalities), so the existing
  // Equality-kind recognizer fires for Real terms with no LRA-specific
  // recognizer needed.
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId five = fixture.rational(5);
  const primo::core::TermId definition = fixture.terms.mk_eq(x, five);
  const primo::core::TermId formula =
      fixture.terms.mk_le(fixture.terms.mk_plus({x, y}), fixture.rational(10));

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(fixture.terms, {definition, formula});

  check(result.candidates == 1);
  check(result.accepted == 1);
  check(result.rewrites >= 1);
  check(result.rejected_cycles == 0);
  const primo::core::TermId rewritten =
      fixture.terms.mk_le(fixture.terms.mk_plus({five, y}), fixture.rational(10));
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void gaussian_elimination_solves_joint_linear_system() {
  // Neither equation alone defines a variable (solved_constant_candidate_
  // from_equality requires a bare Constant on one side), but row-reducing
  // 2x+3y=5 and x-y=1 together gives x=8/5, y=3/5.
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId eq1 = fixture.terms.mk_eq(
      fixture.terms.mk_plus({fixture.terms.mk_times(2, x), fixture.terms.mk_times(3, y)}),
      fixture.rational(5));
  const primo::core::TermId eq2 = fixture.terms.mk_eq(
      fixture.terms.mk_plus({x, fixture.terms.mk_times(-1, y)}), fixture.rational(1));
  const primo::core::TermId formula =
      fixture.terms.mk_le(fixture.terms.mk_plus({x, y}), fixture.rational(10));

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(fixture.terms, {eq1, eq2, formula},
                                              /*enable_gaussian_elimination=*/true);

  check(result.gaussian_candidates == 2);
  check(result.accepted == 2);
  check(result.rejected_cycles == 0);
  const primo::core::TermId x_value = fixture.terms.mk_rational(primo::util::Rational(8, 5));
  const primo::core::TermId y_value = fixture.terms.mk_rational(primo::util::Rational(3, 5));
  const primo::core::TermId rewritten =
      fixture.terms.mk_le(fixture.terms.mk_plus({x_value, y_value}), fixture.rational(10));
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void gaussian_elimination_solves_single_equation_with_coefficient() {
  // `2x = 5` has no bare-variable side, so solved_constant_candidate_
  // from_equality finds nothing; the Gaussian pass still solves it as a
  // degenerate 1x1 system.
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId eq =
      fixture.terms.mk_eq(fixture.terms.mk_times(2, x), fixture.rational(5));
  const primo::core::TermId formula = fixture.terms.mk_le(x, fixture.rational(100));

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(fixture.terms, {eq, formula},
                                              /*enable_gaussian_elimination=*/true);

  check(result.gaussian_candidates == 1);
  check(result.accepted == 1);
  const primo::core::TermId rewritten = fixture.terms.mk_le(
      fixture.terms.mk_rational(primo::util::Rational(5, 2)), fixture.rational(100));
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void gaussian_elimination_bails_out_above_equation_cap() {
  // Defensive bound on the O(equations^2 * variables) elimination cost --
  // 129 equations (one more than kGaussianEliminationMaxEquations) should
  // make the pass bail out cleanly (no crash/hang, gaussian_candidates
  // stays 0), even though every individual `xi = 1` equation is trivially
  // solvable and would normally be found (each is also caught by the
  // ordinary bare-variable recognizer regardless, so accepted still ends
  // up nonzero -- only the Gaussian-specific contribution is under test).
  Fixture uninterpreted;  // reused only for its SymbolTable/TermManager
  primo::core::SymbolTable& symbols = uninterpreted.symbols;
  primo::core::TermManager& terms = uninterpreted.terms;
  const primo::core::SortId real_sort = symbols.real_sort();

  std::vector<primo::core::TermId> equalities;
  for (int i = 0; i < 129; ++i) {
    const primo::core::SymbolId variable =
        symbols.declare_constant("gauss_cap_" + std::to_string(i), real_sort);
    equalities.push_back(terms.mk_eq(terms.mk_constant(variable), terms.mk_rational(1)));
  }

  const primo::smt::SolvedConstantSubstitutionResult result =
      primo::smt::substitute_solved_constants(terms, equalities,
                                              /*enable_gaussian_elimination=*/true);

  check(result.gaussian_candidates == 0);
}

void lower_real_equalities_rewrites_real_sorted_equality_only() {
  RealFixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId equality = fixture.terms.mk_eq(x, y);

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {equality});

  check(result.lowered == 1);
  check(result.assertions.size() == 1);
  const primo::core::TermId expected =
      fixture.terms.mk_and({fixture.terms.mk_le(x, y), fixture.terms.mk_le(y, x)});
  check(result.assertions.front() == expected);

  Fixture uninterpreted;
  const primo::core::TermId a = uninterpreted.constant(uninterpreted.a);
  const primo::core::TermId b = uninterpreted.constant(uninterpreted.b);
  const primo::core::TermId uf_equality = uninterpreted.terms.mk_eq(a, b);
  const primo::smt::RealEqualityLoweringResult uf_result =
      primo::smt::lower_real_equalities(uninterpreted.terms, {uf_equality});
  check(uf_result.lowered == 0);
  check(uf_result.assertions.front() == uf_equality);
}

void lower_real_equalities_distributes_single_guarded_real_equality() {
  RealFixture fixture;
  const primo::core::TermId guard = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("guard", fixture.symbols.bool_sort()));
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId sum = fixture.terms.mk_plus({x, y});
  const primo::core::TermId guarded = fixture.terms.mk_or({guard, fixture.terms.mk_eq(z, sum)});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded},
                                        /*enable_guarded_real_equality_distribution=*/true);

  check(result.lowered == 1);
  check(result.guarded_distributed == 1);
  check(result.assertions.size() == 1);
  const primo::core::TermId expected =
      fixture.terms.mk_and({fixture.terms.mk_or({guard, fixture.terms.mk_le(z, sum)}),
                            fixture.terms.mk_or({guard, fixture.terms.mk_le(sum, z)})});
  check(result.assertions.front() == expected);
}

void lower_real_equalities_keeps_guarded_distribution_disabled_by_default() {
  RealFixture fixture;
  const primo::core::TermId guard = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("guard", fixture.symbols.bool_sort()));
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId sum = fixture.terms.mk_plus({x, y});
  const primo::core::TermId guarded = fixture.terms.mk_or({guard, fixture.terms.mk_eq(z, sum)});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded});

  check(result.lowered == 1);
  check(result.guarded_distributed == 0);
  const primo::core::TermId expected = fixture.terms.mk_or(
      {guard, fixture.terms.mk_and({fixture.terms.mk_le(z, sum), fixture.terms.mk_le(sum, z)})});
  check(result.assertions.front() == expected);
}

void lower_real_equalities_rewrites_guard_before_distribution() {
  RealFixture fixture;
  const primo::core::TermId flag = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("flag", fixture.symbols.bool_sort()));
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId sum = fixture.terms.mk_plus({x, y});
  const primo::core::TermId real_equality_guard = fixture.terms.mk_eq(x, y);
  const primo::core::TermId rewritten_real_equality_guard =
      fixture.terms.mk_and({fixture.terms.mk_le(x, y), fixture.terms.mk_le(y, x)});
  const primo::core::TermId guard = fixture.terms.mk_and({flag, real_equality_guard});
  const primo::core::TermId rewritten_guard =
      fixture.terms.mk_and({flag, rewritten_real_equality_guard});
  const primo::core::TermId guarded = fixture.terms.mk_or({guard, fixture.terms.mk_eq(z, sum)});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded},
                                        /*enable_guarded_real_equality_distribution=*/true);

  check(result.lowered == 2);
  check(result.guarded_distributed == 1);
  const primo::core::TermId expected =
      fixture.terms.mk_and({fixture.terms.mk_or({rewritten_guard, fixture.terms.mk_le(z, sum)}),
                            fixture.terms.mk_or({rewritten_guard, fixture.terms.mk_le(sum, z)})});
  check(result.assertions.front() == expected);
}

void lower_real_equalities_does_not_distribute_negated_real_equality() {
  RealFixture fixture;
  const primo::core::TermId guard = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("guard", fixture.symbols.bool_sort()));
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId disequality = fixture.terms.mk_not(fixture.terms.mk_eq(x, y));
  const primo::core::TermId guarded = fixture.terms.mk_or({guard, disequality});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded});

  check(result.lowered == 1);
  check(result.guarded_distributed == 0);
  const primo::core::TermId expected =
      fixture.terms.mk_or({guard, fixture.terms.mk_not(fixture.terms.mk_and(
                                      {fixture.terms.mk_le(x, y), fixture.terms.mk_le(y, x)}))});
  check(result.assertions.front() == expected);
}

void lower_real_equalities_does_not_distribute_uninterpreted_equality() {
  Fixture fixture;
  const primo::core::TermId guard = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("guard", fixture.symbols.bool_sort()));
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId guarded = fixture.terms.mk_or({guard, fixture.terms.mk_eq(a, b)});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded});

  check(result.lowered == 0);
  check(result.guarded_distributed == 0);
  check(result.assertions.front() == guarded);
}

void lower_real_equalities_does_not_overdistribute_multiple_guarded_equalities() {
  RealFixture fixture;
  const primo::core::TermId guard = fixture.terms.mk_constant(
      fixture.symbols.declare_constant("guard", fixture.symbols.bool_sort()));
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId guarded =
      fixture.terms.mk_or({guard, fixture.terms.mk_eq(x, y), fixture.terms.mk_eq(y, z)});

  const primo::smt::RealEqualityLoweringResult result =
      primo::smt::lower_real_equalities(fixture.terms, {guarded});

  check(result.lowered == 2);
  check(result.guarded_distributed == 0);
  const primo::core::TermId expected = fixture.terms.mk_or(
      {guard, fixture.terms.mk_and({fixture.terms.mk_le(x, y), fixture.terms.mk_le(y, x)}),
       fixture.terms.mk_and({fixture.terms.mk_le(y, z), fixture.terms.mk_le(z, y)})});
  check(result.assertions.front() == expected);
}

void boolean_unit_propagation_simplifies_boolean_structure_and_arguments() {
  Fixture fixture;
  const primo::core::TermId p =
      fixture.constant(fixture.symbols.declare_constant("flag", fixture.symbols.bool_sort()));
  const primo::core::TermId true_term = fixture.terms.mk_bool(true);
  const primo::core::SymbolId g =
      fixture.symbols.declare_function("g", {fixture.symbols.bool_sort()}, fixture.sort);
  const primo::core::TermId gp = fixture.terms.mk_app(g, {p});
  const primo::core::TermId gt = fixture.terms.mk_app(g, {true_term});
  const std::vector<primo::core::TermId> assertions{
      p,
      fixture.terms.mk_or(
          {fixture.terms.mk_not(p), fixture.pred(fixture.p, fixture.constant(fixture.a))}),
      fixture.terms.mk_not(fixture.terms.mk_eq(gp, gt)),
  };

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 2);
  check(result.assertions.size() == 3);
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId disequality = fixture.terms.mk_not(fixture.terms.mk_eq(gp, gt));
  check(std::find(result.assertions.begin(), result.assertions.end(), p) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), pa) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), disequality) !=
        result.assertions.end());
}

void boolean_unit_propagation_preserves_contradictory_units() {
  Fixture fixture;
  const primo::core::TermId p =
      fixture.constant(fixture.symbols.declare_constant("flag", fixture.symbols.bool_sort()));
  const std::vector<primo::core::TermId> assertions{
      p, fixture.terms.mk_not(p),
      fixture.terms.mk_or(
          {fixture.terms.mk_not(p), fixture.pred(fixture.p, fixture.constant(fixture.a))})};

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.units == 0);
  check(result.rewrites == 0);
  check(result.assertions == assertions);
}

void boolean_unit_propagation_skips_when_no_units_exist() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_or({fixture.terms.mk_eq(a, b), fixture.terms.mk_eq(b, c)}),
      fixture.terms.mk_not(fixture.terms.mk_eq(a, c)),
  };

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.units == 0);
  check(result.assignments.empty());
  check(result.rewrites == 0);
  check(result.assertions == assertions);
}

void boolean_unit_simplification_memoizes_shared_boolean_subterms() {
  Fixture fixture;
  const auto boolean_constant = [&fixture](const char* name) {
    return fixture.constant(fixture.symbols.declare_constant(name, fixture.symbols.bool_sort()));
  };
  const primo::core::TermId p = boolean_constant("memo_p");
  const primo::core::TermId q = boolean_constant("memo_q");
  const primo::core::TermId r = boolean_constant("memo_r");
  const primo::core::TermId s = boolean_constant("memo_s");
  const primo::core::TermId shared = fixture.terms.mk_or({fixture.terms.mk_not(p), q});
  const std::vector<primo::core::TermId> assertions{p, fixture.terms.mk_or({shared, r}),
                                                    fixture.terms.mk_implies(shared, s)};

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.rewrites >= 2);
  check(result.memo_hits >= 1);
  check(result.memo_misses >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_or({q, r})) != result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_implies(q, s)) != result.assertions.end());
}

void forced_atom_simplification_memoizes_shared_boolean_subterms() {
  Fixture fixture;
  const primo::core::TermId atom = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId q = fixture.constant(
      fixture.symbols.declare_constant("forced_memo_q", fixture.symbols.bool_sort()));
  const primo::core::TermId r = fixture.constant(
      fixture.symbols.declare_constant("forced_memo_r", fixture.symbols.bool_sort()));
  const primo::core::TermId s = fixture.constant(
      fixture.symbols.declare_constant("forced_memo_s", fixture.symbols.bool_sort()));
  const primo::core::TermId shared = fixture.terms.mk_or({fixture.terms.mk_not(atom), q});
  const std::vector<primo::core::TermId> assertions{atom, fixture.terms.mk_or({shared, r}),
                                                    fixture.terms.mk_implies(shared, s)};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.rewrites >= 2);
  check(result.memo_hits >= 1);
  check(result.memo_misses >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_or({q, r})) != result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_implies(q, s)) != result.assertions.end());
}

void forced_atom_simplification_handles_deep_boolean_terms() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId equality = fixture.terms.mk_eq(a, b);
  const primo::core::TermId p = fixture.constant(
      fixture.symbols.declare_constant("deep_forced_p", fixture.symbols.bool_sort()));
  const primo::core::TermId q = fixture.constant(
      fixture.symbols.declare_constant("deep_forced_q", fixture.symbols.bool_sort()));

  primo::core::TermId deep = p;
  for (std::size_t depth = 0; depth < 20000; ++depth) {
    deep = depth % 2 == 0 ? fixture.terms.mk_implies(deep, q) : fixture.terms.mk_implies(p, deep);
  }

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, {equality, deep});

  check(result.units == 1);
  check(result.equality_pairs.size() == 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), deep) !=
        result.assertions.end());
}

void forced_atom_propagation_simplifies_predicate_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId d =
      fixture.constant(fixture.symbols.declare_constant("d", fixture.sort));
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId cd = fixture.terms.mk_eq(c, d);
  const std::vector<primo::core::TermId> assertions{
      pa, fixture.terms.mk_or({fixture.terms.mk_not(pa), cd})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), pa) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), cd) !=
        result.assertions.end());
}

void forced_atom_propagation_uses_negative_equality_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId d =
      fixture.constant(fixture.symbols.declare_constant("d", fixture.sort));
  const primo::core::TermId ab = fixture.terms.mk_eq(a, b);
  const primo::core::TermId cd = fixture.terms.mk_eq(c, d);
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_not(ab),
                                                    fixture.terms.mk_or({ab, cd})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 1);
  check(result.disequalities == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), fixture.terms.mk_not(ab)) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), cd) !=
        result.assertions.end());
}

void forced_atom_propagation_folds_disequalities_modulo_equalities() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId ab = fixture.terms.mk_eq(a, b);
  const primo::core::TermId ac = fixture.terms.mk_eq(a, c);
  const primo::core::TermId bc = fixture.terms.mk_eq(b, c);
  const primo::core::TermId guard = fixture.pred(
      fixture.p, fixture.constant(fixture.symbols.declare_constant("d", fixture.sort)));
  const std::vector<primo::core::TermId> assertions{ab, fixture.terms.mk_not(ac),
                                                    fixture.terms.mk_or({bc, guard})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 2);
  check(result.disequalities == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), guard) !=
        result.assertions.end());
}

void forced_atom_propagation_folds_transitive_disequalities() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId ab = fixture.terms.mk_eq(a, b);
  const primo::core::TermId bc = fixture.terms.mk_eq(b, c);
  const primo::core::TermId ac = fixture.terms.mk_eq(a, c);
  const std::vector<primo::core::TermId> assertions{ab, bc, fixture.terms.mk_not(ac)};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 3);
  check(result.disequalities == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_bool(false)) != result.assertions.end());
}

void forced_atom_propagation_preserves_contradictory_exact_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const std::vector<primo::core::TermId> assertions{
      pa, fixture.terms.mk_not(pa), fixture.terms.mk_or({fixture.terms.mk_not(pa), pb})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 0);
  check(result.rewrites == 0);
  check(result.assertions == assertions);
}

void finite_domain_at_most_one_adds_pairwise_clauses() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 2);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainAtMostOneResult repeated = amo.generate(assertions);
  check(repeated.domains == 0);
  check(repeated.clauses.empty());
}

void finite_domain_choices_may_be_nested_or_nodes() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_or(
      {fixture.terms.mk_eq(x, a),
       fixture.terms.mk_or({fixture.terms.mk_eq(x, b), fixture.terms.mk_eq(x, c)})}));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
}

void finite_domain_observed_equalities_get_pairwise_amo_clauses_without_choices() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_eq(x, a));
  assertions.push_back(fixture.terms.mk_eq(x, b));
  assertions.push_back(fixture.terms.mk_eq(x, c));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 2);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainAtMostOneResult repeated = amo.generate(assertions);
  check(repeated.domains == 0);
  check(repeated.clauses.empty());
}

void finite_domain_observed_equalities_use_nonconstant_disequality_endpoints() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId fb = fixture.app(fixture.f, b);
  const primo::core::TermId fc = fixture.app(fixture.f, c);
  const primo::core::TermId x = fixture.constant(fixture.x);

  std::vector<primo::core::TermId> assertions{
      fixture.neq(fa, fb),        fixture.neq(fa, fc),        fixture.neq(fb, fc),
      fixture.terms.mk_eq(x, fa), fixture.terms.mk_eq(x, fb), fixture.terms.mk_eq(x, fc)};

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
}

void finite_domain_distinct_endpoints_get_value_amo_clauses() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));
  assertions.push_back(fixture.neq(x, y));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 2);
  check(result.clauses.size() == 9);
}

void finite_domain_distinct_endpoints_use_additional_forced_equalities() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result =
      amo.generate(assertions, {{y, z}}, {{x, z}});

  check(result.domains == 2);
  check(result.clauses.size() == 9);
}

void finite_domain_equality_definitions_connect_terms_with_same_domain() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));
  assertions.push_back(fixture.neq(x, y));

  primo::smt::FiniteDomainEqualityDefinitions definitions(fixture.terms);
  const primo::smt::FiniteDomainEqualityDefinitionsResult result = definitions.generate(assertions);

  check(result.candidate_term_pairs == 1);
  check(result.term_pairs == 1);
  check(result.predicted_clauses == 9);
  check(result.clauses.size() == 9);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 3);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainEqualityDefinitionsResult repeated =
      definitions.generate(assertions);
  check(repeated.candidate_term_pairs == 1);
  check(repeated.term_pairs == 0);
  check(repeated.clauses.empty());
}

void finite_domain_equality_definitions_require_observed_term_equality() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));

  primo::smt::FiniteDomainEqualityDefinitions definitions(fixture.terms);
  const primo::smt::FiniteDomainEqualityDefinitionsResult result = definitions.generate(assertions);

  check(result.candidate_term_pairs == 0);
  check(result.term_pairs == 0);
  check(result.predicted_clauses == 0);
  check(result.clauses.empty());
}

void finite_domain_equality_definitions_respect_auto_guards() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  const std::vector<primo::core::TermId> assertions{
      fixture.neq(a, b),          fixture.neq(a, c),          fixture.neq(b, c),
      fixture.choices(x, values), fixture.choices(y, values), fixture.terms.mk_eq(x, y),
  };

  primo::smt::FiniteDomainEqualityDefinitions definitions(fixture.terms);
  primo::smt::FiniteDomainEqualityDefinitionsOptions domain_guard;
  domain_guard.max_domain_size = 2;
  const primo::smt::FiniteDomainEqualityDefinitionsResult rejected_domain =
      definitions.generate(assertions, domain_guard);
  check(rejected_domain.candidate_term_pairs == 1);
  check(rejected_domain.rejected_domain_size == 1);
  check(rejected_domain.rejected_clause_limit == 0);
  check(rejected_domain.clauses.empty());

  primo::smt::FiniteDomainEqualityDefinitionsOptions clause_guard;
  clause_guard.max_clauses = 8;
  const primo::smt::FiniteDomainEqualityDefinitionsResult rejected_clauses =
      definitions.generate(assertions, clause_guard);
  check(rejected_clauses.candidate_term_pairs == 1);
  check(rejected_clauses.predicted_clauses == 9);
  check(rejected_clauses.rejected_domain_size == 0);
  check(rejected_clauses.rejected_clause_limit == 1);
  check(rejected_clauses.clauses.empty());
}

void congruence_definitions_connect_same_symbol_applications() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId fx = fixture.app(fixture.f, x);
  const primo::core::TermId fy = fixture.app(fixture.f, y);
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_or(
      {fixture.terms.mk_eq(x, y), fixture.terms.mk_not(fixture.terms.mk_eq(fx, fy))})};

  primo::smt::CongruenceDefinitionGenerator definitions(fixture.terms);
  const primo::smt::CongruenceDefinitionResult result = definitions.generate(assertions);

  check(result.groups == 1);
  check(result.skipped_groups == 0);
  check(result.clauses.size() == 1);
  const primo::core::Term& clause = fixture.terms.get(result.clauses.front());
  check(clause.kind == primo::core::TermKind::Or);
  check(clause.children.size() == 2);
}

void finite_domain_evaluator_simplifies_table_tautology() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId fx = fixture.app(fixture.f, x);
  const std::vector<primo::core::TermId> values{a, b};
  std::vector<primo::core::TermId> assertions{
      fixture.neq(a, b),
      fixture.choices(x, values),
      fixture.terms.mk_eq(fixture.app(fixture.f, a), a),
      fixture.terms.mk_eq(fixture.app(fixture.f, b), b),
      fixture.terms.mk_not(fixture.terms.mk_eq(fx, x)),
  };

  primo::smt::FiniteDomainEvaluator evaluator(fixture.terms);
  const primo::smt::FiniteDomainEvaluationResult result = evaluator.simplify(assertions);

  check(result.equalities_simplified >= 1);
  check(result.enumeration_cutoffs == 0);
  check(result.assertions.size() == assertions.size());
  const primo::core::Term& last = fixture.terms.get(result.assertions.back());
  check(last.kind == primo::core::TermKind::BoolValue);
  check(!last.bool_value);
}

// A counter-backed fresh-constant factory, standing in for what the session
// installs on the TacticContext.
struct AuxiliaryFactory {
  primo::core::SymbolTable& symbols;
  primo::core::TermManager& terms;
  std::size_t next = 0;

  primo::smt::FreshConstantFactory factory() {
    return [this](primo::core::SortId sort, std::string_view prefix) {
      const std::string name = "$primo_" + std::string{prefix} + "_" + std::to_string(next++);
      return terms.mk_constant(symbols.declare_internal_constant(name, sort));
    };
  }
};

// The ite rewrites live in this pass rather than in TermManager, so that they
// can be measured and switched off. Lead 16's own falsifier requires the
// if-then-else techniques to be evaluated together, which a constructor fold
// makes impossible.
void simplification_rewrites_ite_structurally() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  const auto y_symbol = symbols.declare_constant("y", int_sort);
  const auto z_symbol = symbols.declare_constant("z", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId q = terms.mk_constant(q_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);
  const primo::core::TermId z = terms.mk_constant(z_symbol);
  const primo::core::TermId zero = terms.mk_rational(primo::util::Rational(0), int_sort);

  const auto simplify = [&terms](primo::core::TermId assertion) {
    return primo::smt::simplify_extended_terms(
               terms, {assertion},
               primo::smt::SimplifyExtendedTermsOptions{.distribute_equalities = true})
        .assertions.at(0);
  };

  // Equal branches make the condition irrelevant.
  check(simplify(terms.mk_le(terms.mk_ite(p, x, x), zero)) == terms.mk_le(x, zero));
  // A negated condition is normalized away, merging the two spellings.
  check(simplify(terms.mk_le(terms.mk_ite(terms.mk_not(p), x, y), zero)) ==
        simplify(terms.mk_le(terms.mk_ite(p, y, x), zero)));
  // A nested ite on the same condition has already decided that branch.
  check(simplify(terms.mk_le(terms.mk_ite(p, terms.mk_ite(p, x, y), z), zero)) ==
        simplify(terms.mk_le(terms.mk_ite(p, x, z), zero)));
  // A Boolean ite is a connective.
  check(simplify(terms.mk_ite(p, terms.mk_bool(true), q)) == terms.mk_or({p, q}));
  check(simplify(terms.mk_ite(p, q, terms.mk_bool(false))) == terms.mk_and({p, q}));
  check(simplify(terms.mk_ite(p, p, q)) == terms.mk_or({p, q}));
}

// (= u (ite c a b)) distributes over the branches, which is what stops the
// lowering pass introducing an auxiliary for that shape.
void simplification_distributes_equality_over_an_ite() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto u_symbol = symbols.declare_constant("u", int_sort);
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  const auto y_symbol = symbols.declare_constant("y", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId u = terms.mk_constant(u_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);

  const std::vector<primo::core::TermId> assertions{terms.mk_eq(u, terms.mk_ite(p, x, y))};
  const primo::smt::SimplifyExtendedTermsResult simplified = primo::smt::simplify_extended_terms(
      terms, assertions, primo::smt::SimplifyExtendedTermsOptions{.distribute_equalities = true});

  check(simplified.equalities_distributed == 1);
  check(simplified.assertions.at(0) ==
        terms.mk_and({terms.mk_implies(p, terms.mk_eq(u, x)),
                      terms.mk_implies(terms.mk_not(p), terms.mk_eq(u, y))}));

  // No ite survives, so lowering introduces no auxiliary at all.
  AuxiliaryFactory auxiliaries{symbols, terms};
  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, simplified.assertions, auxiliaries.factory());
  check(lowered.auxiliary_ites == 0);
  check(auxiliaries.next == 0);
}

// Distribution is sound for any ITE but profitable only for an unshared one.
// Lowering pays once per hash-consed ITE and then one equality per parent;
// distribution pays the whole guard structure per parent. So a shared ITE is
// left alone and the auxiliary is allowed to happen.
void simplification_does_not_distribute_a_shared_ite() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto u_symbol = symbols.declare_constant("u", int_sort);
  const auto v_symbol = symbols.declare_constant("v", int_sort);
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  const auto y_symbol = symbols.declare_constant("y", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId u = terms.mk_constant(u_symbol);
  const primo::core::TermId v = terms.mk_constant(v_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);
  const primo::core::TermId shared = terms.mk_ite(p, x, y);

  // The same hash-consed ite under two equalities.
  const std::vector<primo::core::TermId> assertions{terms.mk_eq(u, shared), terms.mk_eq(v, shared)};
  const primo::smt::SimplifyExtendedTermsResult simplified = primo::smt::simplify_extended_terms(
      terms, assertions, primo::smt::SimplifyExtendedTermsOptions{.distribute_equalities = true});
  check(simplified.equalities_distributed == 0);
  check(simplified.equality_distributions_blocked == 2);
  check(simplified.assertions == assertions);

  // One auxiliary covers both parents, which is the trade being protected.
  AuxiliaryFactory auxiliaries{symbols, terms};
  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, simplified.assertions, auxiliaries.factory());
  check(lowered.auxiliary_ites == 1);
}

// Distribution only fires when it actually retires the ITE. If a branch is
// itself an ITE, (= u (ite c a (ite d e f))) hands back (= u (ite d e f)),
// which needs the auxiliary anyway: the guard structure is paid and nothing is
// collected. On nec-smt, whose ITEs are nested chains, dropping this test made
// the rule fire 18,867 times to remove 20 auxiliaries.
// Constant if-then-else simplification, open-leads lead 16 piece 1.
// Bromberger sect. 8.2.3 / Kim-Somenzi-Jin: an atom comparing a constant with a
// term built only from numbers and ite is decided branch by branch.
void constant_ite_atoms_collapse() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId q = terms.mk_constant(q_symbol);
  const auto number = [&](int value) {
    return terms.mk_rational(primo::util::Rational(value), int_sort);
  };

  // (ite p 3 (ite q 5 7)) has leaves {3,5,7}.
  const primo::core::TermId inner = terms.mk_ite(q, number(5), number(7));
  const primo::core::TermId cite = terms.mk_ite(p, number(3), inner);

  // Every leaf is <= 10, so the atom is true outright.
  {
    const primo::smt::SimplifyConstantItesResult simplified =
        primo::smt::simplify_constant_ites(terms, {terms.mk_le(cite, number(10))});
    check(simplified.assertions.at(0) == terms.mk_bool(true));
    check(simplified.atoms_simplified == 1);
  }
  // No leaf is <= 1, so it is false outright.
  {
    const primo::smt::SimplifyConstantItesResult simplified =
        primo::smt::simplify_constant_ites(terms, {terms.mk_le(cite, number(1))});
    check(simplified.assertions.at(0) == terms.mk_bool(false));
  }
  // Mixed: 3 <= 4 holds and 5, 7 do not, so the comparison is pushed inside and
  // each branch decided. The whole atom becomes exactly the condition.
  {
    const primo::smt::SimplifyConstantItesResult simplified =
        primo::smt::simplify_constant_ites(terms, {terms.mk_le(cite, number(4))});
    check(simplified.assertions.at(0) ==
          terms.mk_ite(p, terms.mk_bool(true), terms.mk_bool(false)));
  }
  // A scale is hoisted rather than divided out, which keeps it sound over the
  // integers: (* 2 cite) has leaves {6,10,14} and none is <= 5.
  {
    const primo::core::TermId scaled = terms.mk_times(primo::util::Rational(2), cite);
    const primo::smt::SimplifyConstantItesResult simplified =
        primo::smt::simplify_constant_ites(terms, {terms.mk_le(scaled, number(5))});
    check(simplified.assertions.at(0) == terms.mk_bool(false));
  }
  // An offset likewise: (+ 100 cite) has leaves {103,105,107}, all > 10.
  {
    const primo::core::TermId shifted = terms.mk_plus({number(100), cite});
    const primo::smt::SimplifyConstantItesResult simplified =
        primo::smt::simplify_constant_ites(terms, {terms.mk_le(number(10), shifted)});
    check(simplified.assertions.at(0) == terms.mk_bool(true));
  }
}

// Compare the exact reconstructed term against an independently evaluated
// three-leaf decision tree, including equality membership and both operand
// orientations. Negative scales reverse the endpoint order; rational thresholds
// exercise exact comparisons rather than floating-point approximations.
void constant_ite_comparisons_match_leaf_decisions() {
  using primo::core::TermId;
  using primo::core::TermKind;
  using primo::util::Rational;
  for (const bool real : {false, true}) {
    primo::core::SymbolTable symbols;
    const auto sort = real ? symbols.real_sort() : symbols.int_sort();
    const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
    const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
    primo::core::TermManager terms(symbols);
    const TermId p = terms.mk_constant(p_symbol);
    const TermId q = terms.mk_constant(q_symbol);
    const auto number = [&](const Rational& value) { return terms.mk_rational(value, sort); };
    const std::vector<Rational> original{Rational(-3, real ? 2 : 1), Rational(-1), Rational(2)};
    const TermId tail = terms.mk_ite(q, number(original[1]), number(original[2]));
    const TermId cite = terms.mk_ite(p, number(original[0]), tail);
    const Rational offset(1, real ? 3 : 1);
    for (const Rational& scale : std::vector<Rational>{Rational(-2), Rational(0), Rational(1),
                                                       real ? Rational(3, 2) : Rational(3)}) {
      const TermId scaled = terms.mk_times(scale, cite);
      const TermId expression = terms.mk_plus({number(offset), scaled});
      std::vector<Rational> values;
      for (const Rational& value : original) {
        values.push_back(offset + scale * value);
      }
      std::vector<Rational> constants = values;
      constants.insert(constants.end(), {Rational(-100), Rational(0), Rational(1), Rational(100)});
      if (real) {
        constants.push_back((values[0] + values[1]) / Rational(2));
      }
      for (const Rational& constant : constants) {
        for (const TermKind op : {TermKind::Le, TermKind::Lt, TermKind::Equality}) {
          for (const bool constant_on_left : {false, true}) {
            const TermId numeral = number(constant);
            const TermId lhs = constant_on_left ? numeral : expression;
            const TermId rhs = constant_on_left ? expression : numeral;
            const TermId atom = op == TermKind::Le   ? terms.mk_le(lhs, rhs)
                                : op == TermKind::Lt ? terms.mk_lt(lhs, rhs)
                                                     : terms.mk_eq(lhs, rhs);
            const auto decide = [&](const Rational& value) {
              const Rational a = constant_on_left ? constant : value;
              const Rational b = constant_on_left ? value : constant;
              return op == TermKind::Le ? a <= b : op == TermKind::Lt ? a < b : a == b;
            };
            const bool a = decide(values[0]);
            const bool b = decide(values[1]);
            const bool c = decide(values[2]);
            const TermId tail_result =
                b == c ? terms.mk_bool(b) : terms.mk_ite(q, terms.mk_bool(b), terms.mk_bool(c));
            const TermId expected = scale.is_zero() ? atom
                                    : a == b && b == c
                                        ? terms.mk_bool(a)
                                        : terms.mk_ite(p, terms.mk_bool(a), tail_result);
            const auto result = primo::smt::simplify_constant_ites(terms, {atom});
            check(result.assertions.at(0) == expected);
          }
        }
      }
    }
    // A top-level singleton leaf set is deliberately left to ordinary constant
    // folding, exactly as before. Singleton descendants are exercised above.
    const TermId singleton = terms.mk_ite(p, number(Rational(2)), number(Rational(2)));
    const TermId atom = terms.mk_lt(singleton, number(Rational(3)));
    const auto result = primo::smt::simplify_constant_ites(terms, {atom});
    check(result.assertions.at(0) == atom);
    check(result.uniformity_checks == 0);
  }
}

void constant_ite_uniformity_reuses_shared_subterms() {
  primo::core::SymbolTable symbols;
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  primo::core::TermManager terms(symbols);
  const auto p = terms.mk_constant(p_symbol);
  const auto q = terms.mk_constant(q_symbol);
  const auto low = terms.mk_rational(primo::util::Rational(-1), symbols.int_sort());
  const auto high = terms.mk_rational(primo::util::Rational(2), symbols.int_sort());
  const auto tail = terms.mk_ite(q, low, high);
  const auto cite = terms.mk_ite(p, tail, tail);
  const auto atom = terms.mk_lt(cite, high);
  const auto once = primo::smt::simplify_constant_ites(terms, {atom});
  const auto twice = primo::smt::simplify_constant_ites(terms, {atom, atom});
  const auto branch = terms.mk_ite(q, terms.mk_bool(true), terms.mk_bool(false));
  const auto expected = terms.mk_ite(p, branch, branch);
  check(once.assertions.at(0) == expected);
  check(twice.assertions == std::vector<primo::core::TermId>({expected, expected}));
  check(once.atoms_simplified == 1 && twice.atoms_simplified == 1);
  check(once.branches_collapsed == 2 && twice.branches_collapsed == 2);
  // The shared tail is classified on first entry and after its children; the
  // duplicate sibling and assertion both hit existing memoization.
  check(once.uniformity_checks == 6 && twice.uniformity_checks == 6);
  check(once.leaf_comparisons == 10 && twice.leaf_comparisons == 10);
}

void constant_ite_large_leaf_comparison_work_is_bounded() {
  using primo::core::TermId;
  using primo::util::Rational;
  for (const std::size_t count : {8U, 64U, 512U, 4096U}) {
    primo::core::SymbolTable symbols;
    const auto symbol = symbols.declare_constant("p", symbols.bool_sort());
    primo::core::TermManager terms(symbols);
    const auto p = terms.mk_constant(symbol);
    const auto number = [&](long value) {
      return terms.mk_rational(Rational(value), symbols.int_sort());
    };
    std::vector<TermId> layer;
    for (std::size_t index = 0; index < count; ++index) {
      layer.push_back(number(static_cast<long>(index * 2)));
    }
    while (layer.size() > 1) {
      std::vector<TermId> next;
      for (std::size_t index = 0; index < layer.size(); index += 2) {
        next.push_back(terms.mk_ite(p, layer[index], layer[index + 1]));
      }
      layer = std::move(next);
    }
    const auto cite = layer.front();
    for (const bool constant_on_left : {false, true}) {
      const auto constant = number(constant_on_left ? -1 : static_cast<long>(count * 2));
      for (const bool strict : {false, true}) {
        const auto lhs = constant_on_left ? constant : cite;
        const auto rhs = constant_on_left ? cite : constant;
        const auto atom = strict ? terms.mk_lt(lhs, rhs) : terms.mk_le(lhs, rhs);
        const auto result = primo::smt::simplify_constant_ites(terms, {atom});
        check(result.assertions.at(0) == terms.mk_bool(true));
        check(result.uniformity_checks == 1);
        check(result.leaf_scan_equivalent == count);
        check(result.leaf_comparisons == 2);
      }
      // An absent value inside the endpoints requires membership, not an
      // endpoint test. A full scan would compare all count leaves.
      const auto absent = number(1);
      const auto atom = constant_on_left ? terms.mk_eq(absent, cite) : terms.mk_eq(cite, absent);
      const auto result = primo::smt::simplify_constant_ites(terms, {atom});
      check(result.assertions.at(0) == terms.mk_bool(false));
      check(result.uniformity_checks == 1);
      check(result.leaf_scan_equivalent == count);
      check(result.leaf_comparisons <= std::bit_width(count) + 1);
    }
  }
}

// Original ownership regression: mixed leaves force hoisting, unlike the
// all-true/all-false scale checks above, which collapse before constructing terms.
void constant_ite_hoisting_survives_arena_growth() {
  for (const bool real : {false, true}) {
    for (const bool sum : {false, true}) {
      for (const bool nested : {false, true}) {
        primo::core::SymbolTable symbols;
        const auto sort = real ? symbols.real_sort() : symbols.int_sort();
        const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
        const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
        primo::core::TermManager terms(symbols);
        const auto number = [&](int value) {
          return terms.mk_rational(primo::util::Rational(value), sort);
        };
        const auto p = terms.mk_constant(p_symbol);
        const auto q = terms.mk_constant(q_symbol);
        const auto two = number(2);
        const auto five = number(5);
        const auto inner = nested ? terms.mk_ite(q, five, two) : five;
        const auto cite = terms.mk_ite(p, two, inner);
        const auto expression =
            sum ? terms.mk_plus({number(3), cite}) : terms.mk_times(primo::util::Rational(3), cite);
        const auto assertion = terms.mk_le(expression, number(sum ? 6 : 9));
        const auto yes = terms.mk_bool(true);
        const auto no = terms.mk_bool(false);
        const auto expected = terms.mk_ite(p, yes, nested ? terms.mk_ite(q, no, yes) : no);

        // Fill the current allocation with distinct, unreachable numerals.
        // The first fresh hoisted branch must grow it, independent of the
        // vector's growth factor. No borrowed reference crosses the call.
        const std::size_t capacity = terms.terms().capacity();
        for (int filler = 1000; terms.size() < capacity; ++filler) {
          number(filler);
        }
        const auto simplified = primo::smt::simplify_constant_ites(terms, {assertion});
        check(terms.terms().capacity() > capacity);
        check(simplified.atoms_simplified == 1);
        check(simplified.assertions.at(0) == expected);
      }
    }
  }
}

// A term that is not a CITE must be left exactly alone.
void non_constant_ite_atoms_are_untouched() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId five = terms.mk_rational(primo::util::Rational(5), int_sort);
  // A variable leaf makes this not a CITE.
  const primo::core::TermId assertion =
      terms.mk_le(terms.mk_ite(p, x, terms.mk_rational(primo::util::Rational(3), int_sort)), five);
  const primo::smt::SimplifyConstantItesResult simplified =
      primo::smt::simplify_constant_ites(terms, {assertion});
  check(simplified.assertions.at(0) == assertion);
  check(simplified.atoms_simplified == 0);
}

void simplification_does_not_distribute_a_nested_ite() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  const auto u_symbol = symbols.declare_constant("u", int_sort);
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  const auto y_symbol = symbols.declare_constant("y", int_sort);
  const auto z_symbol = symbols.declare_constant("z", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId q = terms.mk_constant(q_symbol);
  const primo::core::TermId u = terms.mk_constant(u_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);
  const primo::core::TermId z = terms.mk_constant(z_symbol);

  const std::vector<primo::core::TermId> nested{
      terms.mk_eq(u, terms.mk_ite(p, x, terms.mk_ite(q, y, z)))};
  const primo::smt::SimplifyExtendedTermsResult held = primo::smt::simplify_extended_terms(
      terms, nested, primo::smt::SimplifyExtendedTermsOptions{});
  check(held.equalities_distributed == 0);
  check(held.equality_distributions_nested == 1);
  check(held.assertions == nested);

  // More rounds do not route around it: the guard is on the shape, not on how
  // far the traversal has got. Running this rule to closure is exponential.
  const primo::smt::SimplifyExtendedTermsResult repeated = primo::smt::simplify_extended_terms(
      terms, nested, primo::smt::SimplifyExtendedTermsOptions{.rounds = 4});
  check(repeated.equalities_distributed == 0);
  check(repeated.assertions == nested);

  // The flat shape is the one the rule is for, and it still fires: no
  // auxiliary is needed at all.
  const std::vector<primo::core::TermId> flat{terms.mk_eq(u, terms.mk_ite(p, x, y))};
  const primo::smt::SimplifyExtendedTermsResult distributed =
      primo::smt::simplify_extended_terms(terms, flat, primo::smt::SimplifyExtendedTermsOptions{});
  check(distributed.equalities_distributed == 1);
  check(distributed.equality_distributions_nested == 0);
  AuxiliaryFactory auxiliaries{symbols, terms};
  check(primo::smt::lower_extended_terms(terms, distributed.assertions, auxiliaries.factory())
            .auxiliary_ites == 0);

  // Zero rounds is the leaf switched off.
  const primo::smt::SimplifyExtendedTermsResult none = primo::smt::simplify_extended_terms(
      terms, flat, primo::smt::SimplifyExtendedTermsOptions{.rounds = 0});
  check(none.rounds == 0);
  check(none.assertions == flat);
}

void simplification_rewrites_div_mod_and_abs() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId zero = terms.mk_rational(primo::util::Rational(0), int_sort);
  const auto simplify = [&terms](primo::core::TermId assertion) {
    return primo::smt::simplify_extended_terms(
               terms, {assertion},
               primo::smt::SimplifyExtendedTermsOptions{.distribute_equalities = true})
        .assertions.at(0);
  };

  // D = 1*D + 0, with the remainder in [0,1).
  check(simplify(terms.mk_le(terms.mk_int_div(primo::util::Rational(1), x), zero)) ==
        terms.mk_le(x, zero));
  check(simplify(terms.mk_le(terms.mk_int_mod(primo::util::Rational(1), x), zero)) ==
        terms.mk_le(zero, zero));

  // A remainder is already in [0,k): mod is idempotent, div gives zero, and
  // abs is the identity on it.
  const primo::core::TermId remainder = terms.mk_int_mod(primo::util::Rational(3), x);
  check(simplify(terms.mk_le(terms.mk_int_mod(primo::util::Rational(3), remainder), zero)) ==
        simplify(terms.mk_le(remainder, zero)));
  check(simplify(terms.mk_le(terms.mk_int_div(primo::util::Rational(3), remainder), zero)) ==
        terms.mk_le(zero, zero));
  check(simplify(terms.mk_le(terms.mk_abs(remainder), zero)) ==
        simplify(terms.mk_le(remainder, zero)));
  check(simplify(terms.mk_le(terms.mk_abs(terms.mk_abs(x)), zero)) ==
        simplify(terms.mk_le(terms.mk_abs(x), zero)));
}

void lowering_is_a_no_op_when_nothing_extended_was_built() {
  Fixture fixture;
  AuxiliaryFactory auxiliaries{fixture.symbols, fixture.terms};
  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_eq(fixture.constant(fixture.a), fixture.constant(fixture.b))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(fixture.terms, assertions, auxiliaries.factory());

  check(lowered.assertions == assertions);
  check(lowered.auxiliary_ites == 0);
  check(auxiliaries.next == 0);
}

void lowering_defines_a_term_ite_with_two_guarded_equalities() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  const auto x = symbols.declare_constant("x", int_sort);
  const auto y = symbols.declare_constant("y", int_sort);
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  const primo::core::TermId condition = terms.mk_constant(p);
  const primo::core::TermId conditional =
      terms.mk_ite(condition, terms.mk_constant(x), terms.mk_constant(y));
  const std::vector<primo::core::TermId> assertions{
      terms.mk_le(conditional, terms.mk_rational(primo::util::Rational(3), int_sort))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, assertions, auxiliaries.factory());

  // Two definitions, then the rewritten assertion.
  check(lowered.auxiliary_ites == 1);
  check(lowered.boolean_ites == 0);
  check(lowered.assertions.size() == 3);
  check(terms.get(lowered.assertions.at(0)).kind == primo::core::TermKind::Implies);
  check(terms.get(lowered.assertions.at(1)).kind == primo::core::TermKind::Implies);
  // No extended kind survives.
  for (const primo::core::TermId assertion : lowered.assertions) {
    std::vector<primo::core::TermId> stack{assertion};
    while (!stack.empty()) {
      const primo::core::TermId id = stack.back();
      stack.pop_back();
      const primo::core::Term& node = terms.get(id);
      check(node.kind != primo::core::TermKind::Ite);
      for (const primo::core::TermId child : node.children) {
        stack.push_back(child);
      }
    }
  }
}

void lowering_expands_a_boolean_ite_without_an_auxiliary() {
  primo::core::SymbolTable symbols;
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  const auto q = symbols.declare_constant("q", symbols.bool_sort());
  const auto r = symbols.declare_constant("r", symbols.bool_sort());
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  const std::vector<primo::core::TermId> assertions{
      terms.mk_ite(terms.mk_constant(p), terms.mk_constant(q), terms.mk_constant(r))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, assertions, auxiliaries.factory());

  check(lowered.boolean_ites == 1);
  check(lowered.auxiliary_ites == 0);
  check(auxiliaries.next == 0);
  check(lowered.assertions.size() == 1);
  check(terms.get(lowered.assertions.at(0)).kind == primo::core::TermKind::Or);
}

void lowering_shares_one_quotient_between_div_and_mod() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto x = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  const primo::core::TermId numerator = terms.mk_constant(x);
  const std::vector<primo::core::TermId> assertions{
      terms.mk_le(terms.mk_int_div(primo::util::Rational(3), numerator),
                  terms.mk_rational(primo::util::Rational(5), int_sort)),
      terms.mk_le(terms.mk_int_mod(primo::util::Rational(3), numerator),
                  terms.mk_rational(primo::util::Rational(2), int_sort))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, assertions, auxiliaries.factory());

  // One quotient, one pair of bounds, shared by both assertions.
  check(lowered.quotients == 1);
  check(auxiliaries.next == 1);
  // Two bound definitions, then the two rewritten assertions.
  check(lowered.assertions.size() == 4);
}

void lowering_rewrites_abs_through_the_ite_encoding() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto x = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  const std::vector<primo::core::TermId> assertions{terms.mk_le(
      terms.mk_abs(terms.mk_constant(x)), terms.mk_rational(primo::util::Rational(4), int_sort))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, assertions, auxiliaries.factory());

  check(lowered.abs_rewrites == 1);
  check(lowered.auxiliary_ites == 1);
  check(lowered.assertions.size() == 3);
}

void lowering_prepares_new_modulo_terms_in_later_batches() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto symbol = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  const auto x = terms.mk_constant(symbol);
  const auto zero = terms.mk_rational(primo::util::Rational(0), int_sort);
  const auto quotient = terms.mk_int_div(primo::util::Rational(3), x);
  const auto first_root = terms.mk_le(zero, quotient);
  const auto first = primo::smt::lower_extended_terms(terms, {first_root}, auxiliaries.factory());
  check(first.quotients == 1 && first.assertions.size() == 3);

  // This mod did not exist during the first call. Its defining quotient and
  // both bounds must be emitted when the later formula is admitted.
  const auto remainder = terms.mk_int_mod(primo::util::Rational(5), x);
  const auto later_root = terms.mk_le(remainder, zero);
  const auto later = primo::smt::lower_extended_terms(terms, {later_root}, auxiliaries.factory());
  check(later.quotients == 1 && later.assertions.size() == 3);
  check(auxiliaries.next == 2);

  // Re-admitting the same term cannot reuse a quotient whose constraints were
  // admitted in an earlier, possibly popped scope.
  const auto again = primo::smt::lower_extended_terms(terms, {first_root}, auxiliaries.factory());
  check(again.quotients == 1 && again.assertions.size() == 3);
  check(auxiliaries.next == 3);
  for (const auto& batch : {first, later, again}) {
    std::vector<primo::core::TermId> pending = batch.assertions;
    while (!pending.empty()) {
      const auto term = terms.get(pending.back());
      pending.pop_back();
      check(term.kind != primo::core::TermKind::IntDiv);
      check(term.kind != primo::core::TermKind::IntMod);
      if (term.kind == primo::core::TermKind::Constant) check(term.sort == int_sort);
      pending.insert(pending.end(), term.children.begin(), term.children.end());
    }
  }
}

// The walk is an explicit stack, so nesting depth is bounded by memory rather
// than by the C++ stack.
void lowering_handles_deeply_nested_terms() {
  primo::core::SymbolTable symbols;
  const auto int_sort = symbols.int_sort();
  const auto x = symbols.declare_constant("x", int_sort);
  primo::core::TermManager terms(symbols);
  AuxiliaryFactory auxiliaries{symbols, terms};
  primo::core::TermId nested = terms.mk_constant(x);
  constexpr std::size_t kDepth = 20000;
  for (std::size_t depth = 0; depth < kDepth; ++depth) {
    nested = terms.mk_abs(
        terms.mk_plus({nested, terms.mk_rational(primo::util::Rational(1), int_sort)}));
  }
  const std::vector<primo::core::TermId> assertions{
      terms.mk_le(nested, terms.mk_rational(primo::util::Rational(0), int_sort))};

  const primo::smt::LowerExtendedTermsResult lowered =
      primo::smt::lower_extended_terms(terms, assertions, auxiliaries.factory());

  check(lowered.abs_rewrites == kDepth);
  check(lowered.auxiliary_ites == kDepth);
}

}  // namespace

int main() {
  arithmetic_atom_canonicalization_merges_scaled_aliases();
  arithmetic_atom_canonicalization_handles_sign_strictness_and_rationals();
  arithmetic_atom_canonicalization_orients_equalities_and_folds_constants();
  arithmetic_atom_canonicalization_rewrites_nested_atoms_and_preserves_euf_equalities();
  bounded_boolean_flattening_preserves_threshold_and_handles_deep_dags();
  implicit_conjunction_analysis_ignores_assertion_packaging();
  implicit_conjunction_analysis_recognizes_clause_implications();
  implication_clause_rewrite_flattens_bounded_literal_consequents();
  implication_rewrite_preserves_compound_clause_mode_and_shared_dags();
  implication_rewrite_enters_boolean_equality_but_not_uf_application_arguments();
  boolean_nnf_pushes_negations_without_distribution();
  boolean_nnf_memoizes_both_polarities_and_keeps_uf_atoms_opaque();
  finite_domain_analysis_is_non_materializing();
  integer_numeral_domains_enable_amo_and_equality_channeling_without_symmetry();
  real_or_mixed_value_lists_do_not_gain_semantic_distinctness();
  symmetric_fixed_terms_get_value_precedence();
  asymmetric_value_use_is_rejected();
  symmetry_comparison_preserves_numeric_payloads();
  moving_only_symmetry_terms_are_rejected();
  mixed_fixed_and_moving_terms_use_fixed_value_precedence_only();
  equality_bridge_derives_common_branch_consequence();
  unit_equality_propagation_simplifies_disequality();
  unit_equality_propagation_simplifies_nested_boolean_terms_after_interning();
  unit_equality_propagation_skips_without_nonunit_queries();
  unit_equality_propagation_visits_shared_query_dag_once();
  ground_unit_substitution_rewrites_applications_and_preserves_units();
  ground_unit_substitution_uses_conjunctive_units_and_drops_true_parts();
  solved_constant_substitution_eliminates_acyclic_definition();
  solved_constant_substitution_rejects_cycles();
  solved_constant_substitution_eliminates_real_equality_definition();
  gaussian_elimination_solves_joint_linear_system();
  gaussian_elimination_solves_single_equation_with_coefficient();
  gaussian_elimination_bails_out_above_equation_cap();
  lower_real_equalities_rewrites_real_sorted_equality_only();
  lower_real_equalities_distributes_single_guarded_real_equality();
  lower_real_equalities_keeps_guarded_distribution_disabled_by_default();
  lower_real_equalities_rewrites_guard_before_distribution();
  lower_real_equalities_does_not_distribute_negated_real_equality();
  lower_real_equalities_does_not_distribute_uninterpreted_equality();
  lower_real_equalities_does_not_overdistribute_multiple_guarded_equalities();
  boolean_unit_propagation_simplifies_boolean_structure_and_arguments();
  boolean_unit_propagation_preserves_contradictory_units();
  boolean_unit_propagation_skips_when_no_units_exist();
  boolean_unit_simplification_memoizes_shared_boolean_subterms();
  forced_atom_simplification_memoizes_shared_boolean_subterms();
  forced_atom_simplification_handles_deep_boolean_terms();
  forced_atom_propagation_simplifies_predicate_units();
  forced_atom_propagation_uses_negative_equality_units();
  forced_atom_propagation_folds_disequalities_modulo_equalities();
  forced_atom_propagation_folds_transitive_disequalities();
  forced_atom_propagation_preserves_contradictory_exact_units();
  finite_domain_at_most_one_adds_pairwise_clauses();
  finite_domain_choices_may_be_nested_or_nodes();
  finite_domain_observed_equalities_get_pairwise_amo_clauses_without_choices();
  finite_domain_observed_equalities_use_nonconstant_disequality_endpoints();
  finite_domain_distinct_endpoints_get_value_amo_clauses();
  finite_domain_distinct_endpoints_use_additional_forced_equalities();
  finite_domain_equality_definitions_connect_terms_with_same_domain();
  finite_domain_equality_definitions_require_observed_term_equality();
  finite_domain_equality_definitions_respect_auto_guards();
  congruence_definitions_connect_same_symbol_applications();
  finite_domain_evaluator_simplifies_table_tautology();
  monotone_elimination_discharges_one_sided_variable();
  monotone_elimination_respects_polarity();
  monotone_elimination_folds_negative_polarity_atom_to_false();
  simplification_rewrites_ite_structurally();
  simplification_distributes_equality_over_an_ite();
  simplification_does_not_distribute_a_shared_ite();
  constant_ite_atoms_collapse();
  constant_ite_comparisons_match_leaf_decisions();
  constant_ite_uniformity_reuses_shared_subterms();
  constant_ite_large_leaf_comparison_work_is_bounded();
  constant_ite_hoisting_survives_arena_growth();
  non_constant_ite_atoms_are_untouched();
  simplification_does_not_distribute_a_nested_ite();
  simplification_rewrites_div_mod_and_abs();
  lowering_is_a_no_op_when_nothing_extended_was_built();
  lowering_defines_a_term_ite_with_two_guarded_equalities();
  lowering_expands_a_boolean_ite_without_an_auxiliary();
  lowering_shares_one_quotient_between_div_and_mod();
  lowering_prepares_new_modulo_terms_in_later_batches();
  lowering_rewrites_abs_through_the_ite_encoding();
  lowering_handles_deeply_nested_terms();
  monotone_elimination_keeps_variable_pinned_by_equality();
  monotone_elimination_keeps_variable_under_uninterpreted_application();
  monotone_elimination_orders_dependent_variables();
  monotone_elimination_leaves_two_sided_variables_alone();
  return 0;
}
