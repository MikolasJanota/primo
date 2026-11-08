#include "theory/arithmetic/lra_tableau.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace primo::theory::arithmetic {

LraVarId LraTableau::add_variable() {
  const LraVarId id = vars_.size();
  vars_.push_back(VarState{});
  rows_.emplace_back();
  occurs_in_.emplace_back();
  soi_directions_.push_back(0);
  return id;
}

void LraTableau::add_row(LraVarId basic_var,
                         const std::vector<std::pair<LraVarId, util::Rational>>& terms) {
  Row row;
  row.reserve(terms.size());
  DeltaRational assignment = DeltaRational::from_rational(util::Rational(0));
  for (const auto& [var, coefficient] : terms) {
    if (coefficient.is_zero()) {
      continue;
    }
    row.emplace_back(var, coefficient, static_cast<std::uint32_t>(occurs_in_[var].size()));
    occurs_in_[var].push_back(basic_var);
    assignment = assignment + vars_.at(var).assignment.scaled(coefficient);
  }
  // Establishes the Row ordering invariant (see Row's declaration): callers
  // pass terms in formula order, which is not var-id order. Sorting here is
  // paid once per row at construction, and is what lets pivot()'s hot
  // affected-row loop merge rather than sort.
  std::sort(row.begin(), row.end(),
            [](const RowEntry& lhs, const RowEntry& rhs) { return lhs.var < rhs.var; });
  vars_.at(basic_var).is_basic = true;
  vars_.at(basic_var).assignment = assignment;
  rows_[basic_var] = std::move(row);
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_upper(LraVarId var, DeltaRational value,
                                                              sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.upper.has_value() && value >= state.upper->value) {
    return std::nullopt;
  }
  if (state.lower.has_value() && value < state.lower->value) {
    return std::vector<sat::Lit>{state.lower->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, true, state.upper});
  state.upper = Bound{value, reason};
  if (!state.is_basic) {
    if (state.assignment > value) {
      update(var, value);
    }
  } else {
    update_violation_status(var);
    if (rule_ == PivotingRule::Soi) {
      set_soi_direction(var, soi_direction(var), rows_.at(var));
    }
    try_eliminate_fixed_basic(var);
  }
  return std::nullopt;
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_lower(LraVarId var, DeltaRational value,
                                                              sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.lower.has_value() && value <= state.lower->value) {
    return std::nullopt;
  }
  if (state.upper.has_value() && value > state.upper->value) {
    return std::vector<sat::Lit>{state.upper->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, false, state.lower});
  state.lower = Bound{value, reason};
  if (!state.is_basic) {
    if (state.assignment < value) {
      update(var, value);
    }
  } else {
    update_violation_status(var);
    if (rule_ == PivotingRule::Soi) {
      set_soi_direction(var, soi_direction(var), rows_.at(var));
    }
    try_eliminate_fixed_basic(var);
  }
  return std::nullopt;
}

void LraTableau::update(LraVarId nonbasic_var, const DeltaRational& new_value) {
  VarState& state = vars_.at(nonbasic_var);
  const DeltaRational delta = new_value - state.assignment;
  if (trails_assignments_and_pivots()) {
    trail_.push_back(AssignmentTrailEntry{nonbasic_var, state.assignment});
  }
  state.assignment = new_value;
  // nonbasic_var is, by this function's own contract, never basic --
  // update_violation_status() therefore always erases it here (a no-op if
  // it was never a member). This specifically matters right after
  // check()'s own `pivot(violated, entering); update(violated, target);`
  // sequence: `violated` just became nonbasic via pivot() (which doesn't
  // touch violated_vars_ itself for the leaving variable -- see pivot()'s
  // own comment), so this call is what actually clears its now-stale
  // membership.
  update_violation_status(nonbasic_var);
  // Copy the affected-rows list before iterating: set_soi_direction()
  // below can mutate occurs_in_[some_var]'s vector (adding/removing
  // f_var_ from it) as a side effect, and if nonbasic_var itself happens
  // to be one of f_var_'s row's own terms, that mutation would hit this
  // exact occurs_in_[nonbasic_var] entry -- iterating the live reference
  // directly caused a real heap-use-after-free (found via manual ASAN
  // testing this round, not just theorized).
  const std::vector<LraVarId> affected = occurs_in_.at(nonbasic_var);
  for (const LraVarId basic_var : affected) {
    for (const auto& [var, coefficient, var_slot] : rows_.at(basic_var)) {
      if (var == nonbasic_var) {
        VarState& basic_state = vars_.at(basic_var);
        if (trails_assignments_and_pivots()) {
          trail_.push_back(AssignmentTrailEntry{basic_var, basic_state.assignment});
        }
        basic_state.assignment = basic_state.assignment + delta.scaled(coefficient);
        // f_var_ is harmless to include here too: it has no bounds, so
        // update_violation_status() always computes "not violated" for it.
        update_violation_status(basic_var);
        // f_var_ itself is excluded: it has no bounds, so soi_direction()
        // is always 0 for it and the hook would be a no-op.
        if (rule_ == PivotingRule::Soi && (!f_var_.has_value() || basic_var != *f_var_)) {
          set_soi_direction(basic_var, soi_direction(basic_var), rows_.at(basic_var));
        }
        break;
      }
    }
  }
}

void LraTableau::pivot(LraVarId basic_var, LraVarId entering_var) {
  ++pivot_count_;
  // Bounds-only backtracking (item H9, see the class comment) means the trail needs
  // nothing from this pivot at all on the default path; the snapshot below
  // is built only for SOI, which does have to undo it.
  const bool trail_this_pivot = trails_assignments_and_pivots();
  PivotTrailEntry trail_entry;
  trail_entry.basic_var = basic_var;
  trail_entry.entering_var = entering_var;
  if (trail_this_pivot) {
    trail_entry.old_basic_row = rows_.at(basic_var);
  }

  // No violated_vars_ update here for basic_var (about to become
  // nonbasic): both of this function's call sites in check() always
  // immediately follow pivot() with update(basic_var, target), and
  // update()'s own top-level update_violation_status(nonbasic_var) call
  // clears basic_var's now-stale membership there instead -- see its
  // comment. entering_var's own post-pivot violation status is likewise
  // resolved via that same update() call's dependent-basic-variable loop
  // (entering_var's new row is built in terms of basic_var, so it's
  // always in occurs_in_[basic_var] by the time update() reads it).

  // basic_var is about to become nonbasic and stop contributing to f_var_'s
  // row entirely -- its tracked direction must be reset to the nonbasic
  // baseline (0) now, using its row content as it stood BEFORE this
  // pivot's rewrite (trail_entry.old_basic_row, already copied above).
  // Without this, a variable that later re-enters the basis would compute
  // its next set_soi_direction() delta against a stale historical
  // direction instead of the correct nonbasic baseline. f_var_ can never
  // be basic_var here (it's never selected as a violated variable, having
  // no bounds), so no exclusion is needed.
  if (rule_ == PivotingRule::Soi) {
    set_soi_direction(basic_var, 0, trail_entry.old_basic_row);
  }

  // No explicit erase of basic_var's slot: it's about to become nonbasic,
  // and rebuild_occurs_in_index() (the only place that ever iterates all
  // of rows_ rather than a specific known-basic index) gates by is_basic,
  // not by content, so the moved-from (empty) leftover here is harmless.
  Row old_row = std::move(rows_.at(basic_var));

  util::Rational entering_coeff(0);
  Row remainder;
  remainder.reserve(old_row.size());
  for (const auto& [var, coeff, var_slot] : old_row) {
    if (var == entering_var) {
      entering_coeff = coeff;
    } else {
      remainder.emplace_back(var, coeff);
    }
    // occurs_in_ removal, site 1 of 2: dissolving the leaving row.
    // basic_var stops being basic, so every variable that appeared in its
    // row loses basic_var from its occurrence list. Unconditional, once per
    // term -- the count is exactly sum(|row(leaving)|) over pivots, which is
    // bounded by row length and is the cheap site. Measured on
    // tropical-matrix/constraint-199552: 264,081 calls, 181M elements
    // scanned. See docs/open-leads.md lead 3b for the other site, which is
    // 45x more expensive.
    unlink_occurrence(var, var_slot);
  }

  const util::Rational inv = util::Rational(1) / entering_coeff;
  Row new_entering_row;
  new_entering_row.reserve(remainder.size() + 1);
  // basic_var takes basic_var's old row's place, so it is a new term whose
  // id has no relation to the remainder's -- splice it in at its ordered
  // position rather than prepending, or the Row invariant breaks here and
  // the affected-row merge below silently produces garbage. `remainder` is
  // ordered already, being a filtered copy of an ordered row.
  bool basic_var_placed = false;
  for (const auto& [var, coeff, var_slot] : remainder) {
    if (!basic_var_placed && basic_var < var) {
      new_entering_row.emplace_back(basic_var, inv);
      basic_var_placed = true;
    }
    new_entering_row.emplace_back(var, -(coeff * inv));
  }
  if (!basic_var_placed) {
    new_entering_row.emplace_back(basic_var, inv);
  }

  vars_.at(basic_var).is_basic = false;
  vars_.at(entering_var).is_basic = true;

  const std::vector<LraVarId> affected = occurs_in_[entering_var];
  occurs_in_[entering_var].clear();

  // What this pivot actually costs, in the two quantities item H3's sparsity
  // rule minimizes -- see row_update_count()/row_term_op_count(). Counting
  // them here rather than estimating from :lra.pivots is the difference
  // between explaining a result and guessing at one.
  row_update_count_ += affected.size();

  for (const LraVarId other_basic : affected) {
    // Deliberately a reference, not a copy: this row is needed only to read
    // from and is then handed to the trail, so it can be *moved* there once
    // the merge below has consumed it. Copying it here and again into the
    // trail cost two full row copies per affected row per pivot -- with 359
    // affected rows per pivot measured on scrambled228965, that was the
    // single largest source of the 465M Rational copy-constructions in the
    // 2026-07-31 profile. rows_ is never resized inside this loop, so the
    // reference stays valid.
    Row& other_row = rows_[other_basic];
    row_term_op_count_ += other_row.size() + new_entering_row.size();

    const auto entering_pos =
        std::lower_bound(other_row.begin(), other_row.end(), entering_var,
                         [](const RowEntry& term, LraVarId v) { return term.var < v; });
    const util::Rational entering_in_other = entering_pos->coeff;

    // Both inputs are ordered by var id (the Row invariant), so combining
    // them is a linear two-way merge -- heuristics-note item H8. This used to concatenate them into
    // combine_scratch_ and std::sort the result -- O(m log m) per affected
    // row, which a gprof profile put at 11.6% of runtime on
    // scrambled339450 (100M inner comparisons) before this change.
    Row rebuilt;
    rebuilt.reserve(other_row.size() + new_entering_row.size());
    std::size_t i = 0;
    std::size_t j = 0;
    while (true) {
      // entering_var leaves this row entirely: its contribution is the
      // `entering_in_other` factor the new row is scaled by.
      if (i < other_row.size() && other_row[i].var == entering_var) {
        ++i;
        continue;
      }
      const bool have_other = i < other_row.size();
      const bool have_entering = j < new_entering_row.size();
      if (!have_other && !have_entering) {
        break;
      }
      if (have_other && (!have_entering || other_row[i].var < new_entering_row[j].var)) {
        // Carried over unchanged, and nonzero by the Row invariant. The
        // occurrence entry does not move, so the slot carries across.
        rebuilt.emplace_back(other_row[i].var, other_row[i].coeff, other_row[i].occ_slot);
        ++i;
      } else if (have_entering && (!have_other || new_entering_row[j].var < other_row[i].var)) {
        // A product of two nonzero rationals, so it cannot be zero either.
        rebuilt.emplace_back(
            new_entering_row[j].var, entering_in_other * new_entering_row[j].coeff,
            static_cast<std::uint32_t>(occurs_in_[new_entering_row[j].var].size()));
        occurs_in_[new_entering_row[j].var].push_back(other_basic);
        ++j;
      } else {
        // Present on both sides. The coefficients add, and this is the only
        // place one can cancel to zero -- the Row invariant says a stored
        // coefficient never is.
        util::Rational sum = other_row[i].coeff + entering_in_other * new_entering_row[j].coeff;
        const LraVarId var = other_row[i].var;
        if (sum.is_zero()) {
          // occurs_in_ removal, site 2 of 2: exact cancellation.
          // The variable leaves this row, so its occurrence entry goes with
          // it. This is the only removal *this loop* performs -- entering_var
          // needs none (its whole list was cleared when it became basic), and
          // every other term either stays put or is new. Site 1, above in
          // this same function, dissolves the leaving row unconditionally.
          //
          // Do not read "the only removal" as "rare". This fires whenever two
          // coefficients sum to exactly zero, which on a definition-DAG
          // formula is common: measured at 9.4% of all merged terms on
          // tropical-matrix/constraint-199552 -- 12.0M calls scanning 8.17
          // BILLION elements, against 128.4M merged terms in total. That is
          // ~2.0s of a 4.4s solve and is why pivot() holds 74% self time
          // while Rational copies hold 1.05%. The linear scan below is the
          // single largest measured cost in the arithmetic layer;
          // docs/open-leads.md lead 3b records the candidate fixes.
          unlink_occurrence(var, other_row[i].occ_slot);
        } else {
          // Present before and after: the coefficient changed, the
          // occurrence entry did not, so the slot carries across.
          rebuilt.emplace_back(var, std::move(sum), other_row[i].occ_slot);
        }
        ++i;
        ++j;
      }
    }
    if (trail_this_pivot) {
      trail_entry.old_other_rows.emplace_back(other_basic, std::move(other_row));
    }
    rows_[other_basic] = std::move(rebuilt);
  }

  for (auto& entry : new_entering_row) {
    entry.occ_slot = static_cast<std::uint32_t>(occurs_in_[entry.var].size());
    occurs_in_[entry.var].push_back(entering_var);
  }
  rows_[entering_var] = std::move(new_entering_row);

  if (trail_this_pivot) {
    trail_.push_back(std::move(trail_entry));
  }
}

