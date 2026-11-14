# Why bound propagation has a small aggregate effect

## What the diagnosis establishes

The [repeated propagation comparison](bound-propagation.md) hides substantial
changes in individual search paths. Targeted execution and source review now
confirm two missed inference opportunities, a redundant preprocessing bound
that triggers an expensive UF search path, and a genuine reduction in LP work
on one LIA input. These findings explain specific behavior; they do not turn
the small aggregate PAR2 difference into a general speedup claim.

Production solver code and defaults are unchanged. This investigation records
reproducers and diagnoses; repairs still need implementation and fresh
correctness and performance qualification. Both preprocessing bounds and row
propagation remain opt-in.

## What was executed

The native comparisons use the previous study's frozen Release binary, SHA-256
`1b43e6bdf394fa130483950c5d2a240f8b1667d25f4478e7ac6c244f74ba9cf5`.
All 201 production source files match baseline `5c8ced98`. A fresh SLURM build
also builds the unchanged bound-export probe. A separate diagnostic build
adds logging only; its results are used solely to inspect asserted bounds.

The selected corpus inputs were LIA slot 4, UFLIA slots 438 and 213, and LRA
slots 557 and 569 from the previous selection. These are deliberately chosen
successes and regressions, not a representative sample. The exact names,
hashes and resource limits are in the
{boundbehaviorevidence}`protocol <benchmarks/bound-behavior-20261114/protocol.json>`;
the {boundbehaviorevidence}`observations <benchmarks/bound-behavior-20261114/observations.json>`
record the exact solver option vectors.

The accepted native executions comprise 36 small-problem runs and 18 corpus
ablations. All 54 return SAT, and all 54 have separate, exact-input, full-option
model replays passing direct Dolmen validation. Z3 returns SAT on all 12 small
inputs and all nine modified corpus inputs. It also refutes the lowered input
conjoined with the negation of each complete emitted-bound batch for slots
4, 438 and 213. The corresponding proof checks for LRA 557 and 569 time out
at 30 seconds; those are incomplete checks. No content ablations use those
two unchecked batches.

All solver execution, builds, profiling and validation ran in SLURM. Native
runs have a 120-second wall limit and an 8 GiB address-space limit; validation
is separate from those times. The jobs overlap other development work on the
cluster, so their wall times are diagnostic observations. Deterministic work
counts, exact-content controls, and the earlier three repetitions provide
stronger evidence than their timing ratios alone. Builds were jobs 392076
and 392149; inspection, profiling and ablations were arrays 392098, 392099
and 392143; the bounded trace was job 392150.

## Representation and inference coverage

The preprocessing worklist owns term-indexed intervals in
{boundbehaviorsource}`Analysis <src/smt/preprocess_bound_tightening.cpp#L176>`.
It rounds integer endpoints before updating them and emits unconditional
formula bounds. Its interval snapshot precedes later constant substitution
and Boolean value propagation in the
{boundbehaviorsource}`preprocessing strategy <src/smt/preprocessing_strategy.cpp#L610>`.
Consequently, its “newly finite” count describes that early analysis, not
novel information compared with the arithmetic state eventually given to SAT.

During search, the owning arithmetic execution holds an `LraTableau` with
numeric variable IDs, asserted endpoints and their explaining SAT literals.
Forward row propagation derives an interval for a row's basic variable from
its children; reverse propagation rearranges that equality to bound a child.
The row scanners use the
{boundbehaviorsource}`interval helpers <src/smt/solver_engines.cpp#L7014>`
to test registered atoms and enqueue explained Boolean consequences. They
do not install every computed numeric endpoint into the tableau or run an
unrestricted interval-closure worklist. A useful endpoint without a matching
registered atom can therefore have no direct propagation effect.

### Basic-variable bound notifications miss the reverse scan

Consider the arithmetic-only formula, with either real or integer variables:

```text
u + v >= 5
u <= 2
(v <= 2) or (t >= 20)
```

