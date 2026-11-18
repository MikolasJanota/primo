#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

using primo::core::SortKind;
using primo::core::SymbolKind;
using primo::core::SymbolTable;
using primo::core::TermKind;
using primo::core::TermManager;
using primo::util::Rational;
using primo::util::Statistics;
using primo::util::StatKey;

namespace {

template <typename Fn>
void expect_invalid_argument(Fn fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  assert(false && "expected std::invalid_argument");
}

void symbol_table_tracks_sorts_and_symbols() {
  SymbolTable symbols;
  const auto bool_sort = symbols.bool_sort();
  assert(symbols.get_sort(bool_sort).name == "Bool");
  assert(symbols.get_sort(bool_sort).kind == SortKind::Bool);

  const auto real_sort = symbols.real_sort();
  assert(symbols.get_sort(real_sort).name == "Real");
  assert(symbols.get_sort(real_sort).kind == SortKind::Real);
  assert(symbols.lookup_sort("Real") == real_sort);

  const auto sort_a = symbols.declare_sort("A");
  assert(symbols.lookup_sort("A") == sort_a);
  assert(symbols.get_sort(sort_a).kind == SortKind::Uninterpreted);

  const auto a = symbols.declare_constant("a", sort_a);
  const auto f = symbols.declare_function("f", {sort_a}, sort_a);
  const auto p = symbols.declare_predicate("p", {sort_a});

  assert(symbols.lookup_symbol("a") == a);
  assert(symbols.get_symbol(a).kind == SymbolKind::Constant);
  assert(symbols.get_symbol(f).arg_sorts == std::vector{sort_a});
  assert(symbols.get_symbol(p).result_sort == bool_sort);

  expect_invalid_argument([&] { symbols.declare_sort("A"); });
  expect_invalid_argument([&] { symbols.declare_constant("a", sort_a); });
}

void term_manager_hash_conses_and_checks_sorts() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto sort_b = symbols.declare_sort("B");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_b);
  const auto f_symbol = symbols.declare_function("f", {sort_a}, sort_a);
  const auto pred_symbol = symbols.declare_predicate("p", {sort_a});

  TermManager terms(symbols);
  const auto a0 = terms.mk_constant(a_symbol);
  const auto a1 = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  assert(a0 == a1);
  assert(terms.get(a0).sort == sort_a);
  assert(terms.size() == 2);

  const auto f_a0 = terms.mk_app(f_symbol, {a0});
  const auto f_a1 = terms.mk_app(f_symbol, {a1});
  assert(f_a0 == f_a1);
  assert(terms.get(f_a0).kind == TermKind::Application);

  const auto p_a = terms.mk_app(pred_symbol, {a0});
  assert(terms.get(p_a).sort == symbols.bool_sort());

  const auto eq0 = terms.mk_eq(a0, f_a0);
  const auto eq1 = terms.mk_eq(a1, f_a1);
  assert(eq0 == eq1);
  assert(terms.get(eq0).kind == TermKind::Equality);
  assert(terms.get(eq0).sort == symbols.bool_sort());

  const auto negated_eq = terms.mk_not(eq0);
  assert(terms.get(negated_eq).children == std::vector{eq0});

  const auto conjunction = terms.mk_and({eq0, negated_eq, p_a});
  assert(terms.get(conjunction).children.size() == 3);

  expect_invalid_argument([&] { terms.mk_eq(a0, b); });
  expect_invalid_argument([&] { terms.mk_app(f_symbol, {b}); });
  expect_invalid_argument([&] { terms.mk_not(a0); });
  expect_invalid_argument([&] { terms.mk_app(a_symbol, {}); });
}

void Boolean_terms_are_hash_consed() {
  SymbolTable symbols;
  TermManager terms(symbols);

  const auto t0 = terms.mk_bool(true);
  const auto t1 = terms.mk_bool(true);
  const auto f0 = terms.mk_bool(false);
  assert(t0 == t1);
  assert(t0 != f0);
  assert(terms.get(t0).bool_value);
  assert(!terms.get(f0).bool_value);
}