bool LraTableau::is_fixed(LraVarId var) const {
  const VarState& state = vars_[var];
  return state.lower.has_value() && state.upper.has_value() &&
         state.lower->value == state.upper->value;
}

bool LraTableau::is_unbounded(LraVarId var) const {
  const VarState& state = vars_[var];
  return !state.lower.has_value() && !state.upper.has_value();
}

LraVarId LraTableau::select_elimination_entering_variable(const Row& row) const {
  LraVarId best = vars_.size();
  bool best_unbounded = false;
  std::size_t best_occurrences = 0;
  bool best_unit = false;

  for (const auto& [var, coeff, var_slot] : row) {
    // Never pivot a fixed variable *into* the basis -- that is precisely the
    // state this whole mechanism exists to get out of, and it would trade one
    // permanently-repair-demanding row for another. check()'s own can_enter()
    // rejects fixed variables for the same reason; this is the elimination
    // path's equivalent. If every candidate is fixed then every term of the
    // row is pinned, so the row can never move and never demands a repair --
    // leaving it basic costs nothing, and if its own fixed value disagrees
    // with its terms that is a real conflict for check() to report.
    if (is_fixed(var)) {
      continue;
    }
    const bool unbounded = is_unbounded(var);
    const std::size_t occurrences = occurs_in_[var].size();
    // Same reason select_sparse_entering_variable() prefers it: pivoting on
    // +/-1 keeps 1/coeff integral, so the rewrite introduces no new
    // denominators and coefficients stay on util::Rational's machine-word
    // fast path.
    const bool unit = coeff == util::Rational(1) || coeff == util::Rational(-1);
    bool better = best == vars_.size();
    if (!better) {
      if (unbounded != best_unbounded) {
        better = unbounded;
      } else if (occurrences != best_occurrences) {
        better = occurrences < best_occurrences;
      } else if (unit != best_unit) {
        better = unit;
      } else {
        better = var < best;
      }
    }
    if (better) {
      best = var;
      best_unbounded = unbounded;
      best_occurrences = occurrences;
      best_unit = unit;
    }
  }
  return best;
}

// Item H7 -- see set_fixed_elimination() for the rationale and the soundness
// argument. Called from assert_upper/assert_lower's basic-variable branch.
bool LraTableau::try_eliminate_fixed_basic(LraVarId var) {
  if (!fixed_elimination_) {
    return false;
  }
  const VarState& state = vars_[var];
  if (!state.is_basic || !is_fixed(var)) {
    return false;
  }
  ++fixed_variable_count_;
  // An empty row is the constant 0 (reachable -- see add_row()). There is
  // nothing to pivot on, and if the fixed value is non-zero this is a genuine
  // conflict, which check() reports through its usual no-entering-candidate
  // path. Leaving it alone is what keeps that path reachable.
  if (rows_[var].empty()) {
    return false;
  }
  const LraVarId entering = select_elimination_entering_variable(rows_[var]);
  if (entering == vars_.size()) {
    return false;
  }
  // Copy before pivot(), which moves out of rows_[var].
  const DeltaRational target = state.lower->value;

  ++elimination_pivot_count_;
  pivot(var, entering);
  // Non-basic variables must always sit within their own bounds, so pinning
  // `var` at its fixed value is not optional -- and it is exactly what makes
  // it inert, since can_enter() then rejects it on every branch. This also
  // propagates the resulting change into every dependent row, `entering`'s
  // included, so `entering`'s assignment is correct at the point it may be
  // frozen below.
  update(var, target);

  return true;
}

std::vector<sat::Lit> LraTableau::build_conflict(LraVarId violated, bool too_low) const {
  const VarState& violated_state = vars_.at(violated);
  std::vector<sat::Lit> reasons;
  reasons.push_back(too_low ? violated_state.lower->reason : violated_state.upper->reason);
  for (const auto& [var, coeff, var_slot] : rows_.at(violated)) {
    const VarState& var_state = vars_.at(var);
    const bool positive = coeff.sign() > 0;
    const bool want_upper = (too_low && positive) || (!too_low && !positive);
    reasons.push_back(want_upper ? var_state.upper->reason : var_state.lower->reason);
  }
  return reasons;
}

int LraTableau::soi_direction(LraVarId var) const {
  const VarState& state = vars_.at(var);
  if (state.lower.has_value() && state.assignment < state.lower->value) {
    return -1;
  }
  if (state.upper.has_value() && state.assignment > state.upper->value) {
    return 1;
  }
  return 0;
}

