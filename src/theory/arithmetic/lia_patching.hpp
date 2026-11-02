#pragma once

#include <cstddef>
#include <vector>

#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

// Diagnostic work counters for one patching call. They stay theory-local and
// contain no util::Statistics dependency; solver adapters decide how to expose
// them publicly.
struct LiaPatchingStats {
  // Valid when patching is enabled, including an empty integer set. Reuses the
  // scoring index's remaining count instead of rescanning the final tableau.
  bool fully_integral = false;
  std::size_t occurrence_entries = 0;
  std::size_t candidates_scored = 0;
  std::size_t affected_rows_inspected = 0;
  std::size_t full_scan_equivalent = 0;
  // Accepted moves that left an integer-sorted nonbasic variable at a
  // non-integer value. Such a move can still lower the fractional count by
  // fixing several basic rows, but it breaks the invariant that integer
  // nonbasic variables hold integer values (docs/implementation/lia.md).
  std::size_t fractional_nonbasic_moves = 0;
};

// QF_LIA patching (stage 5, docs/qf-lia-branch-and-bound.md): Bjorner &
// Nachmanson, "Arithmetic Solving in Z3", CAV 2024, Sect. 4.1 -- the FIRST
// layer of that paper's integer waterfall, and the one Primo's stages 1-4
// skipped. Where GCD-consistency, Gomory cuts and branch-and-bound all work to
// *refute* a candidate, this works to *find* a model: it nudges nonbasic
// values so that Int-sorted variables land on integers, turning an LRA model
// into an integer one without any search.
//
// **Patching decides nothing.** It only improves the assignment in place.
// Every Sat/Unsat conclusion still flows through the stages that follow it, so
// a patch that fails to help costs a little time and nothing else. When it
// helps completely, branch_and_bound's own find_fractional_variable finds
// nothing left to branch on and returns Sat at zero nodes -- through the
// existing code path, not a new one.
//
// Two shapes are handled, and the first is one the paper does not need to
// describe. The paper assumes every nonbasic sits at a bound, so only basic
// variables can be fractional. Primo violates that twice over: LraTableau
// deliberately lets nonbasic variables sit strictly inside their bounds (see
// its own class comment), and scale-canonicalization stores literally
// fractional bounds on Int-sorted variables (`(<= (* 3 x) 5)` becomes a bound
// of 5/3). So:
//
//   * a *nonbasic* Int variable at a fractional value is moved straight to the
//     nearest integer inside its freedom interval -- no row reasoning at all;
//   * a *basic* Int variable `xb` is fixed by moving some nonbasic `y` from its
//     row: with coefficient `a`, moving `y` by `delta` moves `xb` by
//     `a * delta`, so `delta = (n - beta(xb)) / a` for the integer `n` nearest
//     `beta(xb)`. The candidate `y` minimizing `|delta|` wins, which is the
//     paper's own criterion.
//
// Feasibility is preserved by construction, with no check() call: a variable's
// freedom interval is exactly the range over which it may move with *every*
// basic variable staying inside its bounds, and no move outside that interval
// is ever taken. That property is what makes patching cheap enough to run
// before the refutation stages rather than instead of them.
//
// Mutates `tableau` via LraTableau::set_nonbasic_value, which is NOT trailed
// outside PivotingRule::Soi -- see that function's comment. Callers must own a
// tableau whose assignment need not survive a pop(); solve_qf_lia_impl's
// per-round `lia_tableau` copy is one.
//
// `patch_limit` caps how many individual moves are attempted (0 disables
// patching entirely, which is the A/B control). Returns the number of Int
// variables that went from non-integer to integer.
std::size_t patch_integer_variables(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                                    std::size_t patch_limit, LiaPatchingStats* stats = nullptr);

}  // namespace primo::theory::arithmetic
