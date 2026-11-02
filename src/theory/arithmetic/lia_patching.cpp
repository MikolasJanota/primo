#include "theory/arithmetic/lia_patching.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "theory/arithmetic/lia_integer_values.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

namespace {

util::Rational magnitude(const util::Rational& value) { return value.sign() < 0 ? -value : value; }

// How far either side of a variable's current value to look for an integer
// target. The two bracketing integers are not enough: in Bjorner &
// Nachmanson's own Example 3 (rows y = x/2, z = x/3, x starting at 3) the
// answer is x = 6, which corresponds to y = 3 -- two integers above y's
// current 3/2, so a floor/ceil-only candidate set never proposes it. Widening
// the window is what lets one move satisfy several rows at once, which is the
// whole point of scoring globally.
//
// Kept small deliberately: each extra target costs an affected-column score,
// and the far ones are the least likely to stay inside a freedom interval. Not
// yet tuned against benchmarks.
constexpr int kTargetWindow = 4;

// Integers near `value`, nearest first. Ordered by distance so the caller's
// tie-break on |delta| stays meaningful and the cheapest useful move is seen
// early.
std::vector<util::Rational> integer_targets(const DeltaRational& value) {
  const util::Rational base = delta_floor(value);
  std::vector<util::Rational> targets;
  targets.reserve(2 * kTargetWindow);
  for (int offset = 0; offset < kTargetWindow; ++offset) {
    targets.push_back(base - util::Rational(offset));
    targets.push_back(base + util::Rational(offset + 1));
  }
  std::stable_sort(targets.begin(), targets.end(),
                   [&value](const util::Rational& lhs, const util::Rational& rhs) {
                     return magnitude(lhs - value.rational) < magnitude(rhs - value.rational);
                   });
  return targets;
}

struct AffectedIntegerRow {
  LraVarId basic = 0;
  util::Rational coefficient;
};

// Patching never pivots, so row incidence is immutable for a complete sweep.
// Indexing only declared-Int basic rows turns candidate scoring from a scan of
// every integer variable (and a row search for each basic one) into a column
// walk over precisely the assignments the move can change.
class PatchingScoringIndex {
 public:
  PatchingScoringIndex(const LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                       LiaPatchingStats& stats)
      : integer_var_(tableau.variable_count(), false),
        non_integer_(tableau.variable_count(), false),
        affected_by_nonbasic_(tableau.variable_count()),
        integer_var_count_(integer_vars.size()),
        stats_(stats) {
    for (const LraVarId var : integer_vars) {
      assert(!integer_var_.at(var) && "integer variable list must be unique");
      integer_var_.at(var) = true;
      non_integer_.at(var) = !is_integer_valued(tableau.assignment(var));
      remaining_ += non_integer_[var] ? 1U : 0U;
      if (!tableau.is_basic(var)) {
        continue;
      }
      for (const auto& entry : tableau.row(var)) {
        if (tableau.is_basic(entry.var) || entry.coeff.is_zero()) {
          continue;
        }
        affected_by_nonbasic_.at(entry.var).push_back(AffectedIntegerRow{var, entry.coeff});
        ++stats_.occurrence_entries;
      }
    }
  }

  std::size_t remaining() const { return remaining_; }
  bool is_integer_var(LraVarId var) const { return integer_var_.at(var); }

  std::size_t score(const LraTableau& tableau, LraVarId moved, const DeltaRational& value) {
    ++stats_.candidates_scored;
    stats_.full_scan_equivalent += integer_var_count_;
    std::size_t result = remaining_;
    if (integer_var_.at(moved)) {
      adjust_count(result, non_integer_[moved], !is_integer_valued(value));
    }
    const DeltaRational delta = value - tableau.assignment(moved);
    for (const AffectedIntegerRow& affected : affected_by_nonbasic_.at(moved)) {
      ++stats_.affected_rows_inspected;
      const DeltaRational projected =
          tableau.assignment(affected.basic) + delta.scaled(affected.coefficient);
      adjust_count(result, non_integer_[affected.basic], !is_integer_valued(projected));
    }
    return result;
  }

  void accepted_move(const LraTableau& tableau, LraVarId moved) {
    if (integer_var_.at(moved)) {
      update_status(moved, !is_integer_valued(tableau.assignment(moved)));
    }
    for (const AffectedIntegerRow& affected : affected_by_nonbasic_.at(moved)) {
      update_status(affected.basic, !is_integer_valued(tableau.assignment(affected.basic)));
    }
  }