void LraTableau::set_soi_direction(LraVarId var, int new_direction, const Row& var_row) {
  int& stored = soi_directions_[var];
  if (new_direction == stored) {
    return;
  }
  const int delta = new_direction - stored;
  const int previous = stored;
  stored = new_direction;

  trail_.push_back(FRowTrailEntry{rows_.at(*f_var_), var, previous});

  // Reused member -- see combine_scratch_'s own comment.
  combine_scratch_.clear();
  for (const auto& [term_var, coeff, term_var_slot] : rows_.at(*f_var_)) {
    combine_scratch_.emplace_back(term_var, coeff);
  }
  const util::Rational delta_rational(delta);
  for (const auto& [term_var, coeff, term_var_slot] : var_row) {
    combine_scratch_.emplace_back(term_var, coeff * delta_rational);
  }
  std::sort(combine_scratch_.begin(), combine_scratch_.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  Row& f_row = rows_.at(*f_var_);
  for (const auto& [term_var, coeff, term_var_slot] : f_row) {
    unlink_occurrence(term_var, term_var_slot);
  }
  Row rebuilt;
  rebuilt.reserve(combine_scratch_.size());
  std::size_t combine_index = 0;
  while (combine_index < combine_scratch_.size()) {
    const LraVarId term_var = combine_scratch_[combine_index].first;
    util::Rational sum = combine_scratch_[combine_index].second;
    ++combine_index;
    while (combine_index < combine_scratch_.size() &&
           combine_scratch_[combine_index].first == term_var) {
      sum += combine_scratch_[combine_index].second;
      ++combine_index;
    }
    if (sum.is_zero()) {
      continue;
    }
    rebuilt.emplace_back(term_var, sum, static_cast<std::uint32_t>(occurs_in_[term_var].size()));
    occurs_in_[term_var].push_back(*f_var_);
  }
  f_row = std::move(rebuilt);
}

namespace {
// One candidate crossing point for a single entering-variable walk
// (SOISELECT, Fig 5 + Section V-A): the point (as a signed delta from the
// entering variable's current assignment) at which row `k` (or the
// entering variable itself, if k==j) reaches one of its own bounds.
struct SoiBreakPoint {
  DeltaRational delta;
  LraVarId k;
  bool is_upper;
};
}  // namespace

std::optional<LraTableau::SoiSelection> LraTableau::soi_select() const {
  const DeltaRational kZero = DeltaRational::from_rational(util::Rational(0));
  const Row& f_row = rows_.at(*f_var_);

  bool have_best = false;
  int best_sign = 0;
  util::Rational best_abs_score(0);
  LraVarId best_j = 0;
  SoiSelection best_selection{0, 0, kZero};

  for (const auto& [j, t0j, j_slot] : f_row) {
    if (t0j.is_zero()) {
      continue;
    }
    const VarState& j_state = vars_.at(j);
    const bool t0j_positive = t0j.sign() > 0;
    // Flex(f) membership (eq 2, specialized to f's own row, implicit d=1):
    // decreasing j helps if t0j>0 and j isn't pinned at its lower bound;
    // increasing j helps if t0j<0 and j isn't pinned at its upper bound.
    const bool eligible =
        t0j_positive ? (!j_state.lower.has_value() || j_state.assignment > j_state.lower->value)
                     : (!j_state.upper.has_value() || j_state.assignment < j_state.upper->value);
    if (!eligible) {
      continue;
    }
    const bool delta_positive = !t0j_positive;  // walk direction that reduces f
    const int walk_sign = delta_positive ? 1 : -1;

    std::vector<SoiBreakPoint> points;
    // j's own bound -- caps how far the walk can go, contributes no beta
    // update (nonbasic variables are always within bounds, d_j stays 0).
    if (delta_positive) {
      if (j_state.upper.has_value()) {
        points.push_back(SoiBreakPoint{j_state.upper->value - j_state.assignment, j, true});
      }
    } else {
      if (j_state.lower.has_value()) {
        points.push_back(SoiBreakPoint{j_state.lower->value - j_state.assignment, j, false});
      }
    }
    // Every row k currently referencing j (excluding f_var_ itself, which
    // has no bounds and so contributes no breakpoint).
    for (const LraVarId k : occurs_in(j)) {
      if (k == *f_var_) {
        continue;
      }
      util::Rational coeff(0);
      for (const auto& [var, c, var_slot] : rows_.at(k)) {
        if (var == j) {
          coeff = c;
          break;
        }
      }
      if (coeff.is_zero()) {
        continue;
      }
      const VarState& k_state = vars_.at(k);
      const util::Rational inv = util::Rational(1) / coeff;
      if (k_state.lower.has_value()) {
        const DeltaRational point_delta = (k_state.lower->value - k_state.assignment).scaled(inv);
        if ((delta_positive && point_delta >= kZero) || (!delta_positive && point_delta <= kZero)) {
          points.push_back(SoiBreakPoint{point_delta, k, false});
        }
      }
      if (k_state.upper.has_value()) {
        const DeltaRational point_delta = (k_state.upper->value - k_state.assignment).scaled(inv);
        if ((delta_positive && point_delta >= kZero) || (!delta_positive && point_delta <= kZero)) {
          points.push_back(SoiBreakPoint{point_delta, k, true});
        }
      }
    }
    if (points.empty()) {
      continue;
    }
    std::sort(points.begin(), points.end(), [&](const SoiBreakPoint& a, const SoiBreakPoint& b) {
      return delta_positive ? a.delta < b.delta : b.delta < a.delta;
    });
    // The entering variable's own bound caps the walk -- discard any
    // breakpoints beyond it (moving j past its own bound isn't a valid
    // UPDATE at all).
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (points[i].k == j) {
        points.resize(i + 1);
        break;
      }
    }

    DeltaRational current_delta = kZero;
    util::Rational beta = t0j;
    DeltaRational delta_vio = kZero;
    bool have_best_for_j = false;
    DeltaRational best_delta_for_j = kZero;
    LraVarId best_k_for_j = j;
    DeltaRational best_delta_vio_for_j = kZero;

    std::size_t index = 0;
    while (index < points.size()) {
      const DeltaRational this_delta = points[index].delta;
      delta_vio = delta_vio + (this_delta - current_delta).scaled(beta);
      current_delta = this_delta;
      std::size_t group_end = index;
      while (group_end < points.size() && points[group_end].delta == this_delta) {
        const LraVarId k = points[group_end].k;
        const DeltaRational abs_delta = delta_positive ? this_delta : -this_delta;
        if (!have_best_for_j) {
          have_best_for_j = true;
          best_delta_for_j = this_delta;
          best_k_for_j = k;
          best_delta_vio_for_j = delta_vio;
        } else {
          const DeltaRational best_abs = delta_positive ? best_delta_for_j : -best_delta_for_j;
          if (delta_vio < best_delta_vio_for_j ||
              (delta_vio == best_delta_vio_for_j && abs_delta < best_abs) ||
              (delta_vio == best_delta_vio_for_j && abs_delta == best_abs && k < best_k_for_j)) {
            best_delta_for_j = this_delta;
            best_k_for_j = k;
            best_delta_vio_for_j = delta_vio;
          }
        }
        ++group_end;
      }
      // Section V-A: integrate to the crossing before changing its slope.
      // Away from zero, each crossed bound changes d_k by +/-1. At zero,
      // d_k already equals 0: only moving OUT of the interval changes it.
      // Keep the bound kind so a fixed variable's two zero events contribute
      // once, while its two nonzero crossings still contribute twice.
      // See docs/arithmetic.md, "SOI endpoint correction". j's own entry
      // never contributes because the walk stops at its bound.
      for (std::size_t g = index; g < group_end; ++g) {
        const LraVarId k = points[g].k;
        if (k == j) {
          continue;
        }
        util::Rational coeff(0);
        for (const auto& [var, c, var_slot] : rows_.at(k)) {
          if (var == j) {
            coeff = c;
            break;
          }
        }
        const bool increasing = (coeff.sign() > 0) == (walk_sign > 0);
        if (this_delta == kZero && increasing != points[g].is_upper) {
          continue;
        }
        const int delta_d = increasing ? 1 : -1;
        beta = beta + coeff * util::Rational(delta_d);
      }
      index = group_end;
    }
    if (!have_best_for_j) {
      continue;
    }

    const int sign = best_delta_vio_for_j == kZero ? 0 : (best_delta_vio_for_j < kZero ? -1 : 1);
    const util::Rational abs_score = t0j.sign() < 0 ? -t0j : t0j;
    if (!have_best) {
      have_best = true;
      best_sign = sign;
      best_abs_score = abs_score;
      best_j = j;
      best_selection = SoiSelection{j, best_k_for_j, best_delta_for_j};
    } else if (sign < best_sign || (sign == best_sign && abs_score > best_abs_score) ||
               (sign == best_sign && abs_score == best_abs_score && j < best_j)) {
      best_sign = sign;
      best_abs_score = abs_score;
      best_j = j;
      best_selection = SoiSelection{j, best_k_for_j, best_delta_for_j};
    }
  }

  if (!have_best) {
    return std::nullopt;
  }
  return best_selection;
}

// Section V-B's f_S = sum of d_i*row_i over only `subset` -- shared by
// build_soi_conflict (which needs f_S's own terms as conflict reasons)
// and soi_subset_still_conflicts (which needs Flex(f_S)'s eligibility
// check). Uses the CACHED soi_directions_ (not a fresh soi_direction()
// recomputation) since `subset` is always drawn from a known-error_vars
// list at a point where the cache is guaranteed consistent with live
// state (see set_soi_direction's invariant).
std::unordered_map<LraVarId, util::Rational> LraTableau::restricted_soi_row(
    const std::vector<LraVarId>& subset) const {
  std::unordered_map<LraVarId, util::Rational> restricted;
  for (const LraVarId basic_var : subset) {
    const int direction = soi_directions_.at(basic_var);
    if (direction == 0) {
      continue;
    }
    const util::Rational d(direction);
    for (const auto& [var, coeff, var_slot] : rows_.at(basic_var)) {
      restricted[var] += coeff * d;
    }
  }
  return restricted;
}

std::vector<sat::Lit> LraTableau::build_soi_conflict(
    const std::vector<LraVarId>& error_vars) const {
  // Section V-B: f_S restricted to `error_vars` (NOT the full f_var_ row,
  // which reflects every originally-violated row -- error_vars may be a
  // minimize_soi_conflict()-shrunk subset of that), same sign-branching
  // pattern as build_conflict (f has no bounds of its own, so unlike
  // build_conflict there's no "too_low" direction to condition on --
  // positive coefficient always wants the lower-bound reason, negative
  // always wants upper).
  std::vector<sat::Lit> reasons;
  for (const auto& [var, coeff] : restricted_soi_row(error_vars)) {
    if (coeff.is_zero()) {
      continue;
    }
    const VarState& var_state = vars_.at(var);
    reasons.push_back(coeff.sign() > 0 ? var_state.lower->reason : var_state.upper->reason);
  }
  for (const LraVarId error_var : error_vars) {
    const VarState& state = vars_.at(error_var);
    const int direction = soi_directions_.at(error_var);
    reasons.push_back(direction < 0 ? state.lower->reason : state.upper->reason);
  }
  // The quantity a minimization strategy is actually trying to move: the
  // clause length, not the size of the error set it was derived from.
  soi_conflict_lit_count_ += reasons.size();
  return reasons;
}

bool LraTableau::soi_subset_still_conflicts(const std::vector<LraVarId>& subset) const {
  // An empty subset is never a conflict (nothing is claimed violated, so
  // Vio(subset)=0 trivially) -- without this guard, f_empty is the zero
  // row, which vacuously has no eligible candidate and would otherwise
  // be (wrongly) treated as "stuck"/conflicting.
  if (subset.empty()) {
    return false;
  }
  const std::unordered_map<LraVarId, util::Rational> restricted = restricted_soi_row(subset);
  for (const auto& [j, t0j] : restricted) {
    if (t0j.is_zero()) {
      continue;
    }
    const VarState& j_state = vars_.at(j);
    const bool t0j_positive = t0j.sign() > 0;
    const bool eligible =
        t0j_positive ? (!j_state.lower.has_value() || j_state.assignment > j_state.lower->value)
                     : (!j_state.upper.has_value() || j_state.assignment < j_state.upper->value);
    if (eligible) {
      return false;
    }
  }
  return true;
}

