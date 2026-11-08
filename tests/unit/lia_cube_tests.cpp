#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_completion.hpp"
#include "theory/arithmetic/lia_cube.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using namespace primo::theory::arithmetic;
using primo::sat::Lit;
using primo::util::Rational;

namespace {
DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }
using Status = LiaCubeResult::Status;

void paper_example_and_completion() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto s = tableau.add_variable();
  const auto t = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}, {y, Rational(1)}});
  tableau.add_row(t, {{y, Rational(-3)}});
  const auto problem = LiaCubeProblem::from_tableau(tableau, {x, y});
  assert(!tableau.assert_upper(s, rat(Rational(9)), Lit{1}));
  assert(!tableau.assert_upper(t, rat(Rational(-2)), Lit{2}));
  assert(!tableau.check());
  assert(!is_integer_valued(tableau.assignment(y)));
  const auto result = complete_integer_assignment(
      tableau, {x, y, s, t}, {0, 0, 0, false, LiaCubeMode::Uniform, 100}, &problem);
  assert(result.status == LiaCompletionResult::Status::Sat);
  assert(result.cube.status == Status::Sat && result.cube.center_found);
  assert(result.cube.constraints == 2);
  assert(result.explanation.empty());
  assert(is_integer_valued(tableau.assignment(x)) && is_integer_valued(tableau.assignment(y)));
  assert(tableau.assignment(s) ==
         tableau.assignment(x).scaled(Rational(3)) + tableau.assignment(y));
  assert(tableau.assignment(s) <= rat(Rational(9)));
  assert(tableau.assignment(t) <= rat(Rational(-2)));
  assert(tableau.upper_bound(t).value().reason.value == 2);
}

void failed_attempt_and_budget_preserve_working_state() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto sum = tableau.add_variable();
  tableau.add_row(sum, {{x, Rational(2)}, {y, Rational(3)}});
  const auto problem = LiaCubeProblem::from_tableau(tableau, {x, y});
  assert(!tableau.assert_lower(sum, rat(Rational(1)), Lit{1}));
  assert(!tableau.assert_upper(sum, rat(Rational(1)), Lit{2}));
  assert(!tableau.check());
  const auto basis_x = tableau.is_basic(x);
  const auto basis_y = tableau.is_basic(y);
  const auto x_value = tableau.assignment(x);
  const auto y_value = tableau.assignment(y);
  const auto pivots = tableau.pivot_count();
  const auto checkpoint = tableau.push();
  for (const auto budget : {std::size_t(0), std::size_t(100)}) {
    const auto result = try_cube_model(tableau, problem, {x, y}, LiaCubeMode::Uniform, budget);
    assert(result.status == (budget == 0 ? Status::LimitReached : Status::Infeasible));
    assert(tableau.is_basic(x) == basis_x && tableau.is_basic(y) == basis_y);
    assert(tableau.assignment(x) == x_value && tableau.assignment(y) == y_value);
    assert(tableau.pivot_count() == pivots && tableau.push() == checkpoint);
    assert(tableau.lower_bound(sum).value().reason.value == 1);
  }
  // Cube infeasibility must not close the satisfiable thin equality.
  assert(complete_integer_assignment(tableau, {x, y}, {0, 0, 100}).status ==
         LiaCompletionResult::Status::Sat);
}