The first two facts imply `v >= 3`, which falsifies `v <= 2` and forces the
other disjunct. With reverse propagation enabled, asserting the row bound
first and the child bound second delivers one reverse consequence and uses
zero SAT decisions. Reversing those two assertion orders delivers zero row
consequences and uses two decisions. The same observation holds for the
small inputs declared as QF_LRA, QF_UFLRA, QF_LIA and QF_UFLIA. Their UF logic
variants contain arithmetic only; the Wisa corpus case below supplies actual
UF interaction.

The scans in
{boundbehaviorsource}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7879>`
and {boundbehaviorsource}`ComponentLraExecution <src/smt/solver_engines.cpp#L9518>`
start with
`tableau_.occurs_in(changed_var)` and return when that list is empty. The
{boundbehaviorsource}`tableau occurrence index <src/theory/arithmetic/lra_tableau.hpp#L748>`
lists rows containing the variable on their right-hand side. A basic variable
has no such occurrences: its own bound update never triggers a reverse scan
of its defining row. Deferred feasibility repair can pivot the basis, but
its completion path does not run a new row scan. The existing positive unit
test exercises the row-bound-first order and previously had a stale comment
attributing its shape to removed single-variable slack restrictions; that
comment is corrected alongside this evidence.

This is an order-sensitive inference gap, not an incorrect SAT result. A
repair should schedule the basic variable's own reverse row and account for
basis changes, while preserving caps and explanation lifetimes.

### Derived integer endpoints are compared before rounding

For integers, use:

```text
2*u + v <= 5
v >= 0
(u <= 2) or (t >= 20)
```

The row gives `u <= 5/2`, hence the integer consequence `u <= 2`. Runtime
`compute_implied_child_bound` returns the rational endpoint `5/2`, and the
entailment test compares it directly against the already rounded atom
threshold 2. That comparison fails. The probe delivers zero row consequences
and uses two decisions. Changing only the target to `u <= 3` delivers one
reverse consequence and uses one decision. Both integer logic declarations
show this behavior; the preprocessing pass can separately derive the rounded
endpoint.

A repair must round the inferred endpoint for a target known to be integer
valued, including its infinitesimal part. Applying integer rounding to arbitrary
rational slack variables would be unsound. No such repair is included here.

## One redundant bound causes the UF regression

On UFLIA `mathsat/Wisa/xs-20-19-2-2-2-5.smt2`, preprocessing emits six bounds:

```text
arg0 >= -19       arg0 <= -19
fmt1 >= 2        fmt1 <= 18
arg1 >= 0        arg1 <= 15
```

They follow from the original constants and inequalities. In particular,
`fmt0 = 0`, `fmt_length = 20` and
`fmt1 < fmt0 + fmt_length - 1` already imply integer `fmt1 <= 18`.
The diagnostic trace confirms the actual arithmetic state, beyond this
algebraic argument:

```text
original: relation=lt rhs=19 ... asserted=18 ... before=infinite after=18
added:    relation=le rhs=18 ... asserted=18 ... before=18 after=18
```

The added assertion does not strengthen the numeric endpoint. It changes the
Boolean assertion stream and subsequent search. The bounded trace of each
profile already holds upper bound 18 at SAT decision level zero. Here “level zero”
means before any SAT decisions, not a mathematical root of an equation.
A non-strict `fmt1 <= 18` atom appears later in the trace without
preprocessing and earlier with preprocessing. The experiment does not isolate whether
atom ordering, explanation selection or later branching is the decisive downstream
cause.

Private nodes count speculative integer branch-and-bound nodes in integer
completion's private working tableau, distinct from SAT decisions. The normal
private cap is 100,000 per completion. In hybrid mode the private budget is
`min(cap, factor * N)`, where `N` counts distinct original mapped integer
terms, including purified UF terms. Factor zero requests a SAT split after
the preceding completion stages; factor one allows up to `N` private nodes
before that handoff. See the {ref}`hybrid policy <lia-hybrid-branching>`
for ownership and stage ordering. Private mode instead reports a resource
limit on exhaustion, which can trigger the UF fallback described below.