void trivial_boolean_terms_are_simplified() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  TermManager terms(symbols);

  const auto a = terms.mk_constant(a_symbol);
  const auto p = terms.mk_constant(p_symbol);
  const auto q = terms.mk_constant(q_symbol);
  const auto t = terms.mk_bool(true);
  const auto f = terms.mk_bool(false);

  assert(terms.mk_eq(a, a) == t);
  assert(terms.mk_not(t) == f);
  assert(terms.mk_not(terms.mk_not(p)) == p);
  assert(terms.mk_and({t, p}) == p);
  assert(terms.mk_and({p, f, q}) == f);
  assert(terms.mk_or({f, p}) == p);
  assert(terms.mk_or({p, t, q}) == t);
  assert(terms.mk_implies(t, p) == p);
  assert(terms.mk_implies(f, p) == t);
  assert(terms.mk_implies(p, f) == terms.mk_not(p));
  assert(terms.mk_implies(p, p) == t);
}

void associative_boolean_terms_preserve_shared_dag_nodes() {
  SymbolTable symbols;
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  const auto r_symbol = symbols.declare_constant("r", symbols.bool_sort());
  TermManager terms(symbols);
  const auto p = terms.mk_constant(p_symbol);
  const auto q = terms.mk_constant(q_symbol);
  const auto r = terms.mk_constant(r_symbol);

  const auto inner_and = terms.mk_and({p, q});
  const auto outer_and = terms.mk_and({inner_and, r});
  assert(terms.get(outer_and).children == std::vector({inner_and, r}));
  assert(terms.mk_and({p, p}) == p);
  assert(terms.get(terms.mk_and({p, q, p, r})).children == std::vector({p, q, r}));

  const auto inner_or = terms.mk_or({p, q});
  const auto outer_or = terms.mk_or({inner_or, r});
  assert(terms.get(outer_or).children == std::vector({inner_or, r}));
  assert(terms.mk_or({q, q}) == q);
  assert(terms.get(terms.mk_or({q, p, q, r})).children == std::vector({q, p, r}));
}

void arithmetic_terms_are_hash_consed_and_checked() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  TermManager terms(symbols);

  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto a = terms.mk_constant(a_symbol);

  const auto three0 = terms.mk_rational(Rational(3));
  const auto three1 = terms.mk_rational(Rational(3));
  assert(three0 == three1);
  assert(terms.get(three0).kind == TermKind::Rational);
  assert(terms.get(three0).sort == symbols.real_sort());
  assert(terms.get(three0).rational_value == Rational(3));

  const auto sum0 = terms.mk_plus({x, y, three0});
  const auto sum1 = terms.mk_plus({x, y, three1});
  assert(sum0 == sum1);
  assert(terms.get(sum0).kind == TermKind::Plus);
  assert(terms.get(sum0).sort == symbols.real_sort());

  const auto scaled_x0 = terms.mk_times(Rational(2), x);
  const auto scaled_x1 = terms.mk_times(Rational(2), x);
  assert(scaled_x0 == scaled_x1);
  assert(terms.get(scaled_x0).kind == TermKind::Times);
  assert(terms.get(scaled_x0).rational_value == Rational(2));
  assert(terms.get(scaled_x0).children == std::vector{x});

  const auto le0 = terms.mk_le(x, y);
  const auto le1 = terms.mk_le(x, y);
  assert(le0 == le1);
  assert(terms.get(le0).kind == TermKind::Le);
  assert(terms.get(le0).sort == symbols.bool_sort());

  const auto lt0 = terms.mk_lt(x, y);
  assert(terms.get(lt0).kind == TermKind::Lt);
  assert(terms.get(lt0).sort == symbols.bool_sort());
  assert(lt0 != le0);

  expect_invalid_argument([&] { terms.mk_plus({x, a}); });
  expect_invalid_argument([&] { terms.mk_times(Rational(2), a); });
  expect_invalid_argument([&] { terms.mk_le(x, a); });
  expect_invalid_argument([&] { terms.mk_plus({}); });
}

