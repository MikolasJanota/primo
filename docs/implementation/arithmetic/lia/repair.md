# Integer completion scheduling and patching

This chapter explains when SAT search invokes rational repair and integer completion, how patching changes a feasible assignment, and when its cached freedom intervals must be recomputed.

Start with the [LIA overview](../../lia.md) for integration with SAT and default settings.

(lia-schedule)=
## When integer reasoning runs

The standalone {migrationsource}`solve_qf_lia_impl recipe
<src/smt/solver_engines.cpp#L10043-L10077>` selects one `LiaTheoryComponent`
and calls {migrationsource}`solve_component_recipe <src/smt/component_solver.cpp#L70-L502>`. Online rational propagation stays enabled
independently of EUF settings. Model-guided combination is disabled: with only
one component, there are no shared scopes, interface arrangements, equality
bridges, or combination fallback. QF_UFLIA selects the same LIA component
alongside EUF.

### Repair schedule and phase advice

"Repair" refers to two operations with different schedules. The following
table describes the **online arithmetic execution** route:

| Event | Rational feasibility | Integer patching/completion |
| --- | --- | --- |
| SAT assigns an arithmetic literal | Apply its bound immediately; reject direct bound contradictions. Full simplex repair may remain pending. | Does not run. |
| Online theory consistency callback during Boolean search | Batch pending work and run `LraTableau::check()` to restore rational feasibility or report a conflict. | Does not run. |
| SAT returns a complete Boolean candidate and the driver requests `FullModel` checking | Reuse a checked online tableau when the route permits, or rebuild the relaxation and check it. | With `--qf-uflia-tableau-equalities`, QF_UFLIA first checks explained shared equalities; if no delivery is needed, run patching, GCD checks, cuts and branch-and-bound on the separate completion tableau. |
| A cut or internal integer split is added during completion | Recheck rational feasibility within that private tableau. | Continue the completion pipeline; do not restart the patcher after every cut or split. |

QF_UFLIA can instead run without online arithmetic execution, depending on its
online-component option and EUF engine. Assignments then record literals
without immediately asserting their bounds into a live tableau. The full-model
check rebuilds the candidate relaxation, applies those bounds, checks rational
feasibility and then runs integer completion. Integer patching remains late. The
[shared-equality scan](../../uflia-equality-propagation.md) can return a guarded
consequence after rational repair and before that completion work. It runs at
the full-candidate boundary, not after every online bound assertion.

The {settingssource}`online arithmetic execution <src/smt/solver_engines.cpp>`
records assigned bounds and marks deferred work.
{settingssource}`ComponentDispatcher <src/smt/component_dispatcher.cpp>` requests
partial checks through the external-clause callback; `LiaTheoryComponent`'s
partial-effort branch performs rational checks only. The model-approval callback
inside SAT also requests partial effort, not integer completion. After
`backend.solve()` returns SAT, the
{settingssource}`shared driver <src/smt/component_solver.cpp>` calls
{settingssource}`ComponentModelCoordinator::reconcile <src/smt/component_model_coordinator.cpp>`,
which requests `TheoryCheckEffort::FullModel` from the LIA component. That is
the integer-completion boundary. Online batching is an intermediate schedule
for **rational** repair, not eager integer patching.

A full Boolean candidate selects the currently asserted arithmetic literals;
it is not yet an accepted model of the original formula. Integer feasibility
and, on QF_UFLIA, component-model agreement can still fail. A learned conflict
or another combination choice can lead to another SAT call and another integer
completion within the same `check-sat`. Thus "late" does not mean once after
the entire SMT query has already been solved.

