# DPLL(T): connecting Boolean search and theory reasoning

The [SAT chapter](sat.md) explains how assertions become clauses. Those clauses
describe the Boolean structure, but do not by themselves enforce equality,
arithmetic, or agreement between theories. DPLL(T) connects Boolean search to
those obligations through explained conflicts and consequences.

Primo delegates decisions, Boolean propagation, conflict analysis, learning,
and backjumping to CaDiCaL. Its SMT drivers translate SAT assignments into
theory facts, schedule theory checks, and decide whether a candidate can become
a solver answer. A shared SMT driver serves QF_LIA, QF_UFLIA and
quantified UF; other routes retain dedicated drivers. This chapter describes
their common contract and the differences that affect correctness. The
[theory solver interface](theory-interface.md) defines the operations and
results supplied by a theory implementation;
[EUF](euf.md) explains congruence closure, its reasons, and theory undo.
[Linear real arithmetic](lra.md) explains the tableau and its repair operations.
The [theory-combination chapter](combination.md) follows equality exchange,
model-guided choices, and arrangement fallback in the general component driver
and the dedicated Real coordinator.

## Representation and ownership

An engine receives assertion `TermId`s, options, model-conversion records, and
a live term manager. Its result is a {source}`CheckSatOutcome
<src/smt/check_sat.hpp#L210-L217>`, not a raw SAT assignment. The
[architecture chapter](architecture.md) explains engine selection and how the
session turns the outcome into an SMT-LIB response.

| State | Owner and representation |
| --- | --- |
| SAT trail and clause database | CaDiCaL behind {source}`CadicalBackend <src/sat/cadical_backend.hpp>`. Neither SAT literals nor the backend contain core terms. |
| Primo's observed assignment | {source}`AssignmentCoordinator <src/smt/assignment_coordinator.hpp>` is the sole writer of {source}`sat::Trail <src/sat/trail.hpp>`. Consumers read {source}`sat::TrailView <src/sat/trail_view.hpp>`; truth comes from the trail's variable-to-position index. |
| Meaning of SAT variables | {source}`BooleanClausifier <src/smt/clausifier.hpp#L20-L55>` owns the atom registry and term-to-literal map, borrowing the backend and term manager. |
| Dedicated EUF callback state | {source}`OnlineEufPropagator <src/smt/solver_engines.cpp#L1634-L1673>` borrows the atom vector, terms, options, and statistics; owns atom-index metadata, candidate queues, reasons, and optional incremental closure state; reads the shared assignment view. |
| Dedicated arithmetic callback state | {source}`OnlineLraPropagator <src/smt/solver_engines.cpp#L6972-L7011>` owns a mutable tableau copied from prepared structural state, checkpoints for backtracking, queues, and reasons; reads the shared assignment view and borrows the prepared data, atom vector, backend, and statistics. |
| Component routing | {source}`ComponentDispatcher <src/smt/component_dispatcher.hpp#L23-L75>` owns variable routes, temporary assignment deltas, stream cursors, and explanation clauses; it borrows component pointers. |
| Component facts and outcomes | {source}`AssignedTheoryLiteral and TheoryCheckResult <src/theory/theory_solver.hpp>` carry a core atom, its SAT literal/value, clauses, consequences, model stamps, or a typed stop result. {source}`TheoryComponent <src/theory/theory_component.hpp#L90-L140>` owns theory-local execution behind this contract and receives no SAT backend handle. |

Copying a `TermId` or `sat::Lit` does not extend its owner's lifetime. Registration
and callbacks can grow vectors/maps, so borrowed entries must not survive those
mutations. The [core ownership rules](core.md) still apply. Connecting a
propagator does not transfer ownership to SAT; callbacks must stop before their
targets are destroyed or their routing is replaced. The {source}`AssignmentConnection guard <src/smt/assignment_coordinator.hpp>`
disconnects with the consumer and shared view still alive, then unbinds the view.
The {source}`component driver <src/smt/component_solver.cpp>` disconnects the
propagator while preparing new atoms, then reconnects it before search resumes.

## Follow an assignment through the callbacks

The callback protocol is defined by {source}`ExternalPropagator
<src/sat/ipasir_up.hpp#L40-L73>` and implemented through the
{source}`CaDiCaL adapter <src/sat/cadical_backend.cpp#L35-L85>`.
Only observed variables reach theory assignment routing. Their notifications
may be batched; a callback must reason over the notified trail, not assume a
fresh independent model each time.

