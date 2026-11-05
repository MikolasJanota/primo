# Phase-region simulation protocol

**Status: isolated diagnostic under qualification; no default proposal.**
This follows the [CSP reading](../papers/csp-restarts-phase-control.md) and
the [earlier phase diagnostic](adaptive-phase-probe.md). It is an original
bounded adaptation, not an implementation of finite-domain impact-based search.
Baptista §6.4.1 motivates varying complementary branch orientation; it does
not provide the geometric gate below or predict a Primo improvement.

## State, invariants and operations

The {regionsource}`ArithmeticPhaseSimulation controller
<src/smt/arithmetic_phase_simulation.hpp>`, owned by
{regionsource}`ComponentLraExecution <src/smt/solver_engines.cpp>`, keeps phase
choices and integer-bound snapshots within that execution's lifetime. A
tableau variable keeps its prepared semantic meaning through pivots; all
history is discarded when the execution/preparation is rebuilt. β denotes
the current rational tableau assignment. The controller borrows
no terms or tableau references. Owned rational endpoints describe closed
integer candidate ranges, using the existing infinitesimal-aware floor/ceil.
This is an interval abstraction, not a count of combined EUF/LIA models.

At each phase refresh, record the current rounded bound range once per bound
variable. Entering a different finite range increments its visit count; repeated
refreshes in the same range do not. History is capped at 4096 FIFO entries.
An atom's sampled preference remains fixed until its bound-range generation
or β sign changes, or the atom is observed assigned at a refresh. Assigned
atoms receive β and lose their cached choice, so a later unassigned refresh
can resample even with the same bounds and β. Before the initial assignment
view is attached, only pending preferences are sampled; these are not observed
decisions. The policy cannot reverse a
propagated literal or a forced assumption. Initial preferences may be installed
before a future assumption is known, but assumptions retain their forced sign.

For a currently unassigned inequality on a known integer-valued expression,
intersect the parent range with each polarity's prepared bound. Require both
children to have positive finite candidate counts. A narrow child has at most
four candidates and the other has at least four times as many. The gate requires
at least two recorded entries into that narrow child's exact range for the
history-gated random policy. The two deterministic geometry controls omit
that history condition, separating geometry from repeated-entry detection.

The policies are:

- observe: measure contexts without changing β;
- random: flip β with probability 1/d at a new hint context;
- gated: the same random flip only when β selects the revisited narrow child;
- least: whenever the narrow/ratio test holds, prefer the child with more candidates;
- most: under that same geometry test, prefer the child with fewer candidates.

The sparse denominators are 16 and 4. Random sampling uses C++'s standard
`mt19937` and uniform integer distribution, with explicit recorded seeds.
These are persistent context choices, not the thesis's once-per-node sampling.
The gate measures revisits, not proven lack of clause-learning progress.
Unbounded, unknown-integer and empty-child geometry cannot activate the gate.
Random controls can still perturb an inequality with no finite geometry.

No policy adds a bound, atom, assumption, variable choice, clause or restart.
SAT selection, learning and existing resource controls remain responsible for
search. Only sign delivery changes. Equality atoms remain outside this producer.
Every recommendation uses bounded container work and exact arithmetic; endpoint
comparison/formatting costs depend on rational operand sizes. Cached contexts
avoid repeated random draws and history formatting on unchanged bounds.

The earlier SAT-layer `PhaseProbe` is restored only as observe instrumentation:
it identifies actual observed decisions through the backend predicate and
separates propagations/assumptions. Its alignment is with the installed sign,
not counterfactual β. Arithmetic flip/context counts do not prove that a later
decision changed. Both streams are retained and reported with that limitation.

## Qualification and measurement

Build the clean snapshot through `slurm_prepare_benchmark_workspace.py
--build-via-slurm`. Run deterministic controller tests for strict/negative bounds,
finite/unbounded ranges, both semantic atom signs, history re-entry, FIFO limits,
stable sampling, assigned-literal exclusion and least/most/gated distinctions
in the {regionsource}`controller regressions
<tests/unit/arithmetic_phase_simulation_tests.cpp>`.
Run solver checks, ASan, a fresh mixed QF_UFLIA reference gate and whole-tree
static review on SLURM before accepting timing claims. Keep failures blocking.

Reuse the committed four development losses, all 30 recorded losses and 14
family controls from `scripts/qualification/adaptive_phase/selection.json`.
Compare native, M124 without β, uninstrumented β, observe, sparse random,
gated random, least and most. Use 15 seconds per process, rotated arm order
and three recorded seeds for the randomized policies. This is a targeted
diagnostic, not a full-corpus or default-promotion evaluation. Validate every
successful SAT timing row separately with direct Dolmen acceptance.

Retain exact source/binary/input hashes, commands, resource limits, context and
decision counters, per-family outcomes and PAR2 in artifacts/run-log metadata.
After measurement, decide the prototype's disposition and restore production
source unless further qualification establishes a reason to adopt it.
