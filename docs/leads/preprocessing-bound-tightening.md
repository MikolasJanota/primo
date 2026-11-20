# Preprocessing bound tightening

**Document purpose: implemented experimental pass and historical qualification record.** The initial study below remains blocked; the option is off by default. Current behavior is in [preprocessing](../implementation/preprocessing.md); any further acceptance work must distinguish this old study from later [bound repairs](../evidence/bound-repairs.md).

## Basis and implementation checklist

Before implementation, the interval-refinement paragraph of Dutertre and
de Moura, *A Fast Linear-Arithmetic Solver for DPLL(T)*, CAV 2006, §4.5 was
read from the [author's copy](https://leodemoura.github.io/files/cav06.pdf).
It permits refinement using equations beyond the current simplex basis.
This milestone applies that elementary interval calculation to unconditional
lowered input constraints. The bounded worklist and emission policy are Primo
engineering choices, not the paper's complete theory propagation algorithm.

The SMT {preprocessboundsource}`analysis API and owned result
<src/smt/preprocess_bound_tightening.hpp>` separate read-only analysis from
{preprocessboundsource}`materialization and the preprocessing tactic
<src/smt/preprocess_bound_tightening.cpp>`.
{preprocessboundsource}`Initial strategy construction
<src/smt/preprocessing_strategy.cpp>` schedules the pass;
{preprocessboundsource}`unit and integration tests
<tests/unit/preprocess_bound_tightening_tests.cpp>` exercise its contract.

- `analyze_preprocessing_bounds` owns normalized polynomial maps, ordered
  constraints, endpoint intervals, occurrence lists and a FIFO dirty queue.
  Inputs are const core terms. Nothing is borrowed across term creation.
- Each endpoint is an exact rational plus an open/closed flag. Integer upper
  endpoints use floor, lower endpoints use ceiling; strict integral thresholds
  move by one. Real strictness is Boolean, not infinitesimal magnitude.
- Normalize each term DAG node once per invocation with charged coefficient
  merges/scales. Collect explicit single-term bounds before visiting rows.
  Traverse positive conjunctions and signed comparisons only. Arithmetic UF
  applications are opaque; guards, disequalities and quantifiers yield no facts.
- For `sum(a*x)<=b`, sum the known minimum contributions and count missing
  endpoints. Exclude each target's contribution to isolate it. A negative
  target coefficient reverses direction. Equality supplies both directions.
  Strictness is the disjunction of the used strict premises. Each row visit
  takes linear arithmetic work and snapshots all candidates before updates.
- Every installed endpoint must be entailed by the input constraints and
  previously proved endpoints. Updating one endpoint queues its incident rows.
  Stable term ordering, original row order and FIFO scheduling fix the result
  for a given input and budget. Crossing endpoints proves a contradiction.
- Charge assertion/node visits, coefficient merges/scales, row contributions,
  target calculations and occurrence visits before work. Default budget is
  100,000 operations; zero does no strengthening. Exhaustion discards unfinished
  calculations, keeps completed deductions and resumes normal solving. This
  bounds counted operations, not rational bit complexity or wall time.
- Emit at most 4,096 strongest improved term/direction endpoints, ordered by
  term ID then lower before upper. Term creation follows analysis, through
  the term manager only. Ordinary input conjuncts carry the deductions through
  SAT and arithmetic preparation; no dynamic tableau rows or private bounds.
- The early-placement version pinned on this page runs once after extended-term
  lowering, before equation elimination and the main loop, even with that loop
  off. The rejected placement experiment below tested changing this schedule. Rebuild
  sessions recompute per query/scope. Assumptions sessions reject enabling;
  quantified queries retain their separate route. Original-model assertions
  and lowering reconstruction remain owned by the existing session/tactic.
- Validate chains, signs, rounding, strictness, missing bounds, wide rows,
  scopes, lowered auxiliaries, work/emission limits and cancellation. Independently
  refute the lowered goal conjoined with the negation of emitted bound batches.
  Validate successful native models separately from performance measurements.

For example, `x,y>=0` and `3*x+4*y=13` imply `x<=13/3` and `y<=13/4`,
hence integer `x<=4,y<=3`. The new upper bounds can then tighten lower bounds
and constraints involving other variables. Unbounded directions or cancellation
may prevent useful refinement. Real tightening can approach a limit without
reaching it; exhaustion is therefore a normal partial-success outcome.

This pass is opt-in. It does not combine rows by elimination, inspect Boolean
branches, or promise a complete arithmetic closure or a solving speedup.

## Representation

The transient implementation-only `Analysis` owns ordered polynomial and interval
maps keyed by `TermId`, a vector of constraints, occurrence lists of constraint
indices, and a FIFO of those indices. Its const input manager outlives the call.
The public `PreprocessBoundResult` owns `PreprocessBoundEndpoint` values and
statistics; it holds no views into the analysis or manager. Returned term IDs
remain valid only in that same manager, whose storage must outlive their use.
The materializer must receive that manager. It saves the target's sort before
creating its numeral, and retains no term-node borrow across manager allocations.
The returned conjuncts enter the existing goal and model-reconstruction flow.

## Qualification is blocked

The implementation is experimental and disabled by default. The generated
correctness gates passed: 5,972 reference comparisons and emitted-batch
refutations, with 28 native timeouts recorded separately. All 4,411 selected
generated SAT models, 688 supplemental models and 138 real-seed mutant models
passed direct Dolmen validation. Release and ASan each passed 49 CTest groups;
337 Python tests passed. These checks do not override the real-corpus failure
below.

The frozen 700-input comparison used 120-second solver deadlines, an 8 GiB
address-space limit, and rotated baseline/tightening order. All 752 successful
native SAT timing rows subsequently passed direct Dolmen validation in a
separate SLURM run. Timing includes 697 complete pairs. On three candidate-first
UFLRA cases the candidate aborted before the baseline ran; separate frozen
baseline runs reproduced the same aborts. Those three supplemental baseline
results classify the failure; they are not complete original timing pairs.

| Logic | Inputs | Baseline solved | Tightening solved | Baseline mean PAR2 (s) | Tightening mean PAR2 (s) |
|---|---:|---:|---:|---:|---:|
| QF_LIA | 200 | 104 | 104 | 120.56 | 119.85 |
| QF_UFLIA | 300 | 259 | 257 | 35.40 | 36.78 |
| QF_LRA | 100 | 85 | 87 | 41.28 | 36.97 |
| QF_UFLRA | 100 | 91 | 91 | 25.60 | 25.61 |
| Total | 700 | 539 | 539 | 59.17 | 58.95 |

PAR2 assigns 240 seconds to each unsolved result, including errors. Each profile
had 63 unknowns, 90 timeouts, five other memory exhaustion results and three
aborts. The preliminary run gains two LRA instances and loses two Certora UFLIA
instances. Its small aggregate timing difference is not accepted as an overall
improvement. Required rotated repetitions of the losses and substantial
slowdowns remain pending because the crash gate stopped acceptance.

The three shared aborts occur on the `cpachecker-bmc-svcomp14` inputs ending in
`rekcba_ctm_false-unreach-call.2.c.smt2`,
`rekcba_ctm_true-unreach-call.1.c.smt2`, and
`rekcba_ctm_true-unreach-call.2.c.smt2`. Under the same memory limit, debugger
backtraces for both frozen binaries locate an allocation exception in a row
vector reservation during `LraTableau::pivot`, reached through an online
arithmetic assignment callback. Unwinding then reaches
{preprocessboundsource}`AssignmentConnection <src/smt/assignment_coordinator.hpp>`,
whose destructor disconnects the external propagator while CaDiCaL is still
solving. CaDiCaL aborts on that invalid-state API call. This pre-existing
callback exception boundary requires a separate correctness repair; bypassing
the disconnect alone would leave the consumer lifetime obligation unresolved.

For the two lost UFLIA instances, tightening itself took about 4–5 milliseconds
and emitted 163 and 210 bounds. Bounded debugger samples later found arithmetic
search, including private integer branching on one instance. These samples
show that preprocessing time alone does not explain the losses; they do not
establish a complete CPU profile or prove the precise search cause.

The committed {preprocessboundevidence}`input selection
<benchmarks/preprocess-bounds-20261113/selection.json>`,
{preprocessboundevidence}`qualification record
<benchmarks/preprocess-bounds-20261113/qualification.json>`,
{preprocessboundevidence}`evaluation
<benchmarks/preprocess-bounds-20261113/evaluation.json>`,
{preprocessboundevidence}`blocking-failure record
<benchmarks/preprocess-bounds-20261113/blocker.json>`, and
{preprocessboundevidence}`run log <docs/run-log.json>` preserve exact inputs,
binary/source identities, per-family outcomes, memory, mechanism counters,
row hashes and the blocked status. The implementation remains available behind
its option, with acceptance pending. Complete the callback-failure repair and fresh
correctness qualification, then repeat the performance comparison and required
paired regressions before accepting or rejecting this experiment.

The separate [propagation follow-up](../evidence/bound-propagation.md) tests
four profiles and three fresh repetitions on 697 retained inputs, with complete
SAT-model replay. It keeps the three known abort cases quarantined and does
not lift the whole-solver qualification blocker above.


## Scheduling experiment (rejected)

The active [improvement plan](bound-propagation-improvements.md) evaluated
running analysis after the existing value/equality simplification stages and
before finite-domain processing, final arithmetic normalization and purification.
The preserved trial patch's
{boundrepairresults}`declared-logic resolution <benchmarks/bound-repairs-20261114/rejected-placement.patch#L76-L84>`
sets `PreprocessingProfile::preprocess_bound_tightening`
from the declared logic, independently of which atoms survive initial
transformations. That owned Boolean controls the
single
{boundrepairresults}`later tactic <benchmarks/bound-repairs-20261114/rejected-placement.patch#L51-L59>`;
it is not a new user option. Disabling the ordinary preprocessing loop leaves
this tactic scheduled once. Integer solved-constant substitution still precedes
it; analysis is skipped if earlier stages resolve the goal.

The
{preprocessboundsource}`analysis <src/smt/preprocess_bound_tightening.cpp#L61>`
still snapshots directly available intervals before its worklist.
`:preprocessing.bounds.redundant-endpoints` counts finite final endpoints no
stronger than that snapshot, rather than counting new facts rejected by SAT.
Moving the snapshot later can avoid emitting facts already exposed by value
substitution. It can also change automatic preprocessing-profile selection;
these interactions require separate evaluation before acceptance.
The historical qualification and timings above concern early placement.

The control used notification/rounding repair snapshot `3c2971a4`; the variant
added later placement in snapshot `e22299f9`. The combined profile enables
same-variable, forward and reverse row propagation together with preprocessing
bounds; integer branching uses the private default of those snapshots. Here Wisa denotes UFLIA
`mathsat/Wisa/xs-20-19-2-2-2-5.smt2` from the retained selection.

The trial is rejected and its source changes are reverted. The
{boundrepairresults}`observations <benchmarks/bound-repairs-20261114/placement-observations.json>`,
{boundrepairresults}`measurement table <benchmarks/bound-repairs-20261114/placement-measurements.csv>`
and {boundrepairresults}`reproduction patch <benchmarks/bound-repairs-20261114/rejected-placement.patch>`
retain the negative result. Across five selected
inputs and two rotated repetitions, all 28 successful SAT timing rows pass
separate direct Dolmen validation; 12 runs time out. LIA and LRA successes keep
the same emitted-bound and pivot counts. Wisa emits no new bounds after later
placement, but its combined-profile runtime rises from 0.504–0.505 seconds to
3.747–3.788 seconds, with pivots increasing from 3,752 to 13,007. These developmental runs overlap other cluster work. Stable bound and pivot
counts support rejection alongside diagnostic wall times; this is selected
evidence, not a general rejection of later analysis.
The earlier propagation repair changed which assertion streams help search;
numeric redundancy alone does not establish a performance win from removal.
The trial profile Boolean and redundant-endpoint counter are not retained.