// GCC 13 at -O3 mis-analyzes this function's recursive-inlining chain and
// emits a false-positive -Warray-bounds= on the `return candidates;` copy
// below (verified: the flagged copy is a plain, in-bounds vector copy
// construction, not an actual out-of-bounds access) -- suppressed
// locally rather than disabling the check project-wide.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds="
#pragma GCC diagnostic ignored "-Wstringop-overflow="
#endif
std::vector<LraVarId> LraTableau::quickxplain(const std::vector<LraVarId>& background,
                                              const std::vector<LraVarId>& candidates) const {
  if (candidates.empty()) {
    return {};
  }
  if (soi_subset_still_conflicts(background)) {
    return {};
  }
  if (candidates.size() == 1) {
    return candidates;
  }
  const auto half = static_cast<std::ptrdiff_t>(candidates.size() / 2);
  const std::vector<LraVarId> first(candidates.begin(), candidates.begin() + half);
  const std::vector<LraVarId> second(candidates.begin() + half, candidates.end());

  std::vector<LraVarId> background_with_first = background;
  background_with_first.insert(background_with_first.end(), first.begin(), first.end());
  const std::vector<LraVarId> second_result = quickxplain(background_with_first, second);

  std::vector<LraVarId> background_with_second_result = background;
  background_with_second_result.insert(background_with_second_result.end(), second_result.begin(),
                                       second_result.end());
  const std::vector<LraVarId> first_result = quickxplain(background_with_second_result, first);

  std::vector<LraVarId> combined = first_result;
  combined.insert(combined.end(), second_result.begin(), second_result.end());
  return combined;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

std::vector<LraVarId> LraTableau::minimize_soi_conflict(
    const std::vector<LraVarId>& error_vars) const {
  return quickxplain({}, error_vars);
}

std::vector<LraVarId> LraTableau::ordered_for_deletion(
    const std::vector<LraVarId>& error_vars) const {
  std::vector<LraVarId> ordered = error_vars;
  if (tuning_.soi_minimization_order == SoiMinimizationOrder::Id) {
    // error_vars already arrives id-ascending (violated_vars_ is a std::set),
    // but sort anyway so the arm does not silently depend on the caller.
    std::sort(ordered.begin(), ordered.end());
    return ordered;
  }

  // Worst first: deletion offers up what it visits first, so whatever we
  // least want in the final conflict must be offered up earliest. Both keys
  // sort descending, with the id ascending as the tie-break so the order is
  // total and reproducible.
  if (tuning_.soi_minimization_order == SoiMinimizationOrder::RowSupport) {
    std::sort(ordered.begin(), ordered.end(), [this](LraVarId lhs, LraVarId rhs) {
      const std::size_t lhs_support = rows_.at(lhs).size();
      const std::size_t rhs_support = rows_.at(rhs).size();
      if (lhs_support != rhs_support) {
        return lhs_support > rhs_support;
      }
      return lhs < rhs;
    });
    return ordered;
  }

  // How far the variable currently sits outside the bound it violates. The
  // direction says which bound that is -- the same convention
  // build_soi_conflict uses to pick the reason literal.
  const auto violation_of = [this](LraVarId var) {
    const VarState& state = vars_.at(var);
    const int direction = soi_directions_.at(var);
    if (direction < 0 && state.lower.has_value()) {
      return state.lower->value - state.assignment;
    }
    if (direction > 0 && state.upper.has_value()) {
      return state.assignment - state.upper->value;
    }
    return DeltaRational{};
  };
  std::sort(ordered.begin(), ordered.end(), [&violation_of](LraVarId lhs, LraVarId rhs) {
    const DeltaRational lhs_violation = violation_of(lhs);
    const DeltaRational rhs_violation = violation_of(rhs);
    if (lhs_violation != rhs_violation) {
      return rhs_violation < lhs_violation;
    }
    return lhs < rhs;
  });
  return ordered;
}

std::vector<LraVarId> LraTableau::delete_soi_conflict(
    const std::vector<LraVarId>& error_vars) const {
  std::vector<LraVarId> kept = ordered_for_deletion(error_vars);

  for (std::size_t index = 0; index < kept.size();) {
    std::vector<LraVarId> without = kept;
    without.erase(without.begin() + static_cast<std::ptrdiff_t>(index));
    // Only drop when what remains still certifies a conflict on its own.
    // This is what makes the result sound without relying on the predicate
    // being monotone -- see the declaration's comment.
    if (soi_subset_still_conflicts(without)) {
      kept = std::move(without);
      continue;  // same index now holds the next candidate
    }
    ++index;
  }
  return kept;
}

// Whether a non-basic variable with coefficient `coeff` in the violated
// variable's row can move in the direction that would repair the violation.
// Every candidate passing this test repairs it by the same amount, which is
// what makes the choice among them free with respect to progress -- see
// PivotingRule::Sparse's comment.
bool LraTableau::can_enter(const util::Rational& coeff, const VarState& state, bool too_low) const {
  const bool positive = coeff.sign() > 0;
  if (too_low) {
    return positive ? (!state.upper.has_value() || state.assignment < state.upper->value)
                    : (!state.lower.has_value() || state.assignment > state.lower->value);
  }
  return positive ? (!state.lower.has_value() || state.assignment > state.lower->value)
                  : (!state.upper.has_value() || state.assignment < state.upper->value);
}

LraVarId LraTableau::select_entering_variable(LraVarId violated, bool too_low, bool use_bland) {
  const Row& row = rows_.at(violated);

  if (use_bland) {
    // Bland's rule: smallest index among the eligible candidates.
    LraVarId entering = vars_.size();
    for (const auto& [var, coeff, var_slot] : row) {
      if (can_enter(coeff, vars_.at(var), too_low) && var < entering) {
        entering = var;
      }
    }
    return entering;
  }

  if (rule_ == PivotingRule::Sparse) {
    return select_sparse_entering_variable(row, too_low);
  }

  // Dantzig-style: pick the largest |coefficient| among eligible entering
  // candidates, tie-breaking by smallest index. The tie-break is an explicit
  // comparison because the scan is ordered by |coefficient|, not by var id --
  // the Bland branch above compares explicitly for the same reason. (Rows
  // *are* ordered by var id; that is the Row invariant pivot() relies on for
  // its two-way merge. An earlier version of this comment claimed otherwise.)
  LraVarId entering = vars_.size();
  util::Rational best_abs_coeff(0);
  for (const auto& [var, coeff, var_slot] : row) {
    if (!can_enter(coeff, vars_.at(var), too_low)) {
      continue;
    }
    const util::Rational abs_coeff = coeff.sign() < 0 ? -coeff : coeff;
    if (entering == vars_.size() || abs_coeff > best_abs_coeff ||
        (abs_coeff == best_abs_coeff && var < entering)) {
      entering = var;
      best_abs_coeff = abs_coeff;
    }
  }
  return entering;
}

// PivotingRule::Sparse's entering choice (heuristics-note item H3): among the
// eligible candidates, the one occurring in the fewest rows, because pivot()
// rewrites exactly those rows.
//
// The scan is bounded (Tuning::sparse_pricing_candidates) but only in how many
// *eligible* candidates it scores -- never in how much of the row it reads
// before concluding there are none. Returning "none" is what check() turns
// into a conflict, so an early exit before finding one would be unsound, not
// merely suboptimal. The loop below therefore only breaks after the counter
// has been incremented, i.e. after at least one candidate is in hand.
LraVarId LraTableau::select_sparse_entering_variable(const Row& row, bool too_low) {
  LraVarId entering = vars_.size();
  std::size_t best_occurrences = 0;
  bool best_has_unit_coeff = false;
  std::size_t scored = 0;

  const std::size_t size = row.size();
  const std::size_t start = size == 0 ? 0 : sparse_pricing_cursor_ % size;
  for (std::size_t offset = 0; offset < size; ++offset) {
    const auto& [var, coeff, var_slot] = row[(start + offset) % size];
    if (!can_enter(coeff, vars_.at(var), too_low)) {
      continue;
    }
    const std::size_t occurrences = occurs_in_[var].size();
    // Prefer pivoting on +/-1: then 1/coeff is integral and the rewrite
    // introduces no new denominators, keeping coefficients inside
    // util::Rational's machine-word fast path (observable as
    // :rational.promotions staying flat).
    const bool has_unit_coeff = coeff == util::Rational(1) || coeff == util::Rational(-1);
    const bool better = entering == vars_.size() || occurrences < best_occurrences ||
                        (occurrences == best_occurrences &&
                         (has_unit_coeff != best_has_unit_coeff ? has_unit_coeff : var < entering));
    if (better) {
      entering = var;
      best_occurrences = occurrences;
      best_has_unit_coeff = has_unit_coeff;
    }
    ++scored;
    if (tuning_.sparse_pricing_candidates != 0 && scored >= tuning_.sparse_pricing_candidates) {
      break;
    }
  }

  if (entering != vars_.size()) {
    ++sparse_pricing_cursor_;
  }
  return entering;
}

namespace {

// Hashes a DeltaRational by both components. Combining with the same mix
// constant used elsewhere in the codebase; correctness needs only that equal
// DeltaRationals hash equal, which follows from Rational's canonical-form
// invariant (see util/rational.hpp) applied to each component.
struct DeltaRationalHash {
  std::size_t operator()(const DeltaRational& value) const {
    const util::RationalHash hash;
    const std::size_t a = hash(value.rational);
    const std::size_t b = hash(value.infinitesimal);
    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6U) + (a >> 2U));
  }
};

struct RowModelKey {
  std::vector<std::pair<LraVarId, util::Rational>> nonfixed_terms;
  DeltaRational fixed_offset;

  bool operator==(const RowModelKey& other) const = default;
};

struct RowModelKeyHash {
  std::size_t operator()(const RowModelKey& key) const {
    std::size_t seed = DeltaRationalHash{}(key.fixed_offset);
    const util::RationalHash rational_hash;
    for (const auto& [var, coefficient] : key.nonfixed_terms) {
      const std::size_t term_hash =
          var ^ (rational_hash(coefficient) + 0x9e3779b97f4a7c15ULL + (var << 6U) + (var >> 2U));
      seed ^= term_hash + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }
    return seed;
  }
};

// The partition of `shared` by model value, shared by model_equal_classes()
// and by the mutation sweep, which needs the same grouping against a scratch
// assignment. Templated on the value lookup so the live path keeps costing
// O(|shared|) rather than materializing an assignment vector to call this.
//
// `total` counts ALL classes, singletons included -- that is the paper's
// |classes(R_beta)|, the quantity a model mutator must not decrease. `classes`
// keeps only the non-singleton ones, which is what a caller can act on.
struct ModelValueGrouping {
  std::vector<std::vector<LraVarId>> classes;
  std::size_t total = 0;
};