void arithmetic_terms_are_simplified() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  // Constant folding.
  assert(terms.mk_plus({terms.mk_rational(Rational(2)), terms.mk_rational(Rational(3))}) ==
         terms.mk_rational(Rational(5)));
  assert(terms.mk_times(Rational(-1), terms.mk_rational(Rational(3))) ==
         terms.mk_rational(Rational(-3)));
  assert(terms.mk_le(terms.mk_rational(Rational(2)), terms.mk_rational(Rational(3))) ==
         terms.mk_bool(true));
  assert(terms.mk_lt(terms.mk_rational(Rational(3)), terms.mk_rational(Rational(2))) ==
         terms.mk_bool(false));

  // Coefficient folding: nested Times multiplies coefficients; 0 elides to
  // the zero constant; 1 is the identity; nested Rational folds directly.
  assert(terms.mk_times(Rational(3), terms.mk_times(Rational(2), x)) ==
         terms.mk_times(Rational(6), x));
  assert(terms.mk_times(Rational(0), x) == terms.mk_rational(Rational(0)));
  assert(terms.mk_times(Rational(1), x) == x);
  assert(terms.mk_plus({x, terms.mk_rational(Rational(0))}) == x);

  // Nested Plus flattens.
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  const auto y = terms.mk_constant(y_symbol);
  const auto inner = terms.mk_plus({x, y});
  const auto outer = terms.mk_plus({inner, terms.mk_rational(Rational(1))});
  assert(terms.get(outer).children.size() == 3);

  // Same-term Le/Lt fold like mk_eq's lhs==rhs shortcut.
  assert(terms.mk_le(x, x) == terms.mk_bool(true));
  assert(terms.mk_lt(x, x) == terms.mk_bool(false));
}

void rational_primitive_integer_scale_clears_denominators_and_content() {
  const std::array integer_coefficients{Rational(6), Rational(-9), Rational(0)};
  assert(Rational::primitive_integer_scale(integer_coefficients) == Rational(1, 3));

  const std::array rational_coefficients{Rational(2, 3), Rational(-4, 5)};
  assert(Rational::primitive_integer_scale(rational_coefficients) == Rational(15, 2));
  assert(rational_coefficients[0] * Rational::primitive_integer_scale(rational_coefficients) ==
         Rational(5));
  assert(rational_coefficients[1] * Rational::primitive_integer_scale(rational_coefficients) ==
         Rational(-6));

  expect_invalid_argument([] {
    const std::array zeros{Rational(0), Rational(0)};
    (void)Rational::primitive_integer_scale(zeros);
  });
}

void statistics_preserve_fixed_and_dynamic_counters() {
  // Promoted fixed counters must retain their old position among extra keys.
  {
    Statistics ordered;
    ordered.increment_dynamic(":custom.before");
    ordered.increment(primo::util::StatKey::EufConflictAckDuplicatePairs);
    ordered.increment_dynamic(":custom.after");
    ordered.increment(primo::util::StatKey::AssertionsInitial);
    assert((ordered.keys() == std::vector<std::string>{":input.assertions", ":custom.before",
                                                       ":euf.conflict-ack.duplicate-pairs",
                                                       ":custom.after"}));
    assert(ordered.get(":euf.conflict-ack.duplicate-pairs") == 1);
  }
  Statistics statistics;
  statistics.increment(StatKey::AssertionsInitial);
  statistics.increment_dynamic(":input.assertions", 2);
  statistics.increment_dynamic(":test.dynamic", 3);
  statistics.increment_dynamic(":test.dynamic", 4);
  statistics.set_dynamic(":test.dynamic", 9);

  assert(statistics.get(StatKey::AssertionsInitial) == 3);
  assert(statistics.get(":input.assertions") == 3);
  assert(statistics.get(":test.dynamic") == 9);
  assert(statistics.contains(":input.assertions"));
  assert(statistics.contains(":test.dynamic"));
  assert(!statistics.contains(":test.missing"));
  assert(statistics.keys() == std::vector<std::string>({":input.assertions", ":test.dynamic"}));
}

