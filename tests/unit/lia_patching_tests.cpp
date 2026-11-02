#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::is_integer_valued;
using primo::theory::arithmetic::LiaPatchingStats;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::theory::arithmetic::patch_integer_variables;
using primo::util::Rational;

namespace {

DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }
DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }

Rational magnitude(const Rational& value) { return value.sign() < 0 ? -value : value; }

std::vector<Rational> reference_integer_targets(const DeltaRational& value) {
  const Rational base = primo::theory::arithmetic::delta_floor(value);
  std::vector<Rational> targets;
  for (int offset = 0; offset < 4; ++offset) {
    targets.push_back(base - Rational(offset));
    targets.push_back(base + Rational(offset + 1));
  }
  std::stable_sort(targets.begin(), targets.end(),
                   [&value](const Rational& lhs, const Rational& rhs) {
                     return magnitude(lhs - value.rational) < magnitude(rhs - value.rational);
                   });
  return targets;
}

std::optional<Rational> reference_row_coefficient(const LraTableau& tableau, LraVarId basic,
                                                  LraVarId nonbasic) {
  for (const auto& entry : tableau.row(basic)) {
    if (entry.var == nonbasic) {
      return entry.coeff;
    }
  }
  return std::nullopt;
}

std::size_t reference_score(const LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                            LraVarId moved, const DeltaRational& value) {
  const DeltaRational delta = value - tableau.assignment(moved);
  std::size_t count = 0;
  for (const LraVarId var : integer_vars) {
    DeltaRational projected = tableau.assignment(var);
    if (var == moved) {
      projected = value;
    } else if (tableau.is_basic(var)) {
      const std::optional<Rational> coefficient = reference_row_coefficient(tableau, var, moved);
      if (coefficient.has_value()) {
        projected = projected + delta.scaled(*coefficient);
      }
    }
    count += is_integer_valued(projected) ? 0U : 1U;
  }
  return count;
}

struct ReferenceCandidate {
  LraVarId var = 0;
  DeltaRational value;
  std::size_t resulting_count = 0;
  Rational distance;
};

std::size_t reference_patch(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                            std::size_t patch_limit) {
  std::size_t remaining = reference_score(tableau, integer_vars, integer_vars.front(),
                                          tableau.assignment(integer_vars.front()));
  const std::size_t initial = remaining;
  for (std::size_t attempt = 0; attempt < patch_limit && remaining > 0; ++attempt) {
    std::optional<ReferenceCandidate> best;
    for (const LraVarId target : integer_vars) {
      if (is_integer_valued(tableau.assignment(target))) {
        continue;
      }
      std::vector<std::pair<LraVarId, DeltaRational>> moves;
      const DeltaRational& current = tableau.assignment(target);
      const std::vector<Rational> targets = reference_integer_targets(current);
      if (!tableau.is_basic(target)) {
        for (const Rational& integer : targets) {
          moves.emplace_back(target, rat(integer));
        }
      } else {
        for (const auto& entry : tableau.row(target)) {
          if (tableau.is_basic(entry.var) || entry.coeff.is_zero()) {
            continue;
          }
          for (const Rational& integer : targets) {
            const DeltaRational delta = (rat(integer) - current).scaled(Rational(1) / entry.coeff);
            moves.emplace_back(entry.var, tableau.assignment(entry.var) + delta);
          }
        }
      }
      for (const auto& [moved, value] : moves) {
        if (!tableau.freedom_interval(moved).contains(value)) {
          continue;
        }
        const std::size_t score = reference_score(tableau, integer_vars, moved, value);
        const Rational distance = magnitude((value - tableau.assignment(moved)).rational);
        if (score < remaining && (!best.has_value() || score < best->resulting_count ||
                                  (score == best->resulting_count && distance < best->distance))) {
          best = ReferenceCandidate{moved, value, score, distance};
        }
      }
      if (best.has_value() && best->resulting_count == 0) {
        break;
      }
    }
    if (!best.has_value()) {
      break;
    }
    tableau.set_nonbasic_value(best->var, best->value);
    remaining = best->resulting_count;
  }
  return initial - remaining;
}

// The claim the whole stage rests on: a move confined to a freedom interval
// keeps every basic variable inside its bounds. Asserted directly after every
// patch rather than trusted, since a violation here is exactly what would make
// branch_and_bound report Sat on an infeasible assignment.
void assert_feasible(LraTableau& tableau) {
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    const DeltaRational& value = tableau.assignment(var);
    if (const auto lower = tableau.lower_bound(var); lower.has_value()) {
      assert(value >= lower->value);
    }
    if (const auto upper = tableau.upper_bound(var); upper.has_value()) {
      assert(value <= upper->value);
    }
  }
  // And no repair should be needed at all -- check() must be a no-op.
  assert(!tableau.check().has_value());
}