 private:
  static void adjust_count(std::size_t& count, bool old_non_integer, bool new_non_integer) {
    if (old_non_integer && !new_non_integer) {
      --count;
    } else if (!old_non_integer && new_non_integer) {
      ++count;
    }
  }

  void update_status(LraVarId var, bool now_non_integer) {
    adjust_count(remaining_, non_integer_[var], now_non_integer);
    non_integer_[var] = now_non_integer;
  }

  std::vector<bool> integer_var_;
  std::vector<bool> non_integer_;
  std::vector<std::vector<AffectedIntegerRow>> affected_by_nonbasic_;
  std::size_t integer_var_count_ = 0;
  std::size_t remaining_ = 0;
  LiaPatchingStats& stats_;
};

struct Candidate {
  LraVarId var = 0;
  DeltaRational value;
  std::size_t resulting_count = 0;
  util::Rational distance;
  bool lattice = false;
};

// `value` modulo the positive integer `modulus`, in [0, modulus).
util::Rational floor_mod(const util::Rational& value, const util::Rational& modulus) {
  return value - modulus * (value / modulus).floor();
}

// Inverse of `value` modulo `modulus` for coprime integers, modulus > 1, by
// the iterative extended Euclidean algorithm.
util::Rational modular_inverse(const util::Rational& value, const util::Rational& modulus) {
  util::Rational old_r = floor_mod(value, modulus);
  util::Rational r = modulus;
  util::Rational old_s(1);
  util::Rational s(0);
  while (!r.is_zero()) {
    const util::Rational quotient = (old_r / r).floor();
    util::Rational next_r = old_r - quotient * r;
    old_r = r;
    r = std::move(next_r);
    util::Rational next_s = old_s - quotient * s;
    old_s = s;
    s = std::move(next_s);
  }
  assert(old_r == util::Rational(1) && "coefficient numerator and denominator must be coprime");
  return floor_mod(old_s, modulus);
}

// Integer values k for an integer nonbasic y, with coefficient a = p/q in the
// row of basic x_b, that leave x_b integral when y := k. With
// c = beta(x_b) - a*beta(y), x_b becomes c + a*k, which is integral exactly
// when c has no infinitesimal part, q*c is an integer, and
// p*k = -q*c (mod q). The solutions form k0 + j*q; this returns the
// 2*kTargetWindow of them nearest beta(y), nearest first. An inference
// adapted from the modular-inverse step of Achterberg et al. Section 3.5
// (docs/papers/mip-presolve-integrality.md), not a procedure from the paper.
std::vector<util::Rational> lattice_values(const DeltaRational& basic_value,
                                           const DeltaRational& moved_value,
                                           const util::Rational& coefficient) {
  const DeltaRational offset = basic_value - moved_value.scaled(coefficient);
  if (!offset.infinitesimal.is_zero()) {
    return {};
  }
  const util::Rational q = coefficient.denominator();
  const util::Rational scaled_offset = offset.rational * q;
  if (!scaled_offset.is_integer()) {
    return {};
  }
  util::Rational residue(0);
  if (q != util::Rational(1)) {
    residue = floor_mod(-scaled_offset * modular_inverse(coefficient.numerator(), q), q);
  }
  const util::Rational current = moved_value.rational;
  const util::Rational base = residue + q * ((current - residue) / q).floor();
  std::vector<util::Rational> values;
  values.reserve(static_cast<std::size_t>(2) * kTargetWindow);
  for (int step = 0; step < kTargetWindow; ++step) {
    values.push_back(base - q * util::Rational(step));
    values.push_back(base + q * util::Rational(step + 1));
  }
  std::stable_sort(values.begin(), values.end(),
                   [&current](const util::Rational& lhs, const util::Rational& rhs) {
                     return magnitude(lhs - current) < magnitude(rhs - current);
                   });
  return values;
}

// Every legal move that could integralize `target`: either `target` itself if
// it is nonbasic, or one of the nonbasics in its row otherwise. A move is
// legal only if it stays inside the moved variable's freedom interval, which
// is by construction the range over which it may move with every basic
// variable staying inside its bounds -- that is what makes feasibility
// preservation structural here rather than something to re-check.
//
// With `integral_moves`, a candidate that would leave an integer nonbasic at
// a non-integer value is rejected, and an integer nonbasic in the target's
// row also gets the lattice candidates above.
void collect_candidates(const LraTableau& tableau, PatchingScoringIndex& scoring, LraVarId target,
                        bool integral_moves, LiaPatchingStats& stats,
                        std::vector<Candidate>& candidates) {
  const auto consider = [&](LraVarId moved, const DeltaRational& value, bool lattice) {
    if (!tableau.freedom_interval(moved).contains(value)) {
      return;
    }
    if (integral_moves && scoring.is_integer_var(moved) && !is_integer_valued(value)) {
      ++stats.fractional_candidates_rejected;
      return;
    }
    if (lattice) {
      ++stats.lattice_candidates_scored;
    }
    const DeltaRational delta = value - tableau.assignment(moved);
    candidates.push_back(Candidate{moved, value, scoring.score(tableau, moved, value),
                                   magnitude(delta.rational), lattice});
  };

  const DeltaRational& current = tableau.assignment(target);
  const std::vector<util::Rational> targets = integer_targets(current);

  if (!tableau.is_basic(target)) {
    // The case the paper does not have to describe -- see the .hpp.
    for (const util::Rational& integer_target : targets) {
      consider(target, DeltaRational::from_rational(integer_target), /*lattice=*/false);
    }
    return;
  }

  for (const auto& entry : tableau.row(target)) {
    if (tableau.is_basic(entry.var) || entry.coeff.is_zero()) {
      continue;
    }
    const util::Rational inverse_coeff = util::Rational(1) / entry.coeff;
    for (const util::Rational& integer_target : targets) {
      // Moving `entry.var` by delta moves `target` by `entry.coeff * delta`,
      // so the delta that lands `target` exactly on `integer_target` is
      // `(integer_target - beta(target)) / entry.coeff`.
      const DeltaRational delta =
          (DeltaRational::from_rational(integer_target) - current).scaled(inverse_coeff);
      consider(entry.var, tableau.assignment(entry.var) + delta, /*lattice=*/false);
    }
    if (integral_moves && scoring.is_integer_var(entry.var)) {
      for (const util::Rational& value :
           lattice_values(current, tableau.assignment(entry.var), entry.coeff)) {
        consider(entry.var, DeltaRational::from_rational(value), /*lattice=*/true);
      }
    }
  }
}

}  // namespace

