#include "theory/arithmetic/lia_patching.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
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
};

// Every legal move that could integralize `target`: either `target` itself if
// it is nonbasic, or one of the nonbasics in its row otherwise. A move is
// legal only if it stays inside the moved variable's freedom interval, which
// is by construction the range over which it may move with every basic
// variable staying inside its bounds -- that is what makes feasibility
// preservation structural here rather than something to re-check.
void collect_candidates(const LraTableau& tableau, PatchingScoringIndex& scoring, LraVarId target,
                        std::vector<Candidate>& candidates) {
  const auto consider = [&](LraVarId moved, const DeltaRational& value) {
    if (!tableau.freedom_interval(moved).contains(value)) {
      return;
    }
    const DeltaRational delta = value - tableau.assignment(moved);
    candidates.push_back(
        Candidate{moved, value, scoring.score(tableau, moved, value), magnitude(delta.rational)});
  };

  const DeltaRational& current = tableau.assignment(target);
  const std::vector<util::Rational> targets = integer_targets(current);

  if (!tableau.is_basic(target)) {
    // The case the paper does not have to describe -- see the .hpp.
    for (const util::Rational& integer_target : targets) {
      consider(target, DeltaRational::from_rational(integer_target));
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
      consider(entry.var, tableau.assignment(entry.var) + delta);
    }
  }
}

}  // namespace

std::size_t patch_integer_variables(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                                    std::size_t patch_limit, LiaPatchingStats* stats) {
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
      collect_candidates(tableau, scoring, var, candidates);
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
    scoring.accepted_move(tableau, best->var);
    remaining = scoring.remaining();
    assert(remaining == best->resulting_count);
  }

  work.fully_integral = remaining == 0;
  return initial - remaining;
}

}  // namespace primo::theory::arithmetic