template <typename ValueOf>
ModelValueGrouping group_by_model_value(const std::vector<LraVarId>& shared, ValueOf value_of) {
  // Insertion order is preserved inside each class, and classes come out in
  // first-appearance order, so the result is deterministic for a given
  // `shared` -- an unordered_map iteration would not be, and a combination
  // layer that case-splits on this must not depend on hash order.
  std::unordered_map<DeltaRational, std::size_t, DeltaRationalHash> class_of_value;
  std::unordered_set<LraVarId> seen;
  ModelValueGrouping grouping;

  for (const LraVarId var : shared) {
    if (!seen.insert(var).second) {
      continue;  // duplicates in `shared` are the caller's business, not ours
    }
    const auto [it, inserted] = class_of_value.emplace(value_of(var), grouping.classes.size());
    if (inserted) {
      grouping.classes.push_back({var});
    } else {
      grouping.classes[it->second].push_back(var);
    }
  }
  grouping.total = grouping.classes.size();

  // Singletons carry no information for the combination layer: a variable
  // equal to nothing else proposes no equality. Dropping them here keeps the
  // caller from having to filter, and keeps the common case (nothing
  // coincides) cheap to test -- an empty result means "no candidates".
  std::erase_if(grouping.classes, [](const std::vector<LraVarId>& c) { return c.size() < 2; });
  return grouping;
}

}  // namespace

std::vector<std::vector<LraVarId>> LraTableau::model_equal_classes(
    const std::vector<LraVarId>& shared) const {
  // Keying on DeltaRational and not on its rational part is load-bearing --
  // see the header's note on why two variables agreeing rationally but
  // differing in delta are not equal in the concrete model.
  return group_by_model_value(
             shared,
             [this](LraVarId var) -> const DeltaRational& { return vars_.at(var).assignment; })
      .classes;
}

std::vector<LraTableau::EntailedEquality> LraTableau::entailed_equalities(
    const std::vector<LraVarId>& shared) const {
  std::vector<EntailedEquality> result;
  if (shared.empty()) {
    return result;
  }

  // Deduplicated, and in the caller's order so the output is deterministic.
  std::vector<LraVarId> vars;
  std::unordered_set<LraVarId> in_shared;
  for (const LraVarId var : shared) {
    if (in_shared.insert(var).second) {
      vars.push_back(var);
    }
  }

  // A variable is *fixed* when both bounds exist and coincide. Every rule here
  // rests on this, and note the bounds are checked for presence rather than
  // dereferenced blind: unlike build_conflict, which may assume its caller
  // established that no entering candidate exists, this runs speculatively and
  // a variable with only one bound is ordinary, not a bug.
  const auto fixed_value = [this](LraVarId var) -> std::optional<DeltaRational> {
    const VarState& state = vars_.at(var);
    if (!state.lower.has_value() || !state.upper.has_value()) {
      return std::nullopt;
    }
    if (state.lower->value != state.upper->value) {
      return std::nullopt;
    }
    return state.lower->value;
  };
  const auto append_fixing_reasons = [this](LraVarId var, std::vector<sat::Lit>& into) {
    const VarState& state = vars_.at(var);
    into.push_back(state.lower->reason);
    into.push_back(state.upper->reason);
  };

  std::set<std::pair<LraVarId, LraVarId>> emitted;
  const auto emit = [&emitted, &result](LraVarId a, LraVarId b, std::vector<sat::Lit> reasons,
                                        EntailedEqualityRule rule) {
    const std::pair<LraVarId, LraVarId> key = a < b ? std::pair{a, b} : std::pair{b, a};
    if (!emitted.insert(key).second) {
      return;
    }
    result.push_back(EntailedEquality{key.first, key.second, std::move(reasons), rule});
  };

  // -- FixedEq -------------------------------------------------------------
  // Group the fixed shared variables by their fixed value, linking each to the
  // first variable seen with that value.
  std::unordered_map<DeltaRational, LraVarId, DeltaRationalHash> representative;
  for (const LraVarId var : vars) {
    const std::optional<DeltaRational> value = fixed_value(var);
    if (!value.has_value()) {
      continue;
    }
    const auto [it, inserted] = representative.emplace(*value, var);
    if (inserted) {
      continue;
    }
    std::vector<sat::Lit> reasons;
    reasons.reserve(4);
    append_fixing_reasons(it->second, reasons);
    append_fixing_reasons(var, reasons);
    emit(it->second, var, std::move(reasons), EntailedEqualityRule::FixedEq);
  }

  // -- EqRow ---------------------------------------------------------------
  // For a basic shared x_i with row x_i = sum a_k x_k, a shared term x_j whose
  // coefficient is exactly 1 splits the row as x_i = x_j + P. If every other
  // variable is fixed then P is fixed, and if beta(P) = 0 then x_i - x_j = 0 in
  // every model.
  //
  // Only coefficient 1 qualifies: at -1 the row says x_i = -x_j + P, which is
  // not an equality between them.
  for (const LraVarId basic : vars) {
    if (!vars_.at(basic).is_basic) {
      continue;
    }
    const Row& row = rows_.at(basic);

    // P is the whole row minus the candidate term, so its fixedness and value
    // do not depend on which candidate is chosen: compute once.
    DeltaRational row_value_of_fixed;
    std::vector<LraVarId> fixed_vars;
    std::vector<LraVarId> unfixed_vars;
    for (const auto& [var, coeff, slot] : row) {
      const std::optional<DeltaRational> value = fixed_value(var);
      if (value.has_value()) {
        fixed_vars.push_back(var);
        row_value_of_fixed = row_value_of_fixed + value->scaled(coeff);
      } else {
        unfixed_vars.push_back(var);
      }
    }
    // At most one non-fixed term may remain, and it must be the candidate x_j.
    if (unfixed_vars.size() > 1) {
      continue;
    }

    for (const auto& [var, coeff, slot] : row) {
      if (var == basic || !in_shared.contains(var) || coeff != util::Rational(1)) {
        continue;
      }
      if (!unfixed_vars.empty() && unfixed_vars.front() != var) {
        continue;  // some *other* term is unfixed, so P is not fixed
      }
      // beta(P) = (sum over fixed terms) minus this term's contribution when it
      // is itself fixed; a fixed candidate contributes to row_value_of_fixed.
      DeltaRational p_value = row_value_of_fixed;
      const std::optional<DeltaRational> candidate_fixed = fixed_value(var);
      if (candidate_fixed.has_value()) {
        p_value = p_value - candidate_fixed->scaled(coeff);
      }
      if (p_value != DeltaRational{}) {
        continue;
      }
      std::vector<sat::Lit> reasons;
      for (const LraVarId fixed : fixed_vars) {
        if (fixed == var) {
          continue;  // the candidate is not part of P
        }
        append_fixing_reasons(fixed, reasons);
      }
      emit(basic, var, std::move(reasons), EntailedEqualityRule::EqRow);
    }
  }

  // -- EqOffsetRows / EqRows ----------------------------------------------
  // Split each shared basic row into its non-fixed polynomial P and fixed
  // remainder P_i. Rows with the same P and beta(P_i) define equal basics.
  // The single coefficient-one P=x_k case is the paper's cheaper
  // EqOffsetRows rule; all other signatures use EqRows. Keeping the first row
  // per key emits a spanning set instead of every pair in the bucket.
  struct IndexedRow {
    LraVarId basic = 0;
    std::vector<LraVarId> fixed_vars;
  };
  std::unordered_map<RowModelKey, IndexedRow, RowModelKeyHash> offset_rows;
  std::unordered_map<RowModelKey, IndexedRow, RowModelKeyHash> general_rows;
  for (const LraVarId basic : vars) {
    if (!vars_.at(basic).is_basic) {
      continue;
    }
    RowModelKey key;
    IndexedRow indexed{.basic = basic, .fixed_vars = {}};
    for (const auto& [var, coefficient, slot] : rows_.at(basic)) {
      const std::optional<DeltaRational> value = fixed_value(var);
      if (value.has_value()) {
        indexed.fixed_vars.push_back(var);
        key.fixed_offset = key.fixed_offset + value->scaled(coefficient);
      } else {
        key.nonfixed_terms.emplace_back(var, coefficient);
      }
    }

    const bool is_offset =
        key.nonfixed_terms.size() == 1 && key.nonfixed_terms.front().second == util::Rational(1);
    auto& index = is_offset ? offset_rows : general_rows;
    const auto [bucket, inserted] = index.emplace(key, indexed);
    if (inserted) {
      continue;
    }
    std::vector<sat::Lit> reasons;
    reasons.reserve(2 * (bucket->second.fixed_vars.size() + indexed.fixed_vars.size()));
    for (const LraVarId fixed : bucket->second.fixed_vars) {
      append_fixing_reasons(fixed, reasons);
    }
    for (const LraVarId fixed : indexed.fixed_vars) {
      append_fixing_reasons(fixed, reasons);
    }
    std::sort(reasons.begin(), reasons.end(),
              [](sat::Lit lhs, sat::Lit rhs) { return lhs.value < rhs.value; });
    reasons.erase(std::unique(reasons.begin(), reasons.end(),
                              [](sat::Lit lhs, sat::Lit rhs) { return lhs.value == rhs.value; }),
                  reasons.end());
    emit(bucket->second.basic, basic, std::move(reasons),
         is_offset ? EntailedEqualityRule::EqOffsetRows : EntailedEqualityRule::EqRows);
  }

  return result;
}

