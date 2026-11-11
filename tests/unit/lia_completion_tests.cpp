#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_completion.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::complete_integer_assignment;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::is_integer_valued;
using primo::theory::arithmetic::LiaCompletionResult;
using primo::theory::arithmetic::LraTableau;
using primo::util::Rational;

namespace {
using Status = LiaCompletionResult::Status;

DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }

void patching_and_disabled_search() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(1, 3)), Lit{1}));
  assert(!tableau.assert_upper(x, rat(Rational(5, 3)), Lit{2}));
  assert(!tableau.check());
  const auto limited = complete_integer_assignment(tableau, {x}, {0, 0, 0});
  assert(limited.status == Status::ResourceLimit);
  assert(tableau.lower_bound(x)->value == rat(Rational(1, 3)));
  assert(tableau.upper_bound(x)->value == rat(Rational(5, 3)));
  const auto patched = complete_integer_assignment(tableau, {x}, {1, 0, 0});
  assert(patched.status == Status::Sat);
  assert(patched.patched_variables == 1);
  assert(patched.patch_stats.fully_integral);
  assert(tableau.assignment(x) == rat(Rational(1)));
}

void branching_preserves_the_winning_assignment() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  assert(!tableau.assert_lower(x, DeltaRational{Rational(0), Rational(1)}, Lit{3}));
  assert(!tableau.assert_upper(x, rat(Rational(2)), Lit{4}));
  assert(!tableau.check());
  const auto result = complete_integer_assignment(tableau, {x}, {0, 0, 1});
  assert(result.status == Status::Sat);
  assert(is_integer_valued(tableau.assignment(x)));
  assert(tableau.assignment(x) == rat(Rational(1)));
}

void gcd_precedes_the_zero_branch_budget() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto sum = tableau.add_variable();
  tableau.add_row(sum, {{x, Rational(4)}, {y, Rational(6)}});
  assert(!tableau.assert_lower(sum, rat(Rational(3)), Lit{5}));
  assert(!tableau.assert_upper(sum, rat(Rational(3)), Lit{6}));
  assert(!tableau.check());
  const auto result = complete_integer_assignment(tableau, {x, y}, {0, 0, 0});
  assert(result.status == Status::Unsat);
  assert(result.explanation.literals().size() == 2);
  assert(result.explanation.literals()[0].value == 5);
  assert(result.explanation.literals()[1].value == 6);
}

void cuts_retain_antecedents_for_later_branching() {
  LraTableau tableau;
  const auto a = tableau.add_variable();
  const auto b = tableau.add_variable();
  const auto integral_row = tableau.add_variable();
  const auto impossible = tableau.add_variable();
  tableau.add_row(integral_row, {{a, Rational(3)}, {b, Rational(5)}});
  assert(!tableau.assert_lower(a, rat(Rational(0)), Lit{7}));
  assert(!tableau.assert_lower(b, rat(Rational(1, 2)), Lit{8}));
  assert(!tableau.assert_lower(impossible, rat(Rational(1, 4)), Lit{9}));
  assert(!tableau.assert_upper(impossible, rat(Rational(3, 4)), Lit{10}));
  assert(!tableau.check());
  const auto before = tableau.variable_count();
  const auto result = complete_integer_assignment(tableau, {integral_row, impossible}, {0, 1, 4});
  assert(result.status == Status::Unsat);
  assert(tableau.variable_count() > before);
  // The first cut is satisfiable. The independent fractional interval fails
  // only in branching; its explanation must not replace the earlier cut's.
  for (const int reason : {7, 8, 9, 10}) {
    assert(std::ranges::any_of(result.explanation.literals(),
                               [reason](Lit literal) { return literal.value == reason; }));
  }
}

void empty_integer_set_succeeds_without_search() {
  LraTableau tableau;
  const auto result = complete_integer_assignment(tableau, {}, {1, 0, 0});
  assert(result.status == Status::Sat);
  assert(result.explanation.empty());
  assert(result.patch_stats.fully_integral);
}
// Gap 3 of docs/papers/mip-presolve-integrality.md: a slack whose row is an
// integer polynomial over integer variables may be treated as an integer
// variable. With s = 2x + 3y in [13/2, 27/4] and x, y unbounded, one split
// on s refutes the candidate, and the explanation is exactly the two bound
// literals: the split s <= 6 or s >= 7 is a tautology over the integers and
// drops out. Branching only on x and y exhausts a small node budget.
void branching_on_an_integer_slack_gives_a_bound_explanation() {
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(3)}});
  assert(!tableau.assert_lower(s, rat(Rational(13, 2)), Lit{5}));
  assert(!tableau.assert_upper(s, rat(Rational(27, 4)), Lit{6}));
  assert(!tableau.check());

  LraTableau inputs_only = tableau;
  const auto without = complete_integer_assignment(inputs_only, {x, y}, {0, 0, 20});
  assert(without.status != Status::Unsat);

  const auto with = complete_integer_assignment(tableau, {s, x, y}, {0, 0, 2});
  assert(with.status == Status::Unsat);
  const std::vector<Lit>& reasons = with.explanation.literals();
  assert(reasons.size() == 2 && reasons[0].value == 5 && reasons[1].value == 6);
}