std::size_t patch_integer_variables(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                                    std::size_t patch_limit, LiaPatchingStats* stats,
                                    bool integral_moves) {
  LiaPatchingStats ignored_stats;
  LiaPatchingStats& work = stats == nullptr ? ignored_stats : *stats;
  work = {};
  if (patch_limit == 0 || integer_vars.empty()) {
    work.fully_integral = patch_limit > 0 && integer_vars.empty();
    return 0;
  }
  PatchingScoringIndex scoring(tableau, integer_vars, work);
  std::size_t remaining = scoring.remaining();
  if (remaining == 0) {
    work.fully_integral = true;
    return 0;
  }
  const std::size_t initial = remaining;

  // Only strictly improving moves are taken. That single rule carries both
  // properties this sweep needs, and it is the same argument
  // diversify_shared_model already makes for its own monotonicity: the
  // assignment can never come out worse than it went in (so there is nothing
  // to roll back, which matters because set_nonbasic_value is not trailed),
  // and termination is free, since each accepted move strictly decreases a
  // count bounded by |integer_vars|.
  std::vector<Candidate> candidates;
  for (std::size_t attempt = 0; attempt < patch_limit && remaining > 0; ++attempt) {
    std::optional<Candidate> best;
    for (const LraVarId var : integer_vars) {
      if (is_integer_valued(tableau.assignment(var))) {
        continue;
      }
      candidates.clear();
      collect_candidates(tableau, scoring, var, integral_moves, work, candidates);
      for (const Candidate& candidate : candidates) {
        if (candidate.resulting_count >= remaining) {
          continue;
        }
        const bool better = !best.has_value() ||
                            candidate.resulting_count < best->resulting_count ||
                            (candidate.resulting_count == best->resulting_count &&
                             candidate.distance < best->distance);
        if (better) {
          best = candidate;
        }
      }
      // A move that integralizes everything cannot be beaten; stop looking.
      if (best.has_value() && best->resulting_count == 0) {
        break;
      }
    }
    if (!best.has_value()) {
      break;
    }
    tableau.set_nonbasic_value(best->var, best->value);
    if (scoring.is_integer_var(best->var) && !is_integer_valued(best->value)) {
      ++work.fractional_nonbasic_moves;
    }
    if (best->lattice) {
      ++work.lattice_moves_accepted;
    }
    scoring.accepted_move(tableau, best->var);
    remaining = scoring.remaining();
    assert(remaining == best->resulting_count);
  }

  work.fully_integral = remaining == 0;
  return initial - remaining;
}

}  // namespace primo::theory::arithmetic
