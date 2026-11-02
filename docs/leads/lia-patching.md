# Configurable and better-guided integer patching

**Status: Open lead. Owner: unassigned.**

Replace the unexplained fixed search width with an explicit parameter, then
investigate candidate selection and progress measures from integer programming.
The [primary-source survey](../papers/lia-primal-heuristics.md) covers rounding
and shifting, the feasibility pump, and RENS. These are candidate experiments;
no new option or heuristic is implemented by this documentation change.

## Baseline and parameter contract

{source}`integer_targets <src/theory/arithmetic/lia_patching.cpp#L17-L43>`
currently sets `kTargetWindow = 4`. Let $m$ be the
{source}`delta-aware floor <src/theory/arithmetic/lia_integer_values.hpp#L20-L38>`
of the current tableau value; for example, $\lfloor n-\delta\rfloor=n-1$
for integer $n$. The routine generates
$m,m+1,m-1,m+2,m-2,m+3,m-3,m+4$, then stably orders them by distance from
the rational part. This window was not benchmark-tuned. The existing
`--lia-patch-limit` limits **accepted moves**, not target distance or scoring
work.

Propose `--lia-patch-window=N`, with positive integer $N$ and default $4$,
generating $m-k,m+k+1$ for $0\le k<N$. Retain the current order and ties at
the default. Keep disabling patching under `--lia-patch-limit=0`; reject
zero for the new window to avoid two disabling conventions. Specify a checked
range and an allocation/work bound before accepting arbitrary user values.
Thread the option through both QF_LIA and QF_UFLIA, and report its effective
value in reproducible experiments. This is a proposed interface, pending
implementation and qualification.

## Comparisons worth making

Start with configurable fixed windows as a controlled baseline, then compare:

1. **Reachable candidates.** Map the moved variable's freedom interval to the
   target row's reachable values, and generate admissible integers directly.
   Account for coefficient sign and exact strict endpoints.
2. **Adaptive widening and arithmetic structure.** Widen only after the cheap
   search fails, under a candidate/row-visit budget. Investigate steps that
   preserve integrality in several affected rows: in $y=x/2,z=x/3$, multiples
   of $6$ satisfy both. A single-row congruence generator now exists behind
   `--lia-patch-integral-moves` (see {ref}`patching <lia-patching>`);
   it handles one target row per candidate, so steps that serve several rows
   at once remain open. It is an inference for Primo, not an implemented
   algorithm from the surveyed papers.
3. **A more informative progress score.** Compare the current count of
   fractional variables with fractionality-guided feasible moves, motivated
   by ZI Round in Achterberg–Berthold–Hendel §2. A smaller rational distance
   is not a finite termination argument. Specify work limits, cycles and
   handling of a pure infinitesimal before permitting count-preserving moves.
4. **Joint moves.** If single-variable progress stalls, compare a small
   RENS-style neighborhood with continued unrestricted branching. Limit its
   nodes and discard failed neighborhoods; their extra fixings do not justify
   an UNSAT answer. Nearest joint roundings still miss some distant solutions.

Temporary-infeasibility repair and a feasibility pump are larger alternatives.
The pump requires minimizing an LP distance objective; the current feasibility
checker is not that operation. Keep these behind evidence that the cheaper
experiments leave a material, reachable gap.

## Representation and acceptance criteria

Use the private integer-completion `LraTableau` and explicit `integer_vars`.
The current `PatchingScoringIndex` is valid only while row incidence stays
unchanged. Any pivoting variant must rebuild or maintain it. A returned
candidate must satisfy every tableau bound and integer obligation and pass
the caller's ordinary model checks. Heuristic failure continues to GCD,
cuts and branching; it never certifies infeasibility.

Preserve the {source}`existing patching regressions
<tests/unit/lia_patching_tests.cpp>` and add distant-target, plateau,
multi-variable and failed-neighborhood cases derived from the selected rules.
Record window, move/work budgets, seeds and fallback behavior. Use existing
candidate/affected-row counters and add rejection or budget counters only
where they distinguish the variants.

Evaluate matched configurations on representative QF_LIA and QF_UFLIA families
including SAT, UNSAT, `unknown` and timeout cases. Compare total
time, solved counts, integer model hits and downstream branch nodes; reducing
the fractional count alone is not a win. Fresh fuzzing and independent model
validation are correctness gates. Keep the default window at $4$ until a
different policy has both correctness evidence and a measured overall benefit.

Contributors may use any suitable machine. In the maintainer workspace, these
batches run on SLURM; see the {ref}`execution policy <run-the-applicable-local-checks>`.
