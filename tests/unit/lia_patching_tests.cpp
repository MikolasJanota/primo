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

// --- Integral patch moves (--lia-patch-integral-moves, gap 4 of
// docs/papers/mip-presolve-integrality.md) ---

std::size_t fractional_count(const LraTableau& tableau, const std::vector<LraVarId>& vars) {
  return static_cast<std::size_t>(std::count_if(vars.begin(), vars.end(), [&](LraVarId var) {
    return !is_integer_valued(tableau.assignment(var));
  }));
}

// The s1 = s2 = (2/3)y case from fractional_nonbasic_move_is_counted: with
// integral moves, y = 3/2 is rejected, and no integral value of y in [1, 2]
// makes both rows integral, so nothing moves.
void integral_moves_reject_the_fractional_nonbasic_move() {
  LraTableau tableau;
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{y, Rational(2, 3)}});
  tableau.add_row(s2, {{y, Rational(2, 3)}});
  assert(!tableau.assert_lower(y, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_upper(y, rat(2), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  LiaPatchingStats stats;
  const std::size_t patched =
      patch_integer_variables(tableau, {y, s1, s2}, 100, &stats, /*integral_moves=*/true);
  assert(patched == 0);
  assert(tableau.assignment(y) == rat(1));
  assert(stats.fractional_nonbasic_moves == 0);
  assert(stats.fractional_candidates_rejected > 0);
  assert_feasible(tableau);
}

// With s = (11/3)y and y = 1, the target window around s = 11/3 reaches
// s in {0, ..., 8}; s is integral only for y a multiple of 3, i.e. s a
// multiple of 11, so the window offers only fractional y. The lattice
// proposes y = 3 (s = 11) directly.
void lattice_finds_a_move_the_window_misses() {
  const auto build = [](LraTableau& tableau, LraVarId& y, LraVarId& s) {
    y = tableau.add_variable();
    s = tableau.add_variable();
    tableau.add_row(s, {{y, Rational(11, 3)}});
    assert(!tableau.assert_lower(y, rat(1), Lit{1}).has_value());
    assert(!tableau.assert_upper(y, rat(10), Lit{2}).has_value());
    assert(!tableau.assert_lower(s, rat(-100), Lit{3}).has_value());
    assert(!tableau.assert_upper(s, rat(100), Lit{4}).has_value());
    assert(!tableau.check().has_value());
  };
  LraTableau window_only;
  LraVarId y = 0;
  LraVarId s = 0;
  build(window_only, y, s);
  assert(patch_integer_variables(window_only, {y, s}, 100) == 0);

  LraTableau lattice;
  build(lattice, y, s);
  LiaPatchingStats stats;
  assert(patch_integer_variables(lattice, {y, s}, 100, &stats, /*integral_moves=*/true) == 1);
  assert(lattice.assignment(y) == rat(3));
  assert(lattice.assignment(s) == rat(11));
  assert(stats.lattice_moves_accepted == 1);
  assert(stats.fractional_nonbasic_moves == 0);
  assert_feasible(lattice);
}

// An offset with an infinitesimal part cannot be cancelled by an integer
// value of y, so the lattice proposes nothing for y; s is fixed by moving the
// continuous z instead, and y stays integral.
void lattice_skips_an_infinitesimal_offset() {
  LraTableau tableau;
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{y, Rational(2, 3)}, {z, Rational(1)}});
  assert(!tableau.assert_lower(y, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_upper(y, rat(5), Lit{2}).has_value());
  assert(!tableau.assert_lower(z, DeltaRational{Rational(0), Rational(1)}, Lit{3}).has_value());
  assert(!tableau.check().has_value());
  assert(!tableau.assignment(s).infinitesimal.is_zero());

  LiaPatchingStats stats;
  patch_integer_variables(tableau, {y, s}, 100, &stats, /*integral_moves=*/true);
  assert(stats.lattice_candidates_scored == 0);
  assert(is_integer_valued(tableau.assignment(y)));
  assert(stats.fractional_nonbasic_moves == 0);
  assert_feasible(tableau);
}

// Brute-force check of the lattice arithmetic. Row s = (p/q)y + b*w with y an
// integer in [lower, lower + width], w a fixed continuous offset, and s wide
// open: some integer k in y's range makes s integral iff integral patching
// makes s integral, and y always ends on an integer.
void lattice_agrees_with_brute_force() {
  unsigned state = 0x4c415454U;
  const auto next = [&state]() {
    state = state * 1664525U + 1013904223U;
    return state >> 8U;
  };
  int solvable = 0;
  int unsolvable = 0;
  for (int trial = 0; trial < 500; ++trial) {
    const long p = static_cast<long>(next() % 61U) - 30;
    const long q = static_cast<long>(next() % 7U) + 1;
    if (p == 0) {
      continue;
    }
    const Rational a(p, q);
    const Rational b(static_cast<long>(next() % 9U) + 1, static_cast<long>(next() % 5U) + 1);
    const Rational w(static_cast<long>(next() % 21U) - 10, static_cast<long>(next() % 6U) + 1);
    const long lower = static_cast<long>(next() % 11U) - 5;
    const long width = static_cast<long>(next() % 12U);

    LraTableau tableau;
    const LraVarId y = tableau.add_variable();
    const LraVarId offset = tableau.add_variable();
    const LraVarId s = tableau.add_variable();
    tableau.add_row(s, {{y, a}, {offset, b}});
    assert(!tableau.assert_lower(y, rat(lower), Lit{1}).has_value());
    assert(!tableau.assert_upper(y, rat(lower + width), Lit{2}).has_value());
    assert(!tableau.assert_lower(offset, rat(w), Lit{3}).has_value());
    assert(!tableau.assert_upper(offset, rat(w), Lit{4}).has_value());
    assert(!tableau.check().has_value());
    if (is_integer_valued(tableau.assignment(s)) || !is_integer_valued(tableau.assignment(y))) {
      continue;
    }
    bool exists = false;
    for (long k = lower; k <= lower + width; ++k) {
      exists = exists || (a * Rational(k) + b * w).is_integer();
    }
    LiaPatchingStats stats;
    patch_integer_variables(tableau, {y, s}, 100, &stats, /*integral_moves=*/true);
    assert(is_integer_valued(tableau.assignment(s)) == exists);
    assert(is_integer_valued(tableau.assignment(y)));
    assert(stats.fractional_nonbasic_moves == 0);
    assert_feasible(tableau);
    (exists ? solvable : unsolvable) += 1;
  }
  // Both outcomes must actually be exercised.
  assert(solvable > 50 && unsolvable > 50);
}

// Randomized invariants of integral patching on three-row tableaus with
// coefficients up to +-12 over denominators up to 5.
void integral_moves_keep_integer_nonbasics_integral() {
  unsigned state = 0x494e5447U;
  const auto next = [&state]() {
    state = state * 1664525U + 1013904223U;
    return state >> 8U;
  };
  for (int trial = 0; trial < 300; ++trial) {
    LraTableau tableau;
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId z = tableau.add_variable();
    const LraVarId b0 = tableau.add_variable();
    const LraVarId b1 = tableau.add_variable();
    const LraVarId b2 = tableau.add_variable();
    const auto coefficient = [&next]() {
      const long numerator = static_cast<long>(next() % 25U) - 12;
      return Rational(numerator == 0 ? 1 : numerator, static_cast<long>(next() % 5U) + 1);
    };
    for (const LraVarId basic : {b0, b1, b2}) {
      tableau.add_row(basic, {{x, coefficient()}, {y, coefficient()}, {z, coefficient()}});
    }
    for (const LraVarId var : {x, y, z}) {
      const long lower = static_cast<long>(next() % 9U) - 4;
      assert(
          !tableau.assert_lower(var, rat(lower), Lit{static_cast<int>(var * 2 + 1)}).has_value());
      assert(!tableau
                  .assert_upper(var, rat(lower + static_cast<long>(next() % 12U)),
                                Lit{static_cast<int>(var * 2 + 2)})
                  .has_value());
    }
    assert(!tableau.check().has_value());
    // z is continuous in half of the trials.
    std::vector<LraVarId> integer_vars{x, y, b0, b1, b2};
    if (next() % 2U == 0) {
      integer_vars.push_back(z);
    }
    std::vector<bool> integral_nonbasic(tableau.variable_count(), false);
    for (const LraVarId var : integer_vars) {
      integral_nonbasic[var] = !tableau.is_basic(var) && is_integer_valued(tableau.assignment(var));
    }
    const std::size_t before = fractional_count(tableau, integer_vars);

    LiaPatchingStats stats;
    const std::size_t patched =
        patch_integer_variables(tableau, integer_vars, 50, &stats, /*integral_moves=*/true);
    const std::size_t after = fractional_count(tableau, integer_vars);
    assert(after <= before && patched == before - after);
    assert(stats.fractional_nonbasic_moves == 0);
    for (const LraVarId var : integer_vars) {
      if (integral_nonbasic[var]) {
        assert(is_integer_valued(tableau.assignment(var)));
      }
    }
    assert_feasible(tableau);
  }
}

// Passing integral_moves = false explicitly is exactly the established
// patcher (whose full-scan oracle test is above).
void explicit_default_matches_the_established_patcher() {
  LraTableau base;
  const LraVarId y = base.add_variable();
  const LraVarId s1 = base.add_variable();
  const LraVarId s2 = base.add_variable();
  base.add_row(s1, {{y, Rational(2, 3)}});
  base.add_row(s2, {{y, Rational(2, 3)}});
  assert(!base.assert_lower(y, rat(1), Lit{1}).has_value());
  assert(!base.assert_upper(y, rat(2), Lit{2}).has_value());
  assert(!base.check().has_value());
  LraTableau implicit = base;
  LraTableau explicit_default = base;
  LiaPatchingStats implicit_stats;
  LiaPatchingStats explicit_stats;
  assert(patch_integer_variables(implicit, {y, s1, s2}, 100, &implicit_stats) ==
         patch_integer_variables(explicit_default, {y, s1, s2}, 100, &explicit_stats, false));
  for (LraVarId var = 0; var < base.variable_count(); ++var) {
    assert(implicit.assignment(var) == explicit_default.assignment(var));
  }
  assert(implicit_stats.candidates_scored == explicit_stats.candidates_scored);
  assert(explicit_stats.fractional_candidates_rejected == 0);
  assert(explicit_stats.lattice_candidates_scored == 0);
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
  integral_moves_reject_the_fractional_nonbasic_move();
  lattice_finds_a_move_the_window_misses();
  lattice_skips_an_infinitesimal_offset();
  lattice_agrees_with_brute_force();
  integral_moves_keep_integer_nonbasics_integral();
  explicit_default_matches_the_established_patcher();
  return 0;
}