// SMT-LIB fixes div and mod by Boute's Euclidean definition: for a nonzero
// divisor there are unique q, r with D = d*q + r and 0 <= r < |d|. The
// remainder is non-negative whatever the signs are, which is where Euclidean
// parts company with the floored definition Knuth promotes and with the
// truncated one C uses. The two negative-divisor rows below are the ones that
// differ, so they are pinned rather than described.
void euclidean_division_follows_the_smtlib_sign_convention() {
  SymbolTable symbols;
  TermManager terms(symbols);
  const auto int_sort = symbols.int_sort();

  struct Row {
    long long numerator;
    long long divisor;
    long long quotient;
    long long remainder;
  };
  // D = d*q + r with 0 <= r < |d| in every row.
  const std::array<Row, 8> rows{{
      {7, 3, 2, 1},
      {-7, 3, -3, 2},
      {7, -3, -2, 1},
      {-7, -3, 3, 2},
      {6, 3, 2, 0},
      {-6, 3, -2, 0},
      {0, 5, 0, 0},
      {-1, 5, -1, 4},
  }};
  for (const Row& row : rows) {
    const primo::core::TermId numerator =
        terms.mk_rational(primo::util::Rational(row.numerator), int_sort);
    const primo::core::TermId quotient =
        terms.mk_int_div(primo::util::Rational(row.divisor), numerator);
    const primo::core::TermId remainder =
        terms.mk_int_mod(primo::util::Rational(row.divisor), numerator);
    assert(terms.get(quotient).kind == TermKind::Rational);
    assert(terms.get(remainder).kind == TermKind::Rational);
    assert(terms.get(quotient).rational_value == primo::util::Rational(row.quotient));
    assert(terms.get(remainder).rational_value == primo::util::Rational(row.remainder));
    // The defining identity, restated over the folded values.
    assert(primo::util::Rational(row.numerator) ==
           primo::util::Rational(row.divisor) * primo::util::Rational(row.quotient) +
               primo::util::Rational(row.remainder));
    assert(!(primo::util::Rational(row.remainder) < primo::util::Rational(0)));
  }
}

void int_div_and_mod_normalize_the_divisor_sign() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.int_sort());
  TermManager terms(symbols);
  const primo::core::TermId x = terms.mk_constant(x_symbol);

  // modE(D, -d) = modE(D, d): the divisor sign never reaches the remainder,
  // so both spellings hash-cons to one node.
  assert(terms.mk_int_mod(primo::util::Rational(-3), x) ==
         terms.mk_int_mod(primo::util::Rational(3), x));
  const primo::core::TermId positive_mod = terms.mk_int_mod(primo::util::Rational(3), x);
  assert(terms.get(positive_mod).kind == TermKind::IntMod);
  assert(terms.get(positive_mod).rational_value == primo::util::Rational(3));
  assert(terms.get(positive_mod).children == std::vector<primo::core::TermId>{x});

  // divE(D, -d) = -divE(D, d): the sign is carried by a Times wrapper, so the
  // stored IntDiv node always has a positive divisor.
  const primo::core::TermId negative_div = terms.mk_int_div(primo::util::Rational(-3), x);
  assert(terms.get(negative_div).kind == TermKind::Times);
  assert(terms.get(negative_div).rational_value == primo::util::Rational(-1));
  const primo::core::TermId inner = terms.get(negative_div).children.at(0);
  assert(terms.get(inner).kind == TermKind::IntDiv);
  assert(terms.get(inner).rational_value == primo::util::Rational(3));
  assert(inner == terms.mk_int_div(primo::util::Rational(3), x));

  bool rejected_zero = false;
  try {
    static_cast<void>(terms.mk_int_div(primo::util::Rational(0), x));
  } catch (const std::invalid_argument&) {
    rejected_zero = true;
  }
  assert(rejected_zero);

  bool rejected_fraction = false;
  try {
    static_cast<void>(terms.mk_int_mod(primo::util::Rational(1, 2), x));
  } catch (const std::invalid_argument&) {
    rejected_fraction = true;
  }
  assert(rejected_fraction);
}