void difference_equality_survives_rounding() {
  for (const auto offset : {-2, 1}) {
    LraTableau tableau;
    const auto x = tableau.add_variable();
    const auto y = tableau.add_variable();
    const auto d = tableau.add_variable();
    // Primitive scaling must recover the difference form.
    tableau.add_row(d, {{x, Rational(2, 3)}, {y, Rational(-2, 3)}});
    const auto problem = LiaCubeProblem::from_tableau(tableau, {x, y});
    assert(!tableau.assert_lower(d, rat(Rational(offset) * Rational(2, 3)), Lit{1}));
    assert(!tableau.assert_upper(d, rat(Rational(offset) * Rational(2, 3)), Lit{2}));
    assert(!tableau.assert_lower(y, rat(Rational(-2)), Lit{3}));
    assert(!tableau.assert_upper(y, rat(Rational(-1)), Lit{4}));
    assert(!tableau.check());
    // The strengthened y bounds force the cube center to -3/2. Thus both
    // rounded coordinates are negative half-integer ties for offset -2.
    // Make both coordinates fractional while preserving the difference.
    if (!tableau.is_basic(y))
      tableau.set_nonbasic_value(y, rat(Rational(-3, 2)));
    else
      tableau.set_nonbasic_value(x, rat(Rational(1, 2)));
    assert(!tableau.check());
    auto uniform = tableau;
    assert(try_cube_model(uniform, problem, {x, y}, LiaCubeMode::Uniform, 100).status ==
           Status::Infeasible);
    const auto result = try_cube_model(tableau, problem, {x, y}, LiaCubeMode::Difference, 100);
    assert(result.status == Status::Sat && result.difference_exemptions == 2);
    assert(tableau.assignment(x) - tableau.assignment(y) == rat(Rational(offset)));
  }
}

void fixed_substitution_strict_bounds_and_large_exact_coefficients() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto s = tableau.add_variable();
  const Rational big = Rational::parse("123456789012345678901234567890").value();
  tableau.add_row(s, {{x, big}, {y, Rational(1, 3)}});
  const auto problem = LiaCubeProblem::from_tableau(tableau, {x, y});
  assert(!tableau.assert_lower(x, rat(Rational(-2)), Lit{1}));
  assert(!tableau.assert_upper(x, rat(Rational(-2)), Lit{2}));
  assert(!tableau.assert_lower(s, {big * Rational(-2), Rational(1)}, Lit{3}));
  assert(!tableau.assert_upper(y, {Rational(3), Rational(-1)}, Lit{4}));
  assert(!tableau.check());
  const auto result = try_cube_model(tableau, problem, {x, y}, LiaCubeMode::Uniform, 100);
  assert(result.status == Status::Sat && result.fixed_coordinates == 1);
  assert(tableau.assignment(x) == rat(Rational(-2)));
  assert(tableau.assignment(y) >= rat(Rational(1)) && tableau.assignment(y) <= rat(Rational(2)));
  assert(tableau.assignment(s) ==
         rat(big * Rational(-2)) + tableau.assignment(y).scaled(Rational(1, 3)));
}

void candidate_rejection_never_adopts() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1, 2)}});
  const auto problem = LiaCubeProblem::from_tableau(tableau, {x});
  assert(!tableau.assert_lower(x, rat(Rational(1)), Lit{1}));
  assert(!tableau.assert_upper(x, rat(Rational(1)), Lit{2}));
  assert(!tableau.check());
  // An extra independent integrality obligation is intentionally stronger
  // than semantic integrality. Validation must catch it, rather than adopting.
  const auto before = tableau.assignment(s);
  const auto result = try_cube_model(tableau, problem, {x, s}, LiaCubeMode::Uniform, 100);
  assert(result.status == Status::CandidateRejected && result.center_found);
  assert(tableau.assignment(s) == before);
}

void bounded_checks_cover_soi_and_resume() {
  for (const auto rule : {LraTableau::PivotingRule::Bland, LraTableau::PivotingRule::Soi}) {
    LraTableau tableau(rule);
    const auto x = tableau.add_variable();
    const auto s = tableau.add_variable();
    tableau.add_row(s, {{x, Rational(2)}});
    assert(!tableau.assert_lower(s, rat(Rational(1)), Lit{1}));
    assert(tableau.check_bounded(0) == LraTableau::BoundedCheckStatus::LimitReached);
    assert(tableau.check_bounded(10) == LraTableau::BoundedCheckStatus::Feasible);
    assert(!tableau.check());
  }
}
}  // namespace

int main() {
  paper_example_and_completion();
  failed_attempt_and_budget_preserve_working_state();
  difference_equality_survives_rounding();
  fixed_substitution_strict_bounds_and_large_exact_coefficients();
  candidate_rejection_never_adopts();
  bounded_checks_cover_soi_and_resume();
}