// Bjorner & Nachmanson Sect. 4.1, Example 3, built exactly as the paper states
// it: y - x/2 = 0, z - x/3 = 0, with x in [3,10], y in [-3,4], z in [-5,12].
// x starts at its lower bound 3, which leaves y = 3/2 fractional. The paper's
// answer is to move x to 6 inside its freedom interval, making y = 3 and
// z = 2, both integral and both within bounds.
void paper_example_3_patches_x_to_an_integer_point() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  tableau.add_row(y, {{x, Rational(1, 2)}});
  tableau.add_row(z, {{x, Rational(1, 3)}});
  assert(!tableau.assert_lower(x, rat(3), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(-3), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(4), Lit{4}).has_value());
  assert(!tableau.assert_lower(z, rat(-5), Lit{5}).has_value());
  assert(!tableau.assert_upper(z, rat(12), Lit{6}).has_value());
  assert(!tableau.check().has_value());

  // The paper's starting state: x at its bound, y fractional.
  assert(tableau.assignment(x) == rat(3));
  assert(tableau.assignment(y) == rat(Rational(3, 2)));
  assert(!is_integer_valued(tableau.assignment(y)));

  const std::size_t patched = patch_integer_variables(tableau, {x, y, z}, 100);
  assert(patched > 0);
  assert(is_integer_valued(tableau.assignment(x)));
  assert(is_integer_valued(tableau.assignment(y)));
  assert(is_integer_valued(tableau.assignment(z)));
  assert_feasible(tableau);
}

// Records the integrality gap tracked in docs/implementation/lia.md: with
// s1 = s2 = (2/3)y and y in [1,2], moving y to 3/2 makes both basic rows
// integral, so the fractional count drops from two to one even though the
// integer nonbasic y becomes fractional. The counter makes that visible.
void fractional_nonbasic_move_is_counted() {
  LraTableau tableau;
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{y, Rational(2, 3)}});
  tableau.add_row(s2, {{y, Rational(2, 3)}});
  assert(!tableau.assert_lower(y, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_upper(y, rat(2), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(y) == rat(1));
  assert(tableau.assignment(s1) == rat(Rational(2, 3)));

  LiaPatchingStats stats;
  const std::size_t patched = patch_integer_variables(tableau, {y, s1, s2}, 100, &stats);
  assert(patched == 1);
  assert(tableau.assignment(y) == rat(Rational(3, 2)));
  assert(is_integer_valued(tableau.assignment(s1)));
  assert(is_integer_valued(tableau.assignment(s2)));
  assert(stats.fractional_nonbasic_moves == 1);
  assert(!stats.fully_integral);
  assert_feasible(tableau);
}

// The case the paper does not have to describe, because in its setting
// nonbasic variables are always at their bounds and therefore already
// integral. Primo's scale-canonicalization stores a fractional bound on an
// Int-sorted variable, so a nonbasic Int variable can sit at 5/3, and the fix
// is to move it to an integer in range with no row reasoning at all.
void fractional_nonbasic_is_rounded_in_place() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(5, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(!is_integer_valued(tableau.assignment(x)));

  assert(patch_integer_variables(tableau, {x}, 100) == 1);
  assert(tableau.assignment(x) == rat(2));
  assert_feasible(tableau);
}

// A variable pinned strictly between two consecutive integers cannot be
// patched at all: the freedom interval holds no integer. Patching must report
// no progress and leave the assignment exactly as it found it, so the
// refutation stages see an unchanged tableau.
void unpatchable_variable_is_left_untouched() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(1, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(Rational(2, 3)), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational before = tableau.assignment(x);

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(x) == before);
  assert_feasible(tableau);
}

// A fixed variable has an empty freedom interval and must simply be skipped,
// not moved off its fixed value. 3x = 2 pins the slack, leaving x = 2/3 with
// nowhere to go.
void fixed_variable_is_not_moved() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}});
  assert(!tableau.assert_lower(s, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(s, rat(2), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational s_before = tableau.assignment(s);

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(s) == s_before);
  assert_feasible(tableau);
}

// Delta-awareness. A strict lower bound stores 3 + eps, so the smallest
// integer the variable may take is 4, not 3 -- taking delta_floor's rational
// part alone would produce 3, which is below the bound.
void strict_bound_rounds_up_past_the_infinitesimal() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, DeltaRational{Rational(3), Rational(1)}, Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(8), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(!is_integer_valued(tableau.assignment(x)));

  assert(patch_integer_variables(tableau, {x}, 100) == 1);
  assert(tableau.assignment(x) == rat(4));
  assert_feasible(tableau);
}