void ite_folds_only_what_is_unconditional() {
  SymbolTable symbols;
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto x_symbol = symbols.declare_constant("x", symbols.int_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.int_sort());
  TermManager terms(symbols);
  const primo::core::TermId p = terms.mk_constant(p_symbol);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const primo::core::TermId y = terms.mk_constant(y_symbol);

  // Constant folding is all the constructor does: a literal condition selects
  // a branch. Every other ite rewrite, including collapsing equal branches,
  // belongs to simplify-extended-terms so that it can be measured.
  assert(terms.mk_ite(terms.mk_bool(true), x, y) == x);
  assert(terms.mk_ite(terms.mk_bool(false), x, y) == y);
  assert(terms.get(terms.mk_ite(p, x, x)).kind == TermKind::Ite);

  // Everything else stays a term, so preprocessing can see it.
  const primo::core::TermId conditional = terms.mk_ite(p, x, y);
  assert(terms.get(conditional).kind == TermKind::Ite);
  assert(terms.get(conditional).sort == symbols.int_sort());
  assert(terms.get(conditional).children == std::vector<primo::core::TermId>({p, x, y}));
  assert(conditional == terms.mk_ite(p, x, y));

  // The branch sort is the term's sort, and Bool branches are allowed.
  const primo::core::TermId boolean_ite = terms.mk_ite(p, terms.mk_bool(true), p);
  assert(terms.get(boolean_ite).sort == symbols.bool_sort());

  bool rejected_condition = false;
  try {
    static_cast<void>(terms.mk_ite(x, x, y));
  } catch (const std::invalid_argument&) {
    rejected_condition = true;
  }
  assert(rejected_condition);

  bool rejected_branches = false;
  try {
    static_cast<void>(terms.mk_ite(p, x, terms.mk_bool(true)));
  } catch (const std::invalid_argument&) {
    rejected_branches = true;
  }
  assert(rejected_branches);
}

void abs_folds_literals_and_counts_as_an_extended_term() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.int_sort());
  const auto r_symbol = symbols.declare_constant("r", symbols.real_sort());
  TermManager terms(symbols);
  const primo::core::TermId x = terms.mk_constant(x_symbol);
  const auto int_sort = symbols.int_sort();

  assert(terms.mk_abs(terms.mk_rational(primo::util::Rational(-4), int_sort)) ==
         terms.mk_rational(primo::util::Rational(4), int_sort));
  assert(terms.mk_abs(terms.mk_rational(primo::util::Rational(4), int_sort)) ==
         terms.mk_rational(primo::util::Rational(4), int_sort));

  const std::size_t before = terms.extended_term_count();
  const primo::core::TermId magnitude = terms.mk_abs(x);
  assert(terms.get(magnitude).kind == TermKind::Abs);
  assert(terms.extended_term_count() == before + 1);
  // Hash-consing means a repeat costs nothing and counts once.
  assert(terms.mk_abs(x) == magnitude);
  assert(terms.extended_term_count() == before + 1);

  // abs belongs to the Ints theory only.
  bool rejected_real = false;
  try {
    static_cast<void>(terms.mk_abs(terms.mk_constant(r_symbol)));
  } catch (const std::invalid_argument&) {
    rejected_real = true;
  }
  assert(rejected_real);
}

}  // namespace

int main() {
  symbol_table_tracks_sorts_and_symbols();
  term_manager_hash_conses_and_checks_sorts();
  Boolean_terms_are_hash_consed();
  trivial_boolean_terms_are_simplified();
  associative_boolean_terms_preserve_shared_dag_nodes();
  arithmetic_terms_are_hash_consed_and_checked();
  arithmetic_terms_are_simplified();
  rational_primitive_integer_scale_clears_denominators_and_content();
  euclidean_division_follows_the_smtlib_sign_convention();
  int_div_and_mod_normalize_the_divisor_sign();
  ite_folds_only_what_is_unconditional();
  abs_folds_literals_and_counts_as_an_extended_term();
  statistics_preserve_fixed_and_dynamic_counters();
  return 0;
}