LraTableau::ModelFilteredEqualityResult LraTableau::model_filtered_unit_equalities(
    const std::vector<LraVarId>& shared) const {
  ModelFilteredEqualityResult result;
  if (shared.empty()) {
    return result;
  }

  const ModelValueGrouping grouping = group_by_model_value(
      shared, [this](LraVarId var) -> const DeltaRational& { return vars_.at(var).assignment; });
  result.stats.model_classes = grouping.classes.size();

  struct SharedVarInfo {
    std::size_t class_index = 0;
    std::size_t parent_index = 0;
  };
  std::unordered_map<LraVarId, SharedVarInfo> shared_var_info;
  shared_var_info.reserve(shared.size());
  std::vector<std::size_t> parent;
  parent.reserve(shared.size());
  for (std::size_t class_index = 0; class_index < grouping.classes.size(); ++class_index) {
    const std::vector<LraVarId>& equal_class = grouping.classes[class_index];
    result.stats.class_terms += equal_class.size();
    result.stats.candidate_pairs += equal_class.size() * (equal_class.size() - 1) / 2;
    for (const LraVarId var : equal_class) {
      shared_var_info.emplace(var, SharedVarInfo{class_index, parent.size()});
      parent.push_back(parent.size());
    }
  }

  const auto fixed = [this](LraVarId var) {
    const VarState& state = vars_.at(var);
    return state.lower.has_value() && state.upper.has_value() &&
           state.lower->value == state.upper->value;
  };
  const auto append_fixing_reasons = [this](LraVarId var, std::vector<sat::Lit>& reasons) {
    const VarState& state = vars_.at(var);
    reasons.push_back(state.lower->reason);
    reasons.push_back(state.upper->reason);
  };
  const auto root = [&parent](std::size_t index) {
    while (parent[index] != index) {
      parent[index] = parent[parent[index]];
      index = parent[index];
    }
    return index;
  };
  const auto emit = [&result, &parent, &root, &shared_var_info](LraVarId lhs, LraVarId rhs,
                                                                std::vector<sat::Lit> reasons,
                                                                EntailedEqualityRule rule) {
    const std::size_t lhs_root = root(shared_var_info.at(lhs).parent_index);
    const std::size_t rhs_root = root(shared_var_info.at(rhs).parent_index);
    if (lhs_root == rhs_root) {
      return false;
    }
    parent[rhs_root] = lhs_root;
    std::sort(reasons.begin(), reasons.end(),
              [](sat::Lit a, sat::Lit b) { return a.value < b.value; });
    reasons.erase(std::unique(reasons.begin(), reasons.end(),
                              [](sat::Lit a, sat::Lit b) { return a.value == b.value; }),
                  reasons.end());
    result.stats.reason_literals += reasons.size();
    if (rhs < lhs) {
      std::swap(lhs, rhs);
    }
    result.equalities.push_back(EntailedEquality{lhs, rhs, std::move(reasons), rule});
    return true;
  };

  // Fixed variables in one exact model class have the same fixed value. No
  // numeric value needs to be placed in another hash table.
  for (const std::vector<LraVarId>& equal_class : grouping.classes) {
    std::optional<LraVarId> representative;
    for (const LraVarId var : equal_class) {
      if (!fixed(var)) {
        continue;
      }
      if (!representative.has_value()) {
        representative = var;
        continue;
      }
      std::vector<sat::Lit> reasons;
      reasons.reserve(4);
      append_fixing_reasons(*representative, reasons);
      append_fixing_reasons(var, reasons);
      ++result.stats.fixed_matches;
      emit(*representative, var, std::move(reasons), EntailedEqualityRule::FixedEq);
    }
  }

  struct ConnectorKey {
    std::size_t class_index = 0;
    LraVarId connector = 0;
    int sign = 0;

    bool operator==(const ConnectorKey&) const = default;
  };
  struct ConnectorKeyHash {
    std::size_t operator()(const ConnectorKey& key) const {
      constexpr std::size_t kHashMix = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
      std::size_t seed = key.class_index;
      seed ^= key.connector + kHashMix + (seed << 6U) + (seed >> 2U);
      const std::size_t sign_hash = key.sign > 0 ? std::size_t{1} : std::size_t{0};
      seed ^= sign_hash + kHashMix + (seed << 6U) + (seed >> 2U);
      return seed;
    }
  };
  struct ConnectorRow {
    LraVarId basic = 0;
    std::vector<LraVarId> fixed_vars;
  };
  std::unordered_map<ConnectorKey, ConnectorRow, ConnectorKeyHash> connector_rows;
  connector_rows.reserve(result.stats.class_terms);

  for (std::size_t class_index = 0; class_index < grouping.classes.size(); ++class_index) {
    for (const LraVarId basic : grouping.classes[class_index]) {
      if (!vars_.at(basic).is_basic) {
        continue;
      }
      const Row& row = rows_.at(basic);
      ++result.stats.rows_inspected;
      result.stats.nonzeros_inspected += row.size();

      std::vector<LraVarId> fixed_vars;
      fixed_vars.reserve(row.size());
      std::size_t nonfixed_count = 0;
      LraVarId nonfixed_var = 0;
      int nonfixed_sign = 0;
      for (const auto& [var, coefficient, slot] : row) {
        if (fixed(var)) {
          fixed_vars.push_back(var);
        } else {
          ++nonfixed_count;
          nonfixed_var = var;
          if (coefficient == util::Rational(1)) {
            nonfixed_sign = 1;
          } else if (coefficient == util::Rational(-1)) {
            nonfixed_sign = -1;
          } else {
            nonfixed_sign = 0;
          }
        }
      }

      // x = y + alpha. Equal model values establish beta(alpha) = 0, and
      // fixedness makes that value invariant across all current models.
      for (const auto& [var, coefficient, slot] : row) {
        const auto candidate = shared_var_info.find(var);
        if (coefficient != util::Rational(1) || candidate == shared_var_info.end() ||
            candidate->second.class_index != class_index) {
          continue;
        }
        if (nonfixed_count != 0 && (nonfixed_count != 1 || nonfixed_var != var)) {
          continue;
        }
        std::vector<sat::Lit> reasons;
        reasons.reserve(2 * fixed_vars.size());
        for (const LraVarId fixed_var : fixed_vars) {
          if (fixed_var != var) {
            append_fixing_reasons(fixed_var, reasons);
          }
        }
        ++result.stats.one_row_matches;
        emit(basic, var, std::move(reasons), EntailedEqualityRule::EqRow);
      }

      if (nonfixed_count != 1) {
        if (nonfixed_count > 1) {
          ++result.stats.nonfixed_row_rejections;
        }
        continue;
      }
      if (nonfixed_sign == 0) {
        ++result.stats.nonunit_rejections;
        continue;
      }

      const ConnectorKey key{class_index, nonfixed_var, nonfixed_sign};
      const auto [previous, inserted] =
          connector_rows.emplace(key, ConnectorRow{basic, fixed_vars});
      if (inserted) {
        continue;
      }
      std::vector<sat::Lit> reasons;
      reasons.reserve(2 * (previous->second.fixed_vars.size() + fixed_vars.size()));
      for (const LraVarId fixed_var : previous->second.fixed_vars) {
        append_fixing_reasons(fixed_var, reasons);
      }
      for (const LraVarId fixed_var : fixed_vars) {
        append_fixing_reasons(fixed_var, reasons);
      }
      ++result.stats.two_row_matches;
      emit(previous->second.basic, basic, std::move(reasons), EntailedEqualityRule::EqOffsetRows);
    }
  }

  return result;
}

void LraTableau::set_nonbasic_value(LraVarId var, const DeltaRational& value) {
  assert(!vars_.at(var).is_basic &&
         "set_nonbasic_value is defined for nonbasic variables only -- a basic "
         "variable's value follows from its row");
  update(var, value);
}

LraTableau::FreedomInterval LraTableau::freedom_interval(LraVarId nonbasic) const {
  return freedom_interval_impl(nonbasic, nullptr);
}

LraTableau::FreedomInterval LraTableau::freedom_interval_impl(
    LraVarId nonbasic, const std::vector<DeltaRational>* beta) const {
  assert(!vars_.at(nonbasic).is_basic &&
         "freedom_interval is defined for nonbasic variables only -- a basic "
         "variable's value follows from its row, and Section 5.2 mutates "
         "without pivoting one free");
  const auto value_of = [this, beta](LraVarId var) -> const DeltaRational& {
    return beta != nullptr ? (*beta)[var] : vars_.at(var).assignment;
  };

  FreedomInterval interval;
  // Start from the variable's own bounds; the row constraints only ever
  // tighten from here, which is why {l_j} and {u_j} are members of the paper's
  // max/min sets rather than special cases.
  if (const auto& own_lower = vars_.at(nonbasic).lower; own_lower.has_value()) {
    interval.lower = own_lower->value;
  }
  if (const auto& own_upper = vars_.at(nonbasic).upper; own_upper.has_value()) {
    interval.upper = own_upper->value;
  }

  const DeltaRational& beta_j = value_of(nonbasic);
  const auto tighten_lower = [&interval](const DeltaRational& candidate) {
    if (!interval.lower.has_value() || *interval.lower < candidate) {
      interval.lower = candidate;
    }
  };
  const auto tighten_upper = [&interval](const DeltaRational& candidate) {
    if (!interval.upper.has_value() || candidate < *interval.upper) {
      interval.upper = candidate;
    }
  };

  // occurs_in_ is the column of `nonbasic`: exactly the basic variables whose
  // value moves when this one does. Walking it (rather than every row) is what
  // makes this O(column) -- the same work update() already does to repair
  // those same rows.
  for (const LraVarId basic : occurs_in_.at(nonbasic)) {
    if (!vars_.at(basic).is_basic) {
      continue;  // stale occurrence; occurs_in_ is a superset between rebuilds
    }
    const Row& basic_row = rows_.at(basic);
    const auto entry = std::find_if(basic_row.begin(), basic_row.end(),
                                    [nonbasic](const RowEntry& e) { return e.var == nonbasic; });
    if (entry == basic_row.end() || entry->coeff.is_zero()) {
      continue;
    }

    const VarState& basic_state = vars_.at(basic);
    const DeltaRational& beta_i = value_of(basic);
    // How far beta(x_i) may move before it hits its own bound, converted into
    // a bound on x_j via division by a_ij. The sign of a_ij decides which of
    // x_i's bounds limits which direction, which is the whole content of the
    // paper's four-way case split.
    const util::Rational inverse_coeff = util::Rational(1) / entry->coeff;
    const bool positive = entry->coeff.sign() > 0;

    if (basic_state.lower.has_value()) {
      const DeltaRational shifted =
          beta_j + (basic_state.lower->value - beta_i).scaled(inverse_coeff);
      if (positive) {
        tighten_lower(shifted);
      } else {
        tighten_upper(shifted);
      }
    }
    if (basic_state.upper.has_value()) {
      const DeltaRational shifted =
          beta_j + (basic_state.upper->value - beta_i).scaled(inverse_coeff);
      if (positive) {
        tighten_upper(shifted);
      } else {
        tighten_lower(shifted);
      }
    }
  }

  return interval;
}

