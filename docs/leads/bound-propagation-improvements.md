# Bound propagation improvements: implementation checklist

Baseline: `ff303826`. This page records the four-stage plan following the
[selected diagnosis](../evidence/bound-behavior.md). Qualification is pending;
existing evidence does not qualify these changes. Notification and rounding repairs now have
{boundrepairevidence}`initial correctness evidence <benchmarks/bound-repairs-20261114/stage1-qualification.json>`;
performance qualification remains pending. The later-placement experiment is rejected with its source changes reverted;
scan-local endpoint reuse is now drafted and awaiting qualification.

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


## Scan-local reuse (draft)

The private
{rowcachetrialsource}`RowBoundCache <src/smt/solver_engines.cpp#L7082>`
owns a local comparison count, a computed/not-computed flag, an optional rational-plus-
infinitesimal endpoint, an optional integer-rounded endpoint and an optional
reason vector. A two-element `RowBoundCaches` array belongs to one row or child
scan, indexed by bound direction. A computed missing endpoint is cached too;
it must not be confused with an unattempted calculation.

Both arithmetic execution paths read the tableau and only enqueue pending
literals during a scan. They neither assert those literals synchronously nor
pivot or register atoms before returning. This invariant makes the local
cache valid without generation numbers. No cache survives a notification,
backtrack, feasibility repair, private completion or registry growth.

The
{rowcachetrialsource}`numeric and explanation helpers <src/smt/solver_engines.cpp#L7013>`
no longer allocate reason vectors during numeric calculation. After an endpoint first
entails an atom, separate explanation helpers collect the same premise literals
in the original order from the unchanged tableau. Each queued consequence gets
an owning copy; the cache's reason vector is retained for other candidate atoms.
A missing premise during explanation collection diagnoses an invariant failure.

For a row of width `r` with `a` candidate atoms, forward endpoint arithmetic
costs `O(r+a)` per scan instead of `O(r*a)`. Reverse arithmetic remains quadratic
in row width when all children are inspected, plus candidate comparisons;
this is not the optimized exclusion-sum worklist used by preprocessing. Lazy
explanations add one linear witness-collection pass per entailed direction.
Each pending consequence still copies its explaining vector in linear time
in the number of premises; total witness-copying cost can remain `O(r*a)`.
These complexity bounds count arithmetic operations, not arbitrary-precision
rational bit costs. This is an engineering optimization of existing CAV2006-style interval
refinement, not a new paper algorithm or stronger closure.

Work counters are published once per row/child group to avoid a statistics
hash lookup per candidate. They distinguish attempted endpoint calculations (including missing
or capped directions), candidate-polarity comparisons and constructed explanation
vectors. The
{rowcachetrialsource}`regression with many thresholds <tests/unit/qf_lra_solver_tests.cpp#L3586>`
for one row tests reuse, including an
actual UF interaction. Existing conflict, sign, scope and allocation-growth
tests and fresh four-logic gates remain required before acceptance.

## Final comparison protocol (pending)

The {boundrepairpairsource}`configuration helper <scripts/qualification/bound_repairs/configure.py>`
creates three private
configurations from the frozen 697-input selection and its four existing
profiles. Each profile has an adjacent baseline/candidate pair. The timing
{boundrepairpairsource}`runner's optional paired_groups <scripts/qualification/lia_hybrid/run.py#L40>` rotates the four pairs by instance index plus
repetition and reverses their internal order on alternating sums. Each native
measurement starts a fresh solver process; the per-allocation provenance records
the actual order, executable hashes, arguments, input identity and host. The
runner verifies executable and input hashes; Git labels are supplied metadata
and require the separate build/preflight and source-equivalence evidence.
Configurations without `paired_groups` retain the runner's earlier rotation.

The {boundrepairpairsource}`submission controller <scripts/qualification/bound_propagation/submit.py>`
accepts `--job-time` for the SLURM allocation duration.
The final comparison requests 20 minutes for eight timing measurements per
allocation and 45 minutes for their separate model replays. This does not change
the native timing deadline of 120 seconds or the 8 GiB address-space limit. Three timing
arrays run sequentially, with verified whole-array dependencies; model arrays
are submitted with `--after` naming the last timing-array ID, so they start only
after all timing arrays finish. Replay allows 180 seconds for the native solver
and 60 seconds for Dolmen. Every successful native SAT timing row
requires its own exact-option Dolmen replay. These scripts and scheduling tests
prepare the comparison; they are not completed performance evidence.