The table also distinguishes the model used by phase advice. When enabled,
`ComponentLraExecution::model_phase_literal` reads the live rational assignment
β. `LiaComponentAdapter` keeps the successful completion model separately;
integer patching does not write its assignment back into that live tableau.
A **shared model projection** partitions shared terms by their equal values in a
candidate model. The [combination procedure](../../combination.md#comparing-complete-model-projections)
compares those partitions from the participating theory solvers.
Standalone QF_LIA, and an online LIA component not producing a shared model
projection, start completion from a copy of the checked online tableau. The
model-guided QF_UFLIA route instead requests a projection and rebuilds a
rational tableau from the prepared base and full candidate literals before
completion. That rebuilt assignment may already differ from live β before
integer patching starts. See the
{ref}`phase-control investigation <narrow-region-phase-candidate>`.

Eager integer repair after every assigned bound, or bounded intermediate
integer-repair checkpoints on feasible partial assignments, are possible
alternative schedules; neither is implemented as a scheduling option here.
An integral assignment to a partial arithmetic trail does not establish SAT
for the whole formula. A limited unsuccessful attempt is not an explained
integer conflict. Comparing schedules would require separate correctness and
performance evidence, including the additional repair work.

The {retentionsource}`driver observes otherwise unowned Boolean constants <src/smt/component_solver.cpp#L276-L291>` through SAT's
assignment interface, in the original atom registry order. Their assignments
remain propositional: the dispatcher does not route them into the LIA component.
Observing them preserves the standalone route's SAT elimination policy while
making their values available in the shared assignment view and final model.

At the full-model check boundary, the LIA adapter copies the rationally feasible
online tableau once, or moves a newly rebuilt rational tableau, into integer
completion. It passes {integersource}`LiaCompletionLimits
<src/theory/arithmetic/lia_completion.hpp>` (patch moves, Gomory rounds, and
branch nodes). Both routes use the following stage order. The cube attempt is available only
in standalone QF_LIA:

| Order | Operation | Result used by the caller |
| --- | --- | --- |
| 1 | Patch fractional integer values | A feasible assignment with fewer fractional integer variables, or no progress. |
| 2 | Check row divisibility using GCDs | A conflict, or no contradiction found. |
| 3 | If enabled, attempt a cube model (QF_LIA only) | A checked integer model, returned immediately; otherwise continue. |
| 4 | Add a bounded sequence of Gomory cuts and recheck | A conflict, or a feasible tableau strengthened by the cuts. |
| 5 | Branch on fractional integer variables | An integer model, exhaustion of all branches, or a node-limit result. |

GCD checks and cuts run before branching, not at each branch node. A zero
cut or patch budget skips that stage. Even if patching makes every required
variable integral, the final success decision comes through branch-and-bound's
integrality check, which then needs no branch nodes.

A refuted candidate produces a clause over the original arithmetic atoms.
The shared driver adds it to the SAT backend and searches for another Boolean
candidate. It reports `unsat` for the whole query when that search is exhausted;
integer infeasibility of one candidate does not by itself refute a formula
with other Boolean choices.

The {retentionsource}`standalone recipe <src/smt/solver_engines.cpp#L10051-L10057>`
sets {retentionsource}`CombinationRecipe::forgettable_clauses <src/smt/component_solver.hpp#L24-L26>` to false:
its online external theory clauses remain permanent in the SAT backend, preserving the
standalone route's retention policy.
{retentionsource}`ComponentDispatcher <src/smt/component_dispatcher.cpp#L28-L39>` stores this policy
for its lifetime and {retentionsource}`applies it to external clauses <src/smt/component_dispatcher.cpp#L259-L262>`,
including after the driver {retentionsource}`rebuilds the dispatcher for new atoms <src/smt/component_solver.cpp#L264-L274>`. Combined recipes retain the default true
policy, which permits SAT clause deletion. This affects search and memory use,
not the validity of the theory clauses.

`QF_UFLIA` calls the same `complete_integer_assignment` operation through
{migrationsource}`complete_lia_integer_model
<src/smt/solver_engines.cpp#L8455-L8476>`. Its
{migrationsource}`LIA component adapter <src/smt/solver_engines.cpp#L9502-L9645>`
keeps the winning tableau, exports shared integer values, or returns a learned
clause/resource-limit result, or a hybrid split request, to the general combination driver. Rational
online propagation and final integer completion have separate responsibilities.
{migrationsource}`LiaTheoryComponent <src/smt/solver_engines.cpp#L9900-L10037>` owns this adapter and the optional online rational
execution state. {migrationsource}`ComponentAdapterSupport <src/smt/solver_engines.cpp#L9651-L9757>` owns the assignment view, registered
atoms, interface terms, and model-token revision. Atom growth destroys the
online execution state before changing the preparation it borrows; assignment
and backtracking invalidate the exported model token.

The recipe passes model-projection policy, {migrationsource}`LiaComponentTiming <src/smt/solver_engines.cpp#L8437-L8441>`
and the route's resolved `LiaSettings` ({ref}`lia-settings`) explicitly.
The adapter does not read QF_UFLIA combination options to select its behavior.
The combined recipe retains its check, rational, and integer timer keys; empty
keys disable timer sampling. Both rebuilt rational checks and completion of an
online tableau use {migrationsource}`LiaComponentAdapter::translate
<src/smt/solver_engines.cpp#L9588-L9635>` to replace the retained
model and return a consistent, learned-clause, resource-limit, or hybrid split result. If
requested projection omits a shared Int term, translation returns `Unknown`
with that diagnostic. Shared
model projection is produced only when the recipe requests it. The shared-sort
contract is Int-only.

The {ref}`combination chapter <the-component-collection-route>`
explains the remaining equality arrangements and model-agreement checks.

{hybridsource}`LiaCompletionResult <src/theory/arithmetic/lia_completion.hpp>` returns a typed `Sat`, `Unsat`, `ResourceLimit`, or hybrid `Split`,
one accumulated `LiaExplanation`, the number of patched variables, and
`LiaPatchingStats`. The completion operation does not copy or prepare the
tableau. Private mode preserves the supplied selection order; hybrid mode
uses history scoring. On success the
caller reads the winning assignment directly; after a failed or limited branch
search, cuts remain but speculative branch bounds are removed. It does not
restore the entry basis or assignment. The caller discards unsuccessful working
copies. A zero branch
budget permits an already integral assignment but no branch node.
Hybrid budget exit repairs rational feasibility and rescans integrality before
returning a model or split request; see {ref}`lia-hybrid-branching`.

Both routes use SMT integration's
{migrationsource}`integer_conflict_clause <src/smt/solver_engines.cpp#L8382-L8396>` to collect blocking
literals from the asserted arithmetic atoms. It handles an empty coarse clause
explicitly; otherwise the existing optional generalization checks the accumulated
explanation against that clause. Individual failed integer branches do not increment the
aggregate conflict budget; the SMT caller counts the reported theory conflict.
The {integersource}`patcher's fully_integral statistic
<src/theory/arithmetic/lia_patching.hpp>` uses its maintained remaining-variable
count, avoiding another scan. {migrationsource}`Both routes publish it <src/smt/solver_engines.cpp#L8372-L8380>` when patching is enabled.

With cubes disabled in private mode, the completion boundary preserves the existing
stage order, variable order, limits, winning assignment, and antecedents from
cuts used by later branching. Enabling cubes inserts one private model attempt
between the basic GCD screen and cuts. It adds sparse preparation, a separate
rational feasibility problem and candidate validation; the other stages keep their existing contracts.

Private mode keeps integer branch choices inside the theory procedure. SAT
learns the resulting conflict clause, but does not see individual integer
splits as SAT decisions. That simplifies the boundary and the lifetime of
temporary rows; it also means that internal branch choices and cuts are not
reused across successive Boolean candidates.

(lia-patching)=
## Patching a feasible assignment

{source}`patch_integer_variables <src/theory/arithmetic/lia_patching.cpp>`
tries to find an integer model by moving nonbasic values without violating
any bounds. It derives from Bjørner and Nachmanson §4.1, with a bounded
candidate search and a global improvement criterion specific to Primo.

For a nonbasic variable $y$, its **freedom interval** is the intersection of
its own bounds with the intervals induced by every basic row containing $y$.
If $x_b$ has coefficient $a\ne0$ for $y$, moving $y$ by $\Delta$ changes
$x_b$ by $a\Delta$. To set $x_b$ to an integer $n$, the candidate move is

$$\Delta=\frac{n-\beta(x_b)}{a},$$

where $\beta$ denotes the current assignment. A candidate is admissible only
if the proposed value of $y$ lies in its freedom interval. This preserves
both row equations and feasibility; patching does not call simplex to repair
an inadmissible move. A fixed variable has a singleton freedom interval and
cannot move away from its fixed value.

Primo also considers directly moving a fractional nonbasic integer variable.
Unlike the simplifying assumption in the paper, nonbasic variables here may
sit inside their bounds, and even an integer variable's bound may be fractional.
For each fractional target, the search considers eight integers around its
delta-aware floor $m$: $m,m+1,m-1,m+2,\ldots,m-3,m+4$, ordered by distance
from the rational part. This is a heuristic window, not an exhaustive search
of its feasible values.

The width is currently the hard-coded `kTargetWindow = 4`; the existing
`--lia-patch-limit` controls accepted moves, not this width. A
[separate open lead](../../../leads/lia-patching.md) proposes a window parameter
and compares more selective candidates with
[mixed-integer-programming rounding and repair heuristics](../../../papers/lia-primal-heuristics.md).

Every candidate is scored by the total number of required integer variables
that would remain nonintegral. Only a strict reduction is accepted. Among
inspected candidates with the same count, a smaller rational move wins;
the scan stops early on finding a move that makes the whole assignment
integral. Each accepted move decreases a nonnegative integer, so the sweep
terminates even apart from its configured move budget. Without
`--lia-patch-integral-moves`, the QF_UFLIA default, it can make an individual previously integral variable fractional, provided the
total count improves. An integral assignment is still subject to the
ordinary model checks.

`--lia-patch-integral-moves` (on by default for QF_LIA) removes that freedom for
integer nonbasic variables: a candidate that would leave one at a
non-integer value is rejected. In exchange, an integer nonbasic $y$ with
coefficient $a=p/q$ in the row of a fractional basic $x_b$ also gets
**lattice candidates**. With $c=\beta(x_b)-a\beta(y)$, setting $y=k$ gives
$x_b=c+ak$, which is integral exactly when $c$ has no infinitesimal part,
$qc$ is an integer and $pk\equiv-qc\pmod q$. The solutions are $k_0+jq$;
$k_0$ uses the inverse of $p$ modulo $q$, computed by an iterative extended
Euclidean algorithm over integer-valued `Rational`s (`numerator()` and
`denominator()` give $p$ and $q$). The eight nearest to $\beta(y)$ are scored
like any other candidate. For $s=\tfrac{11}{3}y$ with $y=1$, the target
window only offers fractional $y$, while the lattice proposes $y=3$, $s=11$.
This generator is Primo's own inference, adapted from the modular-inverse
reduction of Achterberg et al. §3.5; it is not a procedure from the patching
paper. Counters `:lia.patch-fractional-candidates-rejected`,
`:lia.patch-lattice-candidates-scored` and `:lia.patch-lattice-moves-accepted`
record its work.

### Why a nearest move is insufficient

Use the paper's Example 3:

$$y=x/2,\qquad z=x/3,$$

with $3\le x\le10$, $-3\le y\le4$, $-5\le z\le12$, and initial
values $(x,y,z)=(3,3/2,1)$. All three variables must be integers.
The freedom interval of nonbasic $x$ is $[3,8]$: $y\le4$ supplies the tighter
upper endpoint. Moving $x$ to $4$ fixes $y$ but makes $z=4/3$ fractional;
the total remains one fractional variable, so Primo rejects that move.
Moving $x$ to $6$ gives $(6,3,2)$ and reduces the count to zero. Considering
only the floor and ceiling of $y=3/2$ would miss that target.

The {source}`scoring index <src/theory/arithmetic/lia_patching.cpp>`
stores, for each nonbasic variable, the basic integer rows that contain it.
Scoring adjusts a cached global count using only the moved variable and
those affected rows. No pivot occurs during the sweep, so this incidence
index stays valid. The scoring cost per candidate is proportional to the
affected integer column, rather than all integer variables; generating
candidates and calculating freedom intervals still have their own costs.

{source}`LiaPatchingStats <src/theory/arithmetic/lia_patching.hpp#L10-L26>`
exposes occurrence entries, candidates scored, affected rows inspected,
the equivalent full-scan work, and accepted moves that left an integer
nonbasic variable fractional. The public counters include
`:lia.patch-occurrence-entries`, `:lia.patch-candidates-scored`,
`:lia.patch-affected-rows-inspected`, `:lia.patch-full-scan-equivalent` and
`:lia.patch-fractional-nonbasic-moves`.
The {source}`patching tests <tests/unit/lia_patching_tests.cpp>` cover the
paper example, fixed and strict bounds, and comparison of indexed scoring
with an independent full-scan definition.

(patching-interval-reuse)=
## Patching interval reuse

{patchsource}`patch_integer_variables <src/theory/arithmetic/lia_patching.cpp>` owns an
`unordered_map<LraVarId, LraTableau::FreedomInterval>` for candidate selection.
Each {patchsource}`FreedomInterval <src/theory/arithmetic/lra_tableau.hpp>` entry
owns its optional exact `DeltaRational` endpoints; it borrows no
tableau rows or assignments. The first feasibility query for a moved nonbasic
computes its interval, and further candidates for that variable reuse it,
including candidates generated for other integer targets and lattice candidates.
The query remains before the fractional-candidate rejection, preserving existing
scoring and rejection-counter meanings.

The tableau's basis, bounds and assignment stay fixed during a selection attempt.
An accepted move can change intervals of other nonbasics through shared rows:
the whole map is cleared immediately after the move and before the next attempt.
No cache survives the patching call. Patching still never pivots. If future
candidate generation mutates the tableau, it must invalidate the map first.

For example, use `integer_vars = {x, z}` and start these integer nonbasics at
$x=z=1/2$ with continuous row $s=x+z$
and bound $s\ge1$. Candidate selection chooses $x:=1$ first. This changes
$z$'s lower freedom endpoint from $1/2$ to $0$, so the next selection chooses
$z:=0$. Retaining $z$'s old interval would choose a different second move.

The new `:lia.patch-interval-requests`, `:lia.patch-interval-computations`
and `:lia.patch-interval-cache-hits` counters obey requests = computations + hits.
Computations equal the number of distinct queried variables summed over attempts,
including a final unsuccessful attempt. Cache lookups have expected constant
cost. Live entries are linear in the distinct queried variables in the current
attempt; bucket storage can remain at the peak reached earlier in the call
because `clear()` retains the bucket allocation.
The underlying interval query still searches coefficients in affected rows;
this change does not make that query constant-time or replace its representation.

This is a Primo engineering adaptation of the derived-information maintenance
principle in Bromberger (2019), §7.1.4, pp. 225–226 {cite:p}`bromberger2019`,
not an implementation of the thesis's `VarInfo` scheme or a new patching
heuristic. The [Bromberger checklist](../../../papers/bromberger-linear-arithmetic.md#efficiency-review-checklist)
states the invariant and test obligations. Tests in
{patchsource}`lia_patching_tests.cpp <tests/unit/lia_patching_tests.cpp>` cover cross-target reuse, both candidate
families, exact strict/unbounded endpoints, accepted-move prefixes and invalidation
through a shared row. Snapshot `cab1ebc4` is qualified against `75e10877` on the
selected diagnostics/development sets; the [measurement and correctness record](../../../evidence/arithmetic-overhead-20261109.md)
includes fresh generated/mutation gates, every successful native SAT timing
row's direct model check and supplementary runtime controls. This is scoped
acceptance, not a new search heuristic or an overall reference-gap claim.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lia.md#sources-and-implementation-status) and [regression obligations](../../lia.md#models-regression-coverage-and-retained-evidence).