LraTableau::ModelMutationResult LraTableau::diversify_shared_model(
    const std::vector<LraVarId>& shared,
    const std::vector<std::pair<LraVarId, LraVarId>>& forbidden_equalities) const {
  ModelMutationResult result;

  // The scratch model. Copying every variable's value is O(vars) once, which
  // buys the guarantee that nothing below can touch tableau state.
  std::vector<DeltaRational> beta;
  beta.reserve(vars_.size());
  for (const VarState& state : vars_) {
    beta.push_back(state.assignment);
  }
  const auto value_of = [&beta](LraVarId var) -> const DeltaRational& { return beta[var]; };
  const auto forbidden_violations = [&beta, &forbidden_equalities]() {
    return static_cast<std::size_t>(std::count_if(
        forbidden_equalities.begin(), forbidden_equalities.end(),
        [&beta](const auto& pair) { return beta.at(pair.first) == beta.at(pair.second); }));
  };

  result.forbidden_equalities_before = forbidden_violations();
  if (shared.empty()) {
    result.forbidden_equalities_after = result.forbidden_equalities_before;
    result.values = std::move(beta);
    return result;
  }

  auto grouping = group_by_model_value(shared, value_of);
  result.classes_before = grouping.total;
  if (grouping.classes.empty() && result.forbidden_equalities_before == 0) {
    // Nothing coincides, so there is nothing to separate and no reason to pay
    // for the sweep.
    result.classes_after = grouping.total;
    result.forbidden_equalities_after = 0;
    result.values = std::move(beta);
    return result;
  }

  // Which nonbasic variables can move a shared value at all: the shared ones
  // themselves, plus those appearing in a shared BASIC variable's row -- a
  // basic variable moves only through its row, and Section 5.2 will not pivot
  // to make it free. Anything else cannot change the partition, so scoring it
  // would be wasted work. Built in first-appearance order; an unordered_set
  // would make the sweep's outcome depend on hash order.
  std::vector<LraVarId> candidates;
  std::unordered_set<LraVarId> candidate_seen;
  const auto add_candidate = [&candidates, &candidate_seen](LraVarId var) {
    if (candidate_seen.insert(var).second) {
      candidates.push_back(var);
    }
  };
  for (const std::vector<LraVarId>& equal_class : grouping.classes) {
    for (const LraVarId var : equal_class) {
      if (!vars_.at(var).is_basic) {
        add_candidate(var);
        continue;
      }
      for (const auto& [term_var, coeff, slot] : rows_.at(var)) {
        if (!vars_.at(term_var).is_basic && !coeff.is_zero()) {
          add_candidate(term_var);
        }
      }
    }
  }

  // Applies update(x_j, v) to the scratch model exactly as update() would.
  const auto apply = [this, &beta](LraVarId nonbasic, const DeltaRational& value) {
    const DeltaRational shift = value - beta[nonbasic];
    beta[nonbasic] = value;
    for (const LraVarId basic : occurs_in_.at(nonbasic)) {
      if (!vars_.at(basic).is_basic) {
        continue;
      }
      for (const auto& [term_var, coeff, slot] : rows_.at(basic)) {
        if (term_var == nonbasic) {
          beta[basic] = beta[basic] + shift.scaled(coeff);
        }
      }
    }
  };

  for (const LraVarId candidate : candidates) {
    const FreedomInterval interval = freedom_interval_impl(candidate, &beta);
    const DeltaRational current = beta[candidate];

    // Where to try inside the interval. The interval is exact; this choice is
    // pure heuristic, which is why the count is a Tuning knob. Endpoints first
    // -- they are the values most likely to break a coincidence, since a
    // coincidence usually sits in the interior -- then successive midpoints
    // toward the current value, which stay legal when an endpoint is
    // unbounded on one side.
    std::vector<DeltaRational> tries;
    tries.reserve(tuning_.model_mutation_candidates);
    if (interval.lower.has_value()) {
      tries.push_back(*interval.lower);
    } else {
      // Unbounded below, so any step down is legal. Without this an entirely
      // unconstrained variable -- the one that is *easiest* to move -- would
      // offer no candidate at all and be skipped.
      tries.push_back(current - DeltaRational::from_rational(util::Rational(1)));
    }
    if (interval.upper.has_value()) {
      tries.push_back(*interval.upper);
    } else {
      tries.push_back(current + DeltaRational::from_rational(util::Rational(1)));
    }
    const std::size_t seeds = tries.size();
    for (std::size_t i = 0; i < seeds && tries.size() < tuning_.model_mutation_candidates; ++i) {
      tries.push_back((tries[i] + current).scaled(util::Rational(1) / util::Rational(2)));
    }
    if (tries.size() > tuning_.model_mutation_candidates) {
      tries.resize(tuning_.model_mutation_candidates);
    }

    for (const DeltaRational& value : tries) {
      if (value == current) {
        continue;  // a zero-width move cannot change the partition
      }
      const DeltaRational saved = current;
      std::vector<bool> forbidden_was_equal;
      forbidden_was_equal.reserve(forbidden_equalities.size());
      for (const auto& [lhs, rhs] : forbidden_equalities) {
        forbidden_was_equal.push_back(beta.at(lhs) == beta.at(rhs));
      }
      apply(candidate, value);
      auto mutated = group_by_model_value(shared, value_of);
      bool reintroduced_forbidden_equality = false;
      std::size_t newly_separated = 0;
      for (std::size_t index = 0; index < forbidden_equalities.size(); ++index) {
        const auto& [lhs, rhs] = forbidden_equalities[index];
        const bool now_equal = beta.at(lhs) == beta.at(rhs);
        if (!forbidden_was_equal[index] && now_equal) {
          reintroduced_forbidden_equality = true;
          break;
        }
        if (forbidden_was_equal[index] && !now_equal) {
          ++newly_separated;
        }
      }
      if (!reintroduced_forbidden_equality && mutated.total >= grouping.total &&
          (newly_separated > 0 || mutated.total > grouping.total)) {
        grouping = std::move(mutated);
        result.updates.emplace_back(candidate, value);
        break;  // this variable has paid off; move to the next one
      }
      apply(candidate, saved);  // no improvement, so put it back
    }
  }

  result.classes_after = grouping.total;
  result.classes = std::move(grouping.classes);
  result.forbidden_equalities_after = forbidden_violations();
  result.values = std::move(beta);
  assert(result.classes_after >= result.classes_before &&
         "a model mutator must not decrease |classes| -- the paper's stated "
         "obligation, and the reason each update is kept only when it "
         "strictly increases the count");
  assert(result.forbidden_equalities_after <= result.forbidden_equalities_before &&
         "a model mutator must not reintroduce a forbidden shared equality");
  return result;
}

std::optional<std::vector<sat::Lit>> LraTableau::check() {
  bool limit_reached = false;
  return check_impl(std::nullopt, limit_reached);
}

LraTableau::BoundedCheckStatus LraTableau::check_bounded(std::size_t max_steps) {
  bool limit_reached = false;
  const auto conflict = check_impl(max_steps, limit_reached);
  if (limit_reached) return BoundedCheckStatus::LimitReached;
  return conflict ? BoundedCheckStatus::Infeasible : BoundedCheckStatus::Feasible;
}

std::optional<std::vector<sat::Lit>> LraTableau::check_impl(std::optional<std::size_t> max_steps,
                                                            bool& limit_reached) {
  // Anti-cycling fallback for every non-Bland rule (see the PivotingRule class
  // comment): once more than this many pivots have happened within
  // this call, switch to Bland's-rule selection (both loops below) for
  // the remainder. A heuristic threshold, not a formally derived bound --
  // generous relative to the tableau size. Reset every call (cycling risk
  // is per Check/Pivot episode, not global across the tableau's lifetime).
  // Both coefficients are tunable (Tuning); termination needs the fallback
  // to happen after finitely many pivots, not after this particular count,
  // so any value is sound.
  const std::size_t bland_fallback_threshold =
      tuning_.bland_fallback_factor * vars_.size() + tuning_.bland_fallback_offset;
  std::size_t pivot_count = 0;
  bool fallback_counted = false;

  if (rule_ == PivotingRule::Soi) {
    // SOICHECK (Fig 5): note the nonbasic-in-error fixup (SOICHECK line 1)
    // is unconditionally already an invariant here -- assert_upper/
    // assert_lower always sync a nonbasic variable's assignment
    // immediately, so N∩E is always empty by construction; nothing to do.
    while (pivot_count <= bland_fallback_threshold) {
      if (max_steps && pivot_count >= *max_steps && !violated_vars_.empty()) {
        limit_reached = true;
        return std::nullopt;
      }
      const auto selection = soi_select();
      if (!selection.has_value()) {
        // Flex(f) empty (Section V/V-B): either satisfiable (no basic
        // variable remains violated) or a genuine multi-row conflict.
        // soi_direction(id)!=0 is exactly "id is basic and violated", so
        // violated_vars_ (see its own comment) already is this set.
        const std::vector<LraVarId> error_vars(violated_vars_.begin(), violated_vars_.end());
        if (error_vars.empty()) {
          return std::nullopt;
        }
        ++soi_multi_row_conflict_count_;
        std::vector<LraVarId> minimized = error_vars;
        if (tuning_.soi_minimization != SoiMinimization::None) {
          ++soi_minimize_call_count_;
          minimized = tuning_.soi_minimization == SoiMinimization::Deletion
                          ? delete_soi_conflict(error_vars)
                          : minimize_soi_conflict(error_vars);
          if (minimized.size() < error_vars.size()) {
            ++soi_minimize_shrunk_count_;
          }
        }
        return build_soi_conflict(minimized);
      }
      if (selection->entering == selection->leaving) {
        // j reaches its own bound directly -- UPDATE without a pivot
        // (PRIMAL's own footnote: k==j collapses UPDATEANDPIVOT to a
        // plain update).
        update(selection->entering, vars_.at(selection->entering).assignment + selection->delta);
      } else {
        // Capture the leaving row's coefficient for `entering` before
        // pivot() rewrites it, to compute the leaving variable's exact
        // target value directly -- reuses the existing Bland/Dantzig
        // convention below (pivot first using the old row shape, then
        // update the now-nonbasic leaving variable; its occurs_in_ entry
        // post-pivot correctly includes the new entering row).
        util::Rational coeff(0);
        for (const auto& [var, c, var_slot] : rows_.at(selection->leaving)) {
          if (var == selection->entering) {
            coeff = c;
            break;
          }
        }
        const DeltaRational target =
            vars_.at(selection->leaving).assignment + selection->delta.scaled(coeff);
        pivot(selection->leaving, selection->entering);
        update(selection->leaving, target);
      }
      ++pivot_count;
    }
    // Defensive fallback, belt-and-suspenders beyond the paper's own
    // tie-breaking-reduces-to-Bland termination argument (Section V-C):
    // switch to plain Bland selection for the remainder by falling
    // through to the loop below -- pivot_count already exceeds the
    // threshold, so that loop's existing `use_bland` check (`rule_ ==
    // PivotingRule::Bland || pivot_count > bland_fallback_threshold`)
    // already selects Bland unconditionally from here on, with no
    // changes needed to that loop itself. f_var_ is never violated (no
    // bounds), so its ordinary violated-variable scan stays correct too.
    ++bland_fallback_count_;
    // Already counted for this call; the loop below must not count it again.
    fallback_counted = true;
  }

  while (true) {
    if (max_steps && pivot_count >= *max_steps && !violated_vars_.empty()) {
      limit_reached = true;
      return std::nullopt;
    }
    // Counted for every rule that can fall back, not just Dantzig. It read
    // `rule_ == PivotingRule::Dantzig` until 2026-08-06, which meant the
    // counter stayed at 0 under Sparse -- the solver's *default* rule -- even
    // on calls that did switch to Bland selection. That made
    // :lra.bland-fallbacks useless for the one question it exists to answer,
    // namely whether the threshold ever binds. Bland itself is excluded
    // because it never "falls back": it is already the rule.
    if (rule_ != PivotingRule::Bland && !fallback_counted &&
        pivot_count > bland_fallback_threshold) {
      ++bland_fallback_count_;
      fallback_counted = true;
    }
    const bool use_bland = rule_ == PivotingRule::Bland || pivot_count > bland_fallback_threshold;

    LraVarId violated = vars_.size();
    bool too_low = false;
    DeltaRational best_violation;
    std::size_t best_row_size = 0;
    // Iterate violated_vars_ (see its own comment) instead of every
    // variable in the tableau: membership already guarantees `id` is
    // basic and currently out of exactly one bound, so there's no
    // is_basic/is_violated guard to re-check here -- just which bound.
    for (const LraVarId id : violated_vars_) {
      const VarState& state = vars_[id];
      bool candidate_too_low = false;
      DeltaRational violation;
      if (state.lower.has_value() && state.assignment < state.lower->value) {
        candidate_too_low = true;
        violation = state.lower->value - state.assignment;
      } else {
        candidate_too_low = false;
        violation = state.assignment - state.upper->value;
      }
      if (use_bland) {
        violated = id;
        too_low = candidate_too_low;
        break;
      }
      // PivotingRule::Sparse with its default-on leaving-side policy (see
      // set_sparse_leaving): repair a *short* row first, since the row being
      // pivoted out becomes the substituted-in row and its length is the
      // second Markowitz factor. Violation magnitude drops to a tie-break.
      // Unlike the entering-side rule this changes which infeasibility gets
      // repaired, which is why it is separately gated.
      // MathSAT's rule (Griggio thesis Section 2.5.3, page 53): the
      // least-violated basic variable first, tie-broken by the shorter row.
      // The intent is to minimise how much the tableau and beta have to move
      // to become feasible, which is the opposite intuition to Dantzig's
      // "repair the worst infeasibility first".
      if (least_violated_leaving_ && rule_ == PivotingRule::Sparse) {
        const std::size_t row_size = rows_[id].size();
        if (violated == vars_.size() || violation < best_violation ||
            (violation == best_violation && row_size < best_row_size)) {
          violated = id;
          too_low = candidate_too_low;
          best_violation = violation;
          best_row_size = row_size;
        }
        continue;
      }
      if (sparse_leaving_ && rule_ == PivotingRule::Sparse) {
        const std::size_t row_size = rows_[id].size();
        if (violated == vars_.size() || row_size < best_row_size ||
            (row_size == best_row_size && violation > best_violation)) {
          violated = id;
          too_low = candidate_too_low;
          best_violation = violation;
          best_row_size = row_size;
        }
        continue;
      }
      // Dantzig-style: pick the most-violated basic variable (largest
      // magnitude out-of-bounds), tie-breaking by smallest index.
      if (violated == vars_.size() || violation > best_violation) {
        violated = id;
        too_low = candidate_too_low;
        best_violation = violation;
      }
    }
    if (violated == vars_.size()) {
      return std::nullopt;
    }

    const LraVarId entering = select_entering_variable(violated, too_low, use_bland);
    if (entering == vars_.size()) {
      return build_conflict(violated, too_low);
    }

    const DeltaRational target =
        too_low ? vars_.at(violated).lower->value : vars_.at(violated).upper->value;
    pivot(violated, entering);
    update(violated, target);
    ++pivot_count;
  }
}