| Selected configuration | Seconds | Private nodes | Complete fallback | Materialized UF pairs |
|---|---:|---:|---:|---:|
| Row propagation | 0.358 | 0 | 0 | 87 |
| Plus preprocessing | 31.534 | 100,063 | 1 | 4,656 |
| Append exactly the six bounds instead | 31.577 | 100,063 | 1 | 4,656 |
| Omit only `fmt1 <= 18` from appended bounds | 0.401 | 0 | 0 | 91 |
| Omit only `arg1 <= 15` from appended bounds | 4.942 | 0 | 0 | 222 |
| Preprocessing, private cap 1 | 10.106 | 1 | 1 | 4,656 |
| Preprocessing, hybrid factor 0 | 8.395 | 0 | 0 | 254 |
| Preprocessing, hybrid factor 1 | 7.660 | 633 | 0 | 222 |

Preprocessing itself takes about 100 microseconds. The slow native run spends
21.33 seconds in private integer completion. Its resource-limit result then
invokes the
{boundbehaviorsource}`complete-arrangement fallback <src/smt/component_solver.cpp#L347>`:
the model-guided theory combination restarts with all shared-term pairs
available. Its
{boundbehaviorsource}`resource-limit case <src/smt/component_solver.cpp#L607>`
selects this restart. This adds another 3.91 seconds in the recorded fallback timer.
SAT decisions rise from 14,415 to 1,866,502, and the final arithmetic atom
registry grows from 721 to 9,783. That last counter is the current registry
size, not an accumulated insertion count.

Lowering the private cap to 100 or 1,000 also leaves the same complete
fallback and final SAT decision count. Hybrid branching avoids the complete
fallback here but remains much slower than the row-only run. Neither is a
qualified remedy. Representing fixed `arg0 = -19` as one equality instead of
two bounds leaves the bad work counts unchanged, rejecting that initial
representation hypothesis. The full six-way omission controls are in the
{boundbehaviorevidence}`measurement table <benchmarks/bound-behavior-20261114/measurements.csv>`.

## The LIA improvement is reduced LP work

On QF_LIA `20231117-c_inference/30_30_18_12_sat.smt2`, the six emitted bounds
are `eta_1`, `eta_17`, `eta_21`, `eta_11`, `eta_13` and `eta_27` all at least 1.

| Configuration | Seconds | LP pivots | SAT decisions | Private nodes |
|---|---:|---:|---:|---:|
| Row propagation | 81.441 | 282,750 | 49,904 | 0 |
| Plus preprocessing | 6.816 | 20,789 | 10,408 | 0 |
| Append exactly the six bounds instead | 6.828 | 20,789 | 10,408 | 0 |

Identical work counts for explicit bounds and preprocessing show that the
bound content changes this search path. There is no private integer branching
to accelerate in this example. The early analysis reports zero newly finite
endpoints, illustrating why that counter alone does not predict usefulness.

## Profiling explains different costs

Callgrind instruction-event profiles use a separate 60-second collection cap,
with symbol downloads disabled to avoid the initial profiler startup stall.
The UF row-only profile completes; the UF combined and LIA row-only profiles
are partial prefixes. In the UF combined prefix, forward interval calculation
accounts for 22.17% of inclusive instruction events and reverse interval
calculation 34.35%. Those are disjoint helper call trees, totaling 56.52%.
The prefix has not reached the expensive private search seen in the complete
native run. It does not estimate that stage's CPU share.

Code inspection explains repeated work: a scanner recomputes a row or child
interval, including allocation of its reason vector, for each candidate atom
and potentially both polarities. It also scans after assertions that leave
the numeric endpoint unchanged, as the trace demonstrates. In contrast, the
LIA row-only prefix spends 90.12% of inclusive instruction events in tableau
feasibility checking, including 70.53% in pivoting. Nested inclusive shares
must not be added. These instruction shares are not full-run wall-time
percentages.