1. **Receive assignments.** A signed integer reports which literal became
   true. The coordinator rejects invalid/opposite events, ignores identical
   duplicates, updates the shared view, then delivers the new literals. {source}`EUF routing <src/smt/solver_engines.cpp#L2008-L2033>` looks up
   its atom and updates the incremental state
   when present. {source}`ComponentDispatcher::notify_assignment
   <src/smt/component_dispatcher.cpp#L64-L85>` batches deltas by owner and marks
   affected components dirty. Optional relevance filtering is an SMT policy;
   it is not a license for a theory to ignore arbitrary assigned constraints.
2. **Open a SAT decision level.** The coordinator saves the trail boundary.
   EUF records the new level, creating its closure checkpoint lazily through {source}`apply_new_decision_level
   <src/smt/solver_engines.cpp#L1741-L1745>`. The component dispatcher tells
   every component to `push_level`, even when the SAT decision itself names
   no theory atom. SAT levels are not SMT-LIB assertion scopes.
3. **Check or propagate.** A conflict supplies a clause falsified by the
   current trail. A propagation supplies a literal with an explanation.
   In the component route, {source}`partial_check
   <src/smt/component_dispatcher.cpp#L132-L161>` visits dirty components;
   components marked `check_on_full_model` wait for a complete assignment.
   These checks use `Partial` effort even inside the model callback.
4. **Let SAT react.** CaDiCaL uses the explanation for conflict analysis or
   propagation and selects the resulting backjump. Primo supplies theory
   reasons, not its own CDCL implementation.
5. **Undo assignments.** The coordinator removes the suffix and supplies its
   exact literals after updating the view. EUF's {source}`apply_backtrack
   <src/smt/solver_engines.cpp#L1747-L1765>` restores closure state, removes discarded
   reasons, and clears pending candidates/conflicts. The
   {source}`component dispatcher <src/smt/component_dispatcher.cpp>` similarly
   prunes reasons and restores each component's theory state to the reported level.
   Root and retained-level facts survive; same-level rollback is a no-op.

Routing a batch is linear in its literal count plus the receiving components'
work. Streaming a clause costs one callback per literal plus a terminator.
Neither observation bounds the cost of a theory check, explanation, rollback,
or the full search. Dense SAT-variable arrays also consume space up to their
largest registered index, including gaps.

(assignment-rollback-regression)=
### Decision boundaries and rollback

In the shared {source}`trail <src/sat/trail.cpp>`, boundary `k` marks the start
of level `k+1`. Rolling back to `new_level` keeps exactly that many boundaries
and preserves root assignments. The theory's separate
{source}`checkpoints <src/smt/solver_engines.cpp#L1366-L1390>` must preserve the
same facts.