// A Gomory cut from a row whose basic variable is an integer slack: with
// s = (2/3)t, t at its lower bound 1 and t <= 7/5, s lies in [2/3, 14/15],
// which holds no integer. One cut round refutes it without patching or
// branching, and the explanation uses only t's bound literals.
void gomory_cut_from_an_integer_slack_row() {
  LraTableau tableau;
  const auto t = tableau.add_variable();
  const auto s = tableau.add_variable();
  tableau.add_row(s, {{t, Rational(2, 3)}});
  assert(!tableau.assert_lower(t, rat(Rational(1)), Lit{7}));
  assert(!tableau.assert_upper(t, rat(Rational(7, 5)), Lit{8}));
  assert(!tableau.check());
  assert(tableau.is_basic(s) && tableau.assignment(s) == rat(Rational(2, 3)));
  LraTableau no_stages = tableau;
  assert(complete_integer_assignment(no_stages, {s}, {0, 0, 0}).status != Status::Unsat);
  const auto result = complete_integer_assignment(tableau, {s}, {0, 1, 0});
  assert(result.status == Status::Unsat);
  const std::vector<Lit>& reasons = result.explanation.literals();
  assert(!reasons.empty());
  for (const Lit lit : reasons) {
    assert(lit.value == 7 || lit.value == 8);
  }
}

}  // namespace

int main() {
  {
    LraTableau tableau;
    const auto x = tableau.add_variable();
    const auto y = tableau.add_variable();
    const auto slack = tableau.add_variable();
    tableau.add_row(slack, {{x, Rational(2)}, {y, Rational(-2)}});
    assert(!tableau.assert_lower(slack, rat(Rational(1)), Lit{1}));
    assert(!tableau.assert_upper(slack, rat(Rational(3, 2)), Lit{2}));
    assert(!tableau.check());
    primo::theory::arithmetic::LiaBranchHistory history;
    const std::vector originals{x, y};
    primo::theory::arithmetic::LiaCompletionLimits limits{0, 0, 50};
    limits.hybrid = true;
    const auto result =
        complete_integer_assignment(tableau, originals, limits, nullptr, &history, &originals);
    assert(result.status == Status::Split && result.branch_nodes == 2);
    assert(!tableau.lower_bound(x) && !tableau.upper_bound(x));
    assert(!tableau.lower_bound(y) && !tableau.upper_bound(y));
    assert(!tableau.check());
  }
  {
    // An open gap on a stable coordinate must escape as a split, including
    // negative infinitesimal floors, without adding rows or numerical variables.
    for (const auto& value :
         {DeltaRational{Rational(-3), Rational(-1)}, DeltaRational{Rational(-7, 2), Rational(0)}}) {
      LraTableau tableau;
      const auto x = tableau.add_variable();
      const auto y = tableau.add_variable();
      tableau.add_row(x, {{y, Rational(1)}});
      assert(!tableau.assert_upper(y, value, Lit{1}));
      assert(!tableau.check());
      const auto count = tableau.variable_count();
      primo::theory::arithmetic::LiaBranchHistory history;
      const std::vector original{x};
      primo::theory::arithmetic::LiaCompletionLimits limits{0, 0, 20};
      limits.hybrid = true;
      limits.private_branch_factor = 0;
      const auto result =
          complete_integer_assignment(tableau, original, limits, nullptr, &history, &original);
      assert(result.status == Status::Split);
      assert(result.split_variable == x && result.split_floor == Rational(-4));
      assert(result.branch_nodes == 0 && result.branch_budget_exit);
      assert(tableau.variable_count() == count);
      assert(!tableau.upper_bound(x) && !tableau.lower_bound(x));
      assert(!tableau.check());
    }
  }
  patching_and_disabled_search();
  branching_preserves_the_winning_assignment();
  gcd_precedes_the_zero_branch_budget();
  cuts_retain_antecedents_for_later_branching();
  empty_integer_set_succeeds_without_search();
  branching_on_an_integer_slack_gives_a_bound_explanation();
  gomory_cut_from_an_integer_slack_row();
}