The immediate implementation leads are therefore to cover basic-variable
bound events, round provably integer inferred endpoints, and avoid re-emitting
bounds redundant after existing normalization. Reusing interval calculations
within a scan is another performance lead. Skipping unchanged bounds requires
care around newly registered atoms and backtracking; a numeric no-op alone
is not a sufficient cache invariant. Each change needs focused regressions,
fresh differential/model gates and paired evaluation before acceptance.

## Reproduction, limitations and sources

The {boundbehaviorevidence}`diagnostic runner <scripts/qualification/bound_behavior/diagnose.py>`
and {boundbehaviorevidence}`receipt auditor <scripts/qualification/bound_behavior/analyze.py>`
construct the selected tests and check all 54 model replays against the exact
adapted inputs and option lists. For a new run, copy the
{boundbehaviorevidence}`configuration template <benchmarks/bound-behavior-20261114/config.example.json>`
to a private file, replace its absolute paths and retain the selected case
hashes. Native and probe digests identify this recorded run; a new build with
a different digest needs its own recorded identity. `out` must name a fresh
results location; the runner refuses to overwrite a task directory.

Build through `scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm`
with `--extra-target primo_preprocess_bound_probe`, then run its
`--check-only --no-sync` preflight against the clean source checkout before
submission. The SLURM
{boundbehaviorevidence}`wrapper <scripts/qualification/bound_behavior/diagnose.sbatch>`
requires `PRIMO_WORKDIR`, `PRIMO_BEHAVIOR_RUNNER` and `PRIMO_BEHAVIOR_CONFIG`
to name the remote checkout, runner and private JSON file. Submit inspection
mode 0 first. After it succeeds, modes 4 and 5 read its verified bound batches;
profiling modes 1–3 are independent. All modes require an allocation and the
wrapper uses `SLURM_ARRAY_TASK_ID` as the mode. The native and model tools must
be available on compute nodes; profiling additionally needs Valgrind,
`callgrind_control` and `callgrind_annotate`.

The saved receipts can be audited locally without solver execution:

```sh
python3 scripts/qualification/bound_behavior/analyze.py \
  --results results/bound-behavior-20261114 \
  --out benchmarks/bound-behavior-20261114
```

The
{boundbehaviorevidence}`observations <benchmarks/bound-behavior-20261114/observations.json>`
retain counters, receipts, validation artifact hashes, emitted bounds, profile
digests and trace lines. Large raw profiles remain in ignored
`results/bound-behavior-20261114` and the remote diagnostic workspace.
The {boundbehaviorevidence}`accounting <benchmarks/bound-behavior-20261114/accounting.psv>`
includes the rejected initial attempt: its 36 completed small-problem model
checks are retained, its failed profiler startups contribute no profiles,
and its cancelled dependent ablations contribute no executions. The retry
records the two LRA proof timeouts explicitly.

The {boundbehaviorevidence}`trace patch <benchmarks/bound-behavior-20261114/root-bound-trace.patch>`
is applied only to an isolated baseline checkout. It copies the old bound,
performs the existing assertion, then logs old and current values without
adding terms or changing solver state. Its separate binary digest and work
caps are recorded. Both trace queries intentionally return `unknown` at the
conflict cap; they contribute no SAT model checks or performance rows.

The arithmetic basis remains Dutertre and de Moura,
[*A Fast Linear-Arithmetic Solver for DPLL(T)*, CAV 2006, §4.5](https://leodemoura.github.io/files/cav06.pdf).
That section motivates heuristic explained bound refinement, not exhaustive
interval closure. This investigation exercises the existing ordinary interval
rules; it neither implements a new paper algorithm nor repairs their current
scheduling and integer-rounding gaps. The broader correctness evidence remains
the separate [qualified study](bound-propagation.md); these selected probes do
not replace a merge gate for future solver changes.