The {source}`callback tests <tests/unit/euf_callback_tests.cpp>` exercise root
retention, retained levels, and repeated backtracks through empty observed
levels. They check explanations against every interpretation of the fixture's
five constants. The [repair record](../review-history.md#euf-callback-rollback-regression)
documents the earlier boundary error and the limits of those tests.

### Shared assignment ownership

CaDiCaL owns the full SAT trail. Its API reports assignments and backtrack levels,
so Primo keeps one trail of observed assignments per SAT connection.
{source}`AssignmentCoordinator <src/smt/assignment_coordinator.hpp>` updates that
trail before notifying the theory integrations. The dedicated EUF/arithmetic,
mixed, component, and quantified-UF consumers all read it through
{source}`sat::TrailView <src/sat/trail_view.hpp>`.

The shared trail records which observed literals are true and where they occur
in the assignment order. Each theory still owns the changes those assignments
cause: for example, EUF class merges or arithmetic bounds. Its undo log restores
that theory state when the coordinator reports a backtrack.

Primo's trail contains observed assignments, not every SAT assignment. Ordinary
Tseytin variables that only encode Boolean connectives stay inside CaDiCaL.
For online recipes, the shared driver also {retentionsource}`observes Boolean constants it owns
<src/smt/component_solver.cpp#L276-L291>`, preserving the standalone LIA route's
SAT observation policy. These assignments are on the shared trail but have no
theory routing destination. Generated Boolean terms used as function arguments
have an additional theory role. For example,
with `f : Bool → U`, the frontend rewrites `f(and p q)` to `f(b)` and defines
the fresh Boolean term `b` by `b ↔ (p ∧ q)`. The
{source}`Boolean-argument registration <src/smt/clausifier.cpp#L104-L156>` observes
the variable for `b`. Its assignment tells EUF to merge `b` with `true` or
`false`, with the assigned literal as the reason; this creates no separate SAT
variable for that equality. A generated term used by EUF therefore has a
theory role in addition to its propositional definition.
Every SAT decision level still gets a boundary, including levels with no
observed assignments.

This extra storage follows from the current notification interface and
consumer requirements; it is not inherent to DPLL(T). Avoiding it would need a
backend API for assignment/order/rollback access with defined mutation and
lifetime rules. The current {source}`dense position index <src/sat/trail.cpp>`
also has a memory limitation: unobserved variable IDs can leave empty slots
below the largest observed ID, even though their literals are not on Primo's trail.

{source}`ComponentAssignmentView <src/theory/component_assignment.hpp>` stores
atom-registration metadata and projects values from the shared view. Full
component checks build temporary snapshots; quantifiers order their projection
by SAT trail position to preserve scheduling. Lazy routes load a completed-model
snapshot through the same read interface, without enabling online propagation.
The mixed route registers newly generated interface atoms before each solve,
including when only arithmetic uses online callbacks. The interface manager
reads their assignment from the view; its tentative model-equality values are
separate theory state.

Online model callbacks validate notified state instead of filling missing
assignments. Retained sessions reconnect with a new owner and re-observe root
facts after the preceding connection has settled its reason callbacks.

## Conflicts, propagations, and explanations

For currently true theory literals `l₁, …, lₖ`:

| Theory result | Clause supplied to SAT |
| --- | --- |
| Their conjunction is inconsistent | `¬l₁ ∨ … ∨ ¬lₖ` |
| They imply an unassigned literal `q` | `¬l₁ ∨ … ∨ ¬lₖ ∨ q` |

These clauses must follow from the theory and any correctly represented
premises. A model preference such as “these two terms happen to have equal
values” is not an entailed equality. Such choices need assumptions or an
explained case split, not unconditional propagation.

For example, if `a ∧ ¬b` implies `q`, the reason sent to SAT is
`¬a ∨ b ∨ q`. The interfaces use two explanation-storage conventions:

| API | Stored literals | Conversion to the SAT reason |
| --- | --- | --- |
| Component API: `TheoryCheckResult::reasons` | `[a, ¬b]`, the premises that hold | {source}`implication_clause <src/smt/component_dispatcher.cpp#L18-L24>` negates the premises and adds `q`. |
| Shared equality batch: `ExplainedEquality::reasons` | `[a, ¬b]`, the premises that hold | The [outer driver](uflia-equality-propagation.md) materializes the equality atom, negates the premises and adds its literal. |
| Dedicated EUF/LRA propagators | `[¬a, b]`, the other literals of the reason clause | The reason callback adds `q` and returns the stored literals unchanged. |

The premises must be negated exactly once. Negating the dedicated propagator's
stored `[¬a, b]` again would incorrectly produce `a ∨ ¬b ∨ q`.

{source}`EUF reason storage <src/smt/solver_engines.cpp#L2170-L2209>` checks
that a candidate reason contains its conclusion exactly once and that its
other literals are currently false and observed. This is a shape/trail check,
not an independent proof of theory validity. The
{source}`reason callback <src/smt/solver_engines.cpp#L1863-L1890>` streams the
propagated literal followed by the other clause literals, ending with zero. The component route similarly
{source}`stores a reason before returning a propagation
<src/smt/component_dispatcher.cpp#L203-L238>`.

Reason delivery can be delayed until SAT asks for it. That does not mean the
reason is always computed lazily: a propagator may already have built and
stored it. Delayed computation must preserve a valid dependency on the trail
at propagation time; explaining a literal using a consequence derived later
from that same literal can create a cycle in conflict analysis.

### Reason lifetime is separate from clause retention

A reason for a propagation that survives backtracking may still be requested.
The {source}`LRA rollback <src/smt/solver_engines.cpp#L7024-L7056>` and component
dispatcher erase reasons for assignments actually removed, preserving earlier
ones. Pending, undelivered candidates may be discarded and rediscovered; live
reasons are not interchangeable with that queue. The existing
{source}`root-reason lifetime regression
<tests/unit/component_combination_tests.cpp#L527-L589>` checks this distinction.

The dedicated {source}`EUF conflict callback
<src/smt/solver_engines.cpp#L1892-L1916>` and
{source}`LRA conflict callback <src/smt/solver_engines.cpp#L7227-L7237>` request
non-forgettable external clauses in the API variant carrying that flag.
The shared {retentionsource}`ComponentDispatcher::cb_has_external_clause
<src/smt/component_dispatcher.cpp#L259-L262>` uses its recipe's retention policy.
{retentionsource}`CombinationRecipe::forgettable_clauses
<src/smt/component_solver.hpp#L24-L26>` defaults to true, permitting deletion for
the existing combined recipes. The {retentionsource}`standalone LIA recipe
<src/smt/solver_engines.cpp#L10051-L10057>` sets it to false to preserve its
permanent external clauses. Backend retention of a
requested reason, a propagator's C++ storage, and SAT's learned backjump clause
are three separate things. The [Yices note](../papers/yices.md) and
[forgetting investigation](../leads/theory-lemma-forgetting.md) cover the
remaining questions about deletion timing and termination across restarts.

## Lazy and online routes

The EUF engine choice and propagation schedule are separate options. The
{source}`defaults <src/smt/check_sat.hpp#L220-L226>` select incremental EUF with
automatic scheduling; {source}`mode resolution
<src/smt/solver_engines.cpp#L484-L553>` chooses a schedule for the goal.
The {source}`CLI options <src/main.cpp#L631-L635>` are
`--euf-engine` and `--euf-prop`.

| Route | During SAT search | After a SAT candidate |
| --- | --- | --- |
| EUF `--euf-engine=lazy --euf-prop=off` | No EUF external propagator | Build/check the selected theory assignment; add a conflict clause and solve again when inconsistent. |
| Incremental EUF with `--euf-prop=conflict` | Maintain the closure and deliver conflicts; do not return positive theory propagations through `cb_propagate` | Reuse the synchronized closure's conflict verdict. |
| Incremental EUF with `--euf-prop=online` | Maintain the closure, deliver conflicts, and attempt budgeted explained propagation | Reuse that closure's verdict. Propagation is not exhaustive. |
| EUF `--euf-prop=model` | No online propagator in the dedicated UF driver | Attempt propagation lemmas from the full assignment, and perform the selected final consistency check. |
| Dedicated LRA with CaDiCaL | Maintain a backtrackable tableau; finish deferred repair through the external-clause callback | Reuse the tableau conflict state; extract a model if requested. Turning off optional bound propagation does not turn off consistency checking. |
| Online component recipe | Route assignments and partial checks through `ComponentDispatcher` | Inspect pending lemmas/failures, then perform full component checks and shared-model reconciliation. |

The owning loops are {source}`solve_qf_uf_engine_impl
<src/smt/solver_engines.cpp#L10381-L10593>`,
{source}`solve_qf_lra_impl <src/smt/solver_engines.cpp#L8183-L8310>`, and
{source}`solve_component_recipe <src/smt/component_solver.cpp#L70-L473>`.
“Lazy” above describes when the theory check runs; it does not describe lazy
reason construction or scoped incremental sessions.

The [policy reference](../reference/policies.md) gives the effective `auto`
selection rules, candidate/delivery budgets, and phase precedence, including
a worked budget example and the relevant counters.

## When a candidate becomes an answer

`backend.solve() == Sat` means the backend has satisfied the current Boolean
clauses and its callback obligations. The SMT driver still owns the remaining
theory, combination, and result-construction obligations.

- **Dedicated incremental EUF:** the driver reuses the callback closure when
  present; other routes run their final theory checker. A conflict yields a
  blocking theory clause and another solve; absence of conflict permits the
  single-theory SAT result, with typed model construction when requested.
- **Dedicated LRA:** {source}`cb_check_found_model
  <src/smt/solver_engines.cpp#L7058-L7077>` validates observed-atom completeness, finishes
  any deferred tableau check, and rejects a conflict. Eager registration
  normally finishes this repair earlier through the external-clause callback.
- **General component driver (QF_LIA, QF_UFLIA and quantified UF):** the model callback's
  `Partial` check is not the final combination decision. The outer loop first handles
  {source}`pending lemmas and typed failures
  <src/smt/component_solver.cpp#L305-L349>`. With the propagator disconnected,
  it clausifies new lemmas and registers their atoms before another solve. The
  {source}`coordinator <src/smt/component_model_coordinator.cpp#L140-L208>`
  then requests `FullModel` checks. The
  {migrationsource}`standalone LIA recipe <src/smt/solver_engines.cpp#L10049-L10076>`
  uses one arithmetic component: its integer completion must succeed, with no
  multi-theory model reconciliation. For combined recipes, the coordinator
  checks the chosen equalities and
  disequalities between shared terms (the *arrangement*) or reconciles the
  components' models on those terms, according to the combination policy.
  Each theory being consistent on its own is insufficient for combination. Integer search adds
  integrality and resource-limit obligations; quantified UF adds
  [instantiation and completion obligations](quantifiers.md#supported-scope-and-answers).
  Neither is explained by the ground-EUF example below.
- **Dedicated Real combination:** {source}`QF_UFLRA <src/smt/solver_engines.cpp#L10801-L10827>`
  retains its own driver for equality exchange, model reconciliation, and exact
  fallback checks.

Reversible model-equality choices are resubmitted as assumptions. An UNSAT
answer under those choices can reject only that branch:
{source}`the component loop <src/smt/component_solver.cpp#L350-L367>` learns
a clause over failed choices and retries, rather than immediately declaring
the input UNSAT. The SAT chapter explains ordinary assertion-scope selectors.

Cancellation, unsupported capabilities, unknown results, and resource exhaustion
must retain their typed outcomes. In particular, a component failure is not
an empty conflict clause. {source}`SolveTerminator
<src/smt/solver_runtime.cpp#L12-L27>` handles cancellation and its configured
theory-conflict budget; the [statistics lead](../leads/statistics-instrumentation.md)
records why this accounting cannot simply be disabled.

For requested models, theory facts and numeric values feed model construction
and reverse conversion. The {source}`session's finalization
<src/smt/solver_session.cpp#L655-L682>` checks original assertions in debug
builds or with `--check-model`, then prints the typed interpretation. This
check is conditional, and does not replace the solving obligations above or
independent validation. A model-construction failure is an internal error,
not evidence of theory UNSAT.

### Protected model scopes

The optional mixed online-EUF route borrows its live closure for tentative
model merges. During that scope the coordinator freezes the shared view and
queues all assignment, level, and rollback events. It replays them together
for every consumer after the scope ends. Ordinary callbacks cannot run while
protected; stored reasons may be streamed, but EUF cannot reconstruct a missing
reason from temporary merges. Before accepting a model, any queued notifications
force release, replay, and another SAT/theory round. The
{source}`coordinator tests <tests/unit/assignment_coordinator_tests.cpp>` cover
this scheduling protocol directly; mixed solver regressions cover model merging
and reconstructed-model validity.

## Worked example: equality rejects a Boolean branch

The following {download}`input <../examples/dpllt-equality.smt2>` has three answers:
`sat`, `unsat`, `sat`.

```{literalinclude} ../examples/dpllt-equality.smt2
:language: lisp
```

Write `A` for `a = b`, `B` for `b = c`, and `C` for `f(a) = f(c)`.
The assertion clauses are:

```text
¬p ∨ A
¬p ∨ B
¬p ∨ ¬C
```

Consider a branch choosing `p=true`. Boolean propagation gives `A`, `B`, and
`¬C`. EUF merges `a` with `b` and `b` with `c`; congruence consequently forces
`f(a) = f(c)`, contradicting `¬C`. Its explanation is the theory-valid clause
`¬A ∨ ¬B ∨ C`. In the dedicated online route, assignment notifications feed
the closure, {source}`maybe_store_conflict
<src/smt/solver_engines.cpp#L2487-L2498>` queues the explanation, and the
external-clause callbacks stream it to CaDiCaL.

One possible conflict-analysis derivation is:

```text
¬A ∨ ¬B ∨ C     resolve with ¬p ∨ ¬C on C  →  ¬p ∨ ¬A ∨ ¬B
¬p ∨ ¬A ∨ ¬B   resolve with ¬p ∨ B  on B  →  ¬p ∨ ¬A
¬p ∨ ¬A        resolve with ¬p ∨ A  on A  →  ¬p
```

The theory explanation and the learned clause `¬p` differ. The first expresses
an equality law; the second also depends on the input clauses. If `A` and `B`
arrive while `C` is unassigned, the same theory lemma can instead explain
propagating `C`. CaDiCaL can then encounter a Boolean conflict. These are two
schedules for the same logical obstruction.

With `p=false`, all three assertions hold, so the initial query is SAT.
Asserting `p` in the new scope makes it UNSAT; popping that assertion restores
SAT. CaDiCaL may choose a different branch or learned clause, and preprocessing
may discover the contradiction before search. The
{ref}`review record <dpllt-chapter-checks>` includes checks of this example
under lazy, online, conflict-only, and retained-session configurations.

## Sources and implementation status

- **Nieuwenhuis, Oliveras, and Tinelli, *Solving SAT and SAT Modulo Theories:
  From an Abstract Davis–Putnam–Logemann–Loveland Procedure to DPLL(T)*,
  JACM 53(6), 937–977 (2006).**
  [Author text](https://homepage.cs.uiowa.edu/~tinelli/papers/NieOT-JACM-06.pdf),
  §§4.1, 4.4, 5.1–5.2. Basis: trail notifications/undo, theory-valid conflicts,
  optional non-exhaustive propagation, and explanations suitable for conflict
  analysis. These mechanisms are implemented through the owners above.
  Primo does not implement the paper's full abstract calculus or inherit its
  termination theorem merely by using callbacks. Explanation minimization and
  exhaustive propagation are not promised; the cost of each theory algorithm
  needs its own analysis.
- **Fazekas, Niemetz, Preiner, Kirchweger, Szeider, and Biere,
  *IPASIR-UP: User Propagators for CDCL*, SAT 2023, §§2.1–2.4.**
  [Primary text and publication record](https://doi.org/10.4230/LIPIcs.SAT.2023.8).
  Basis: observation, trail notifications, delayed reason requests, model
  checks, and streamed external clauses. Primo implements this subset through
  its adapter, with the API differences listed in the [SAT chapter](sat.md).
  Proof-producing integration is not implemented. The installed dependency's
  clause-retention flags go beyond the paper's basic interface description.
- **Original Primo design:** {source}`component combination contract
  <docs/n-way-combination-migration.md>` and
  {source}`retained UF session design <docs/assumption-incremental-uf.md>`.
  These specify ownership, atom registration between SAT connections,
  handling of component results, and session reuse. SAT backtracking,
  a callback's incremental theory state,
  and SMT-LIB `push`/`pop` are distinct lifetimes. The default session rebuilds;
  opt-in assumptions engines retain clauses and selectors across queries.

| Obligation | Existing verification and limitation |
| --- | --- |
| A theory conflict reaches SAT even without optional propagation | {source}`EUF external-conflict tests <tests/unit/boolean_solver_tests.cpp#L2589-L2635>`. |
| Explained propagation occurs during search | {source}`online congruence test <tests/unit/boolean_solver_tests.cpp#L2480-L2500>` checks delivered propagation; broader explanation algorithms belong in the EUF chapter. |
| Rollback preserves surviving reasons and assignment routing | {source}`component lifecycle regression <tests/unit/component_combination_tests.cpp#L527-L589>` and {source}`backend reconnect regressions <tests/unit/sat_backend_tests.cpp#L194-L235>`. |
| Assignment consumers see one consistent trail across callbacks and reconnections | {source}`Coordinator protocol tests <tests/unit/assignment_coordinator_tests.cpp>` cover root and retained levels, empty levels, duplicate/invalid events, registration growth, reentrant delivery, protected replay and reconnect. |
| A candidate with pending work or a typed failure is not accepted as SMT SAT | Component-loop checks above and {source}`component regression suite <tests/unit/component_combination_tests.cpp>`; the [combination](combination.md) and [quantifier](quantifiers.md) chapters explain their additional obligations. |
| Scoped assertions change satisfiability without corrupting earlier state | The worked example, {source}`session regressions <tests/unit/smt_session_tests.cpp>`, and the distinct session ownership described above. |

Changes to callbacks, explanations, scheduling, or final checks require the
[correctness gates](../development/correctness.md).
The [assignment migration evidence](../evidence/assignment.md) records the dated
tests, comparisons and model-validation limitations of that ownership change.