// A budget of zero is the A/B control and must be a complete no-op.
void zero_patch_limit_is_a_no_op() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(5, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational before = tableau.assignment(x);

  assert(patch_integer_variables(tableau, {x}, 0) == 0);
  assert(tableau.assignment(x) == before);
}

// An already-integral assignment gives patching nothing to do.
void already_integral_assignment_reports_no_progress() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(9), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(x) == rat(2));
  assert_feasible(tableau);
}

// Only strictly improving moves are taken, so a sweep with nothing to gain
// must leave the assignment byte-for-byte as it found it. That matters
// specifically because set_nonbasic_value is not trailed: there is no undo, so
// "never take a move that does not help" is the whole safety argument. Here x
// is pinned strictly between 0 and 1 and cannot be integralized at all.
void a_sweep_with_no_improving_move_changes_nothing() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  assert(!tableau.assert_lower(x, rat(Rational(1, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(Rational(2, 3)), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(5), Lit{4}).has_value());
  assert(!tableau.check().has_value());

  std::vector<DeltaRational> before;
  before.reserve(tableau.variable_count());
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    before.push_back(tableau.assignment(var));
  }

  assert(patch_integer_variables(tableau, {x, y}, 100) == 0);
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    assert(tableau.assignment(var) == before[var]);
  }
  assert_feasible(tableau);
}

void indexed_scoring_matches_full_scan_oracle() {
  unsigned state = 0x4c494131U;
  const auto next = [&state]() {
    state = state * 1664525U + 1013904223U;
    return state;
  };
  for (int trial = 0; trial < 40; ++trial) {
    LraTableau indexed;
    const LraVarId x = indexed.add_variable();
    const LraVarId y = indexed.add_variable();
    const LraVarId z = indexed.add_variable();
    const LraVarId b0 = indexed.add_variable();
    const LraVarId b1 = indexed.add_variable();
    const auto coefficient = [&next]() {
      const long numerator = static_cast<long>(next() % 5U) - 2;
      return Rational(numerator == 0 ? 1 : numerator, static_cast<long>(next() % 3U) + 1);
    };
    indexed.add_row(b0, {{x, coefficient()}, {y, coefficient()}, {z, coefficient()}});
    indexed.add_row(b1, {{x, coefficient()}, {y, coefficient()}, {z, coefficient()}});
    const std::vector<LraVarId> integer_vars{x, y, z, b0, b1};
    for (const LraVarId var : std::vector<LraVarId>{x, y, z}) {
      const Rational lower(static_cast<long>(next() % 7U) - 3, static_cast<long>(next() % 3U) + 1);
      assert(
          !indexed.assert_lower(var, rat(lower), Lit{static_cast<int>(var * 2 + 1)}).has_value());
      assert(
          !indexed.assert_upper(var, rat(lower + Rational(10)), Lit{static_cast<int>(var * 2 + 2)})
               .has_value());
    }
    assert(!indexed.check().has_value());
    LraTableau reference = indexed;

    LiaPatchingStats stats;
    const std::size_t indexed_patched = patch_integer_variables(indexed, integer_vars, 20, &stats);
    const std::size_t reference_patched = reference_patch(reference, integer_vars, 20);
    assert(indexed_patched == reference_patched);
    for (LraVarId var = 0; var < indexed.variable_count(); ++var) {
      assert(indexed.assignment(var) == reference.assignment(var));
    }
    assert(stats.affected_rows_inspected <= stats.full_scan_equivalent);
    assert_feasible(indexed);
  }
}

}  // namespace

int main() {
  paper_example_3_patches_x_to_an_integer_point();
  fractional_nonbasic_is_rounded_in_place();
  fractional_nonbasic_move_is_counted();
  unpatchable_variable_is_left_untouched();
  fixed_variable_is_not_moved();
  strict_bound_rounds_up_past_the_infinitesimal();
  zero_patch_limit_is_a_no_op();
  already_integral_assignment_reports_no_progress();
  a_sweep_with_no_improving_move_changes_nothing();
  indexed_scoring_matches_full_scan_oracle();
  return 0;
}