void LraTableau::pop(std::size_t checkpoint) {
  if (trail_.size() <= checkpoint) {
    return;
  }
  // occurs_in_ only depends on rows_/is_basic (see rebuild_occurs_in_index()'s
  // own comment) -- never on occurs_in_ itself -- so it's safe to defer one
  // full rebuild to the end of this loop instead of doing it inside undo()
  // for every individual PivotTrailEntry/FRowTrailEntry undone. Profiling
  // p5-zenonumeric_s5.smt2 (gprof) found rebuild_occurs_in_index() at ~9% of
  // runtime with a call count matching the *pivot* count almost exactly --
  // undo() was rebuilding it once per pivot undone, not once per pop() call,
  // so a pop() that unwinds N pivots at once did N redundant full rebuilds.
  bool occurs_in_index_stale = false;
  while (trail_.size() > checkpoint) {
    const TrailEntry& entry = trail_.back();
    if (std::holds_alternative<PivotTrailEntry>(entry) ||
        std::holds_alternative<FRowTrailEntry>(entry)) {
      occurs_in_index_stale = true;
    }
    undo(entry);
    trail_.pop_back();
  }
  if (occurs_in_index_stale) {
    rebuild_occurs_in_index();
  }
  // violated_vars_ isn't patched incrementally per undo case (bound/
  // assignment/pivot/f-row trail entries interact in too many
  // combinations to do that safely) -- see rebuild_violated_vars()'s own
  // comment for why a full rebuild here, rather than per-entry patching,
  // is the right tradeoff.
  rebuild_violated_vars();
}

void LraTableau::undo(const TrailEntry& entry) {
  if (const auto* bound = std::get_if<BoundTrailEntry>(&entry)) {
    VarState& state = vars_.at(bound->var);
    if (bound->is_upper) {
      state.upper = bound->previous;
    } else {
      state.lower = bound->previous;
    }
    return;
  }
  if (const auto* assignment = std::get_if<AssignmentTrailEntry>(&entry)) {
    vars_.at(assignment->var).assignment = assignment->previous;
    return;
  }
  if (const auto* pivot_entry = std::get_if<PivotTrailEntry>(&entry)) {
    // No explicit clear of entering_var's slot: it's about to become
    // nonbasic again, and pop()'s own end-of-loop rebuild_occurs_in_index()
    // call is is_basic-gated, not content-gated (see rows_'s own comment),
    // so the stale leftover here is harmless. That rebuild is deferred to
    // pop() (once per pop() call, not once per entry undone here) -- see
    // pop()'s own comment for why.
    rows_[pivot_entry->basic_var] = pivot_entry->old_basic_row;
    vars_.at(pivot_entry->basic_var).is_basic = true;
    vars_.at(pivot_entry->entering_var).is_basic = false;
    for (const auto& [other_basic, old_row] : pivot_entry->old_other_rows) {
      rows_[other_basic] = old_row;
    }
    return;
  }
  // PivotingRule::Soi only: undo one set_soi_direction() call's effect,
  // restoring both f_var_'s row and the affected variable's tracked
  // direction atomically (see FRowTrailEntry's comment). Same deferred-
  // rebuild reasoning as the PivotTrailEntry branch above.
  const auto& f_row_entry = std::get<FRowTrailEntry>(entry);
  rows_[*f_var_] = f_row_entry.previous_f_row;
  soi_directions_[f_row_entry.var] = f_row_entry.previous_direction;
}

bool LraTableau::occurs_in_index_matches_full_rebuild() const {
  std::vector<std::vector<LraVarId>> expected(occurs_in_.size());
  for (LraVarId basic_var = 0; basic_var < vars_.size(); ++basic_var) {
    if (!vars_[basic_var].is_basic) {
      continue;
    }
    for (const auto& [var, coefficient, var_slot] : rows_[basic_var]) {
      expected[var].push_back(basic_var);
    }
  }
  for (std::size_t var = 0; var < expected.size(); ++var) {
    std::vector<LraVarId> actual = occurs_in_[var];
    std::sort(actual.begin(), actual.end());
    std::sort(expected[var].begin(), expected[var].end());
    if (actual != expected[var]) {
      return false;
    }
  }
  // Every row entry's back-index must round-trip: the occurrence slot it
  // names has to be the very row it belongs to. This is what makes the
  // O(log row) removal in pivot() falsifiable rather than merely plausible
  // -- without it a wrong slot silently unlinks the wrong occurrence, and
  // the set comparison above would still pass on the very next rebuild.
  for (LraVarId basic_var = 0; basic_var < vars_.size(); ++basic_var) {
    if (!vars_[basic_var].is_basic) {
      continue;
    }
    for (const RowEntry& entry : rows_[basic_var]) {
      const std::vector<LraVarId>& column = occurs_in_[entry.var];
      if (entry.occ_slot >= column.size() || column[entry.occ_slot] != basic_var) {
        return false;
      }
    }
  }
  return true;
}

void LraTableau::unlink_occurrence(LraVarId var, std::uint32_t slot) {
  std::vector<LraVarId>& column = occurs_in_[var];
  const LraVarId moved = column.back();
  column[slot] = moved;
  column.pop_back();
  if (slot >= column.size()) {
    // The removed entry was the last one: nothing moved, nothing to repair.
    return;
  }
  // `moved` now sits at `slot`, so its own row entry for `var` must say so.
  // rows_[moved] is ordered by var id (the Row invariant), so this is a
  // binary search over one row rather than a scan of the column.
  Row& row = rows_[moved];
  const auto it = std::lower_bound(row.begin(), row.end(), var,
                                   [](const RowEntry& e, LraVarId v) { return e.var < v; });
  if (it != row.end() && it->var == var) {
    it->occ_slot = slot;
  }
}

void LraTableau::rebuild_occurs_in_index() {
  // rows_ has a slot for every variable, not just currently-basic ones
  // (see rows_'s own comment) -- a nonbasic slot's leftover content must
  // not be walked here, hence the explicit is_basic gate rather than
  // iterating rows_ directly.
  for (std::vector<LraVarId>& entry : occurs_in_) {
    entry.clear();
  }
  for (LraVarId basic_var = 0; basic_var < vars_.size(); ++basic_var) {
    if (!vars_[basic_var].is_basic) {
      continue;
    }
    for (auto& entry : rows_[basic_var]) {
      entry.occ_slot = static_cast<std::uint32_t>(occurs_in_[entry.var].size());
      occurs_in_[entry.var].push_back(basic_var);
    }
  }
}

void LraTableau::update_violation_status(LraVarId var) {
  const VarState& state = vars_.at(var);
  const bool violated =
      state.is_basic && ((state.lower.has_value() && state.assignment < state.lower->value) ||
                         (state.upper.has_value() && state.assignment > state.upper->value));
  if (violated) {
    violated_vars_.insert(var);
  } else {
    violated_vars_.erase(var);
  }
}

void LraTableau::rebuild_violated_vars() {
  violated_vars_.clear();
  for (LraVarId var = 0; var < vars_.size(); ++var) {
    update_violation_status(var);
  }
}

}  // namespace primo::theory::arithmetic
