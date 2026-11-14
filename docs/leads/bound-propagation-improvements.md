# Bound propagation improvements: implementation checklist

Baseline: `ff303826`. This page records the four-stage plan following the
[selected diagnosis](../evidence/bound-behavior.md). Qualification is pending;
existing evidence does not qualify these changes. Notification and rounding repairs now have
{boundrepairevidence}`initial correctness evidence <benchmarks/bound-repairs-20261114/stage1-qualification.json>`;
performance qualification remains pending. The later-placement experiment is rejected with its source changes reverted;
scan reuse remains planned.

## Basis and obligations

Dutertre and de Moura, *A Fast Linear-Arithmetic Solver for DPLL(T)*,
[CAV 2006, §4.5](https://leodemoura.github.io/files/cav06.pdf), describes
explained refinement of tableau bounds against existing atoms. Section 5
supplies the ordered infinitesimal representation. The section was read before
implementation. Integer rounding is the elementary implication that an integer
bounded above/below by a rational endpoint is bounded by its floor/ceiling;
Primo's existing delta-aware integer-value helpers implement that extension.
This is ordinary interval refinement, not exhaustive closure or a new complete
integer procedure.

- {boundrepairsource}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7116>`
  and {boundrepairsource}`ComponentLraExecution <src/smt/solver_engines.cpp#L8897>`
  own their tableau and pending consequences. Each `PendingPropagation` owns
  its `reasons` vector. The
  {boundrepairsource}`preparation policy <src/smt/solver_engines.cpp#L3488>`
  establishes integer-valued targets and chooses `IntegerBoundRounding::All`.
- Bound notifications on a basic variable must scan its own row in reverse;
  nonbasic notifications inspect the occurrence index. This notification
  schedule is a Primo adaptation of explained refinement. Reverse scans must not
  depend on a registered atom for the row's basic variable.
- Keep forward and reverse size/fanout limits. This milestone does not promise
  inference completeness across basis changes or compute numeric closure.
- Before comparing an inferred endpoint with an atom, use delta floor for an
  upper endpoint and delta ceiling for a lower endpoint only under `All`
  integer rounding, via the shared
  {boundrepairsource}`endpoint comparison <src/smt/solver_engines.cpp#L7091>`.
  Keep premises unchanged: integrality is a background fact.
- Pending consequences own reasons; no borrowed term/row/atom reference may
  cross a mutation of its owner. No ordinary bound introduces a tableau row.
- Later, evaluate moving the single preprocessing worklist after existing
  value/equality simplification, before finite-domain processing and final
  arithmetic normalization. Revert the stage if qualification rejects it.
- Finally, reuse upper/lower calculations within a single scan only. No
  persistent cache, backtracking invalidation scheme or numeric-no-op skip.
- Add mechanism counters and focused assertions; compare exact options and
  identities on SLURM. Stop performance work on a correctness failure.

## Verification and delivery

First qualify notification/rounding repairs, then independently evaluate
preprocessing placement and scan-local reuse. Each accepted stage needs source,
tests, synchronized implementation documentation and a verified commit.
All solver batches/build qualification run in SLURM through the preparation
helper. Required gates include check, ASan, lint, static review, four-logic
reference/model tests and seed mutations. Final evaluation uses the frozen
697-case selection, four profiles, three rotated pairs, 120 seconds and the
existing memory limits, with separate complete Dolmen SAT coverage.
Defaults, private branch budgets and UF fallback policy remain unchanged.

For example, `s = 2u + v`, `s <= 5` and `v >= 0` imply `u <= 5/2`,
hence `u <= 2` for integer `u`. With bidirectional row propagation enabled,
asserting the basic bound last must also trigger its reverse scan, subject to
the configured limits. The
{boundrepairsource}`focused regression <tests/unit/qf_lra_solver_tests.cpp#L3537>`
checks assertion ordering and both integer bound directions.
