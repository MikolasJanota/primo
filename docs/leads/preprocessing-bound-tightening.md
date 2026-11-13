# Preprocessing bound tightening

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
- Run once immediately after required extended-term lowering, before equation
  elimination and the main preprocessing loop, even with the loop off. Rebuild
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
