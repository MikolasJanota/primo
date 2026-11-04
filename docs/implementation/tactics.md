# Goals, tactics, and strategies

A solver query passes through transformations before it reaches an engine.
Primo represents that work as **tactics**, which transform goals. A **goal**
is an ordered collection of assertions that must all hold, together with
information needed to reconstruct a model after transformation. Composing
tactics in sequence, repeating them, or choosing between them produces another
tactic. This chapter follows the default solving path for formulas without
quantifiers.

Preprocessing uses these operations to combine its passes into a pipeline.
Solver calls also follow the goal contract, so a transformed goal carries its
model-reconstruction history from preprocessing into solving. The
[preprocessing chapter](preprocessing.md#how-tactics-execute-preprocessing)
explains that connection and the order of the passes. Construction of tactic
compositions is internal C++ functionality; Primo has no user-facing tactic
language. The internal `Strategy` representation is explained under
[strategy identity and compilation](#strategy-identity-and-compilation).

The basis is de Moura and Passmore's *The Strategy Challenge in SMT Solving*,
§3 {cite:p}`deMoura2013`: assertions within a goal are conjunctive, subgoals
are alternatives, and transformations compose with model conversions. Primo
implements a subset of that framework, with the differences described below.

(tactics-representation)=
## Representation

| Type and code | Meaning and ownership |
| --- | --- |
| {source}`Goal <src/smt/tactic.hpp#L29-L57>` | Ordered assertion IDs, model-reconstruction records, an optional immutable typed model, and an `InconclusiveReason` value with diagnostic text. Updates return a new goal and share the data that has not changed. |
| {source}`AnyOf <src/smt/tactic.hpp#L59-L76>` | A nonempty, shared immutable sequence of alternative goals. Its constructor rejects an empty sequence. |
| {source}`Tactic <src/smt/tactic.hpp>` | A named executable function from a context and `AnyOf` to another `AnyOf`, with an incremental-safety classification. Copies share the implementation. |
| {source}`TacticContext <src/smt/tactic.hpp#L85-L141>` | Borrows the term manager and statistics used by this execution; supplies cancellation, deadlines, model requirements, and a function for creating fresh constants. |
| {source}`Strategy <src/smt/strategy.hpp>` | Immutable graph of tactic applications and composition operations. A leaf applies one tactic and stores its identifier and parameters. Other nodes describe sequencing, branching, repetition, fallback, time limits, or skip. |

A `Goal` stores term IDs; the term manager owns the terms themselves. Goals
and saved lists of term IDs must therefore be used with the manager in which
they were created. Tactics create replacement terms through that manager.
They may allocate terms and update statistics while leaving the input goal
unchanged.

Preprocessing tactics read the term manager, statistics, and callbacks from
the executing `TacticContext`; they store their options by value. They do not
retain a reference to the context used to construct the strategy. A prepared
finite-domain strategy can also store assertion and clause IDs, which remain
tied to their original term manager.
{source}`make_session_solver_strategy <src/smt/solver_leaves.cpp>` builds
tactics that borrow the frontend, statistics, and context of one solve.
The resulting strategy can be used only while those objects are alive.

(tactics-goal-truth)=
## Conjunction within a goal, alternatives between goals

For assertions $A_1,\ldots,A_n$, a goal denotes
$A_1\land\cdots\land A_n$. An `AnyOf` containing $G_1,\ldots,G_k$ denotes
their disjunction. Proving one alternative satisfiable settles the whole
disjunction; proving it unsatisfiable requires eliminating every alternative.

Each transformation must preserve satisfiability: its input has a model
exactly when at least one output goal does. If it removes or introduces
symbols, it must also provide a way to reconstruct an input model from a
model of the transformed goal.

The {source}`truth function <src/smt/tactic.cpp#L190-L241>` recognizes the
following cases directly from the assertion list:

- an empty assertion sequence is true;
- a goal containing an explicit `false` assertion is false;
- every other goal is inconclusive;
- an `AnyOf` is true if any goal is true, and false if all goals are false.

The true and false cases are called **terminal**: no further solving is
needed. This function does not call an engine. In particular, a list containing
only `true` is classified as inconclusive until a transformation removes that
assertion. Encountering a non-Boolean assertion raises a contract error.

The {source}`solver tactic adapter <src/smt/solver_tactic.cpp>` turns engine
answers into this representation. SAT clears the successful goal's assertions
and attaches its typed model. UNSAT removes that alternative; if none remain,
the adapter returns a single false goal. Unknown or unsupported results retain
the goal with an `InconclusiveReason` value. Fallback decisions use this value
rather than diagnostic text.

(tactics-composition)=
## Composition and stopping

The ordinary runtime combinators are implemented in
{source}`tactic.cpp <src/smt/tactic.cpp#L273-L383>`; strategy compilation
constructs them from the graph.

| Operation | Execution rule |
| --- | --- |
| `skip` | Return the input unchanged. |
| `sequence` | Feed each result into the next child; stop when the combined result is syntactically terminal. |
| `conditional` | Evaluate the [predicate](#conditional-predicates-and-probes) on each current inconclusive goal, then run the selected child on it. |
| `repeat` | Apply the child until it returns a terminal result, reaches a fixed point, fails the condition for another iteration, or reaches the iteration limit. Zero iterations returns the input. |
| `try_in_order` | Try each child strategy on the **original** input. Return the first terminal result; if none is terminal, return the last result. Cancellation stops the attempts. |
| {source}`try_for <src/smt/strategy.cpp#L329-L346>` | Run the child with the requested deadline, or an earlier enclosing deadline. If time expires and the result is still inconclusive, set its reason to `ResourceLimit`. |

For example, `try_in_order(A, B)` runs `B` if `A` is inconclusive and execution
has not been cancelled, giving `B` the original formula. The sequence `A; B`
instead passes `A`'s transformed formula and model conversions to `B`.

`make_goal_tactic` applies a tactic to each inconclusive goal and collects all
the resulting alternatives into one `AnyOf`. It preserves terminal goals.
Some preprocessing groups use the internal
{source}`complete_sequence <src/smt/preprocessing_tactics.cpp#L228-L237>` helper: every
phase of the group runs, including bookkeeping phases, before the enclosing
strategy can stop. It is not interchangeable with the public short-circuiting sequence.

Repetition normally compares complete goal values. Shared storage lets it
recognize identical values without comparing their contents. The preprocessing
loop compares only assertion vectors, so changes to reconstruction records
alone do not trigger another round. On a fixed point,
`repeat` returns the **new** result, retaining conversions added by that last
iteration. Replacing it with the previous result could lose a reconstruction
step even though both assertion vectors match.

Comparing assertion vectors requires no traversal of the term graph. The
preprocessing rule for extra iterations separately counts reachable terms.
The iteration limit bounds the number of calls; it gives no bound on the work
or allocation within one tactic.

Timeouts are cooperative. `Tactic::operator()` checks cancellation and the
deadline before invocation; a long-running child must also poll its context.
`try_for` creates no worker process and cannot forcibly interrupt a child.
A terminal answer returned after the deadline is retained. Contract errors
and backend exceptions propagate to the caller without trying another strategy.

Immutable goals keep each fallback attempt's formula and reconstruction
changes separate. They do not roll back term allocations, fresh-symbol
creation, or statistics.
The scheduler is sequential; no parallel tactic execution is implemented.

(tactics-strategy-identity)=
## Strategy identity and compilation

This section concerns how Primo represents tactic compositions internally.
`Strategy` is an abstract syntax tree (with shared nodes) for an internal
language of tactic applications. `Tactic` is the executable form. Both can
represent a single transformation or a composition; composition itself does
not distinguish the two types.

For example, a pipeline that first simplifies and then substitutes can be
built directly as a composed `Tactic`. Alternatively, a `Strategy` can store a
`Sequence` node with two `Leaf` children for those operations. Calling
`compile` on that description constructs the composed `Tactic`; invoking the
result executes the operations in the described order. Compilation does not
make an additional scheduling choice.

The explicit nodes let C++ code inspect the composition, print it, and compute
a fingerprint. Leaf nodes retain executable tactics as well as their names
and parameters, so this is not a standalone text language that users can
submit to Primo. This representation is useful when working on the pipeline's
implementation; it is not needed to select a preprocessing profile or
understand the order of its passes.

{source}`Strategy::canonical <src/smt/strategy.cpp#L230-L294>` serializes
composition, leaf identifiers and parameters, named probes and repeat policies,
and time budgets. Parameters have a canonical ordering. `fingerprint()` hashes
this string with FNV-1a; strategy equality compares the canonical strings.

This describes the supplied labels and rendered parameters, not the executable
behavior or captured values of a callable. Callers must include relevant
configuration in their probe names and repeat-policy identifiers.

The built-in {repairsource}`at_most <src/smt/strategy.cpp>` renders finite thresholds with the classic locale and
`max_digits10` precision, so distinct finite `double` values have distinct
names, except signed zeros, both rendered as `0`. Non-finite names are `inf`,
`-inf`, and `nan`; the callable still uses ordinary `<=` comparison (including
false comparisons with NaN). Fingerprint formatting also uses the classic
locale. This repairs configuration identity; affected strategy descriptions
and fingerprints change. Historical experiment records retain their original
identities and require their recorded solver revision for interpretation.
Neither equal descriptions nor equal fingerprints establish behavioral
equivalence. Reproducing an experiment also requires the solver revision,
options, inputs, and resource limits.

{source}`compile <src/smt/strategy.cpp#L296-L378>` walks the composition graph
in postorder and builds runtime tactics. Both compilation and canonicalization
use explicit traversal stacks, so a deeply nested strategy does not require
an equally deep C++ call stack during those operations. Execution still follows
the composed functions, whose stack use depends on their implementations.

## Conditional predicates and probes

A **probe** measures a property of the current goal or tests a condition on it.
The executable `conditional` takes a
{source}`GoalPredicate <src/smt/tactic.hpp#L160-L175>`: a C++ callable returning
`bool` from `const TacticContext&` and `const Goal&`. A caller can supply its
own condition. The immutable strategy layer instead takes a named
{source}`BooleanProbe <src/smt/strategy.hpp#L36-L63>`; `NumericProbe` returns
`double`. Probe names appear in the canonical strategy description.

Only two built-in probe builders are currently exported:

| Builder | Meaning |
| --- | --- |
| {source}`assertion_count() <src/smt/strategy.cpp#L100-L104>` | Number of entries in the current goal's assertion vector, returned as `double`. It does not count conjuncts or deduplicate roots. |
| {repairsource}`at_most(probe, limit) <src/smt/strategy.cpp>` | Boolean test that the numeric probe's result is at most the supplied limit. |

For example, `Strategy::conditional(at_most(assertion_count(), 1), A, B)`
runs `A` on a goal with at most one assertion root and `B` otherwise.
The goals `[p, q]` and `[p and q]` therefore select different branches, despite
being logically equivalent. The probe sees each current inconclusive goal
**after preceding transformations**; terminal goals bypass it. The
{source}`existing regression <tests/unit/tactic_tests.cpp#L359-L384>` checks
that changing one root to two changes the selected branch.

Production selection uses richer analyses directly:
[preprocessing profiles](preprocessing.md#profiles-and-repetition) inspect
formula size, conjunct structure, and finite domains;
{source}`solver dispatch <src/smt/solver_strategy.cpp#L50-L80>` inspects theory
features, declared logic, and model requirements. Adaptive repetition compares
reachable-term counts before and after a pass. These mechanisms do not use
`Strategy::conditional` or export their measurements as named probes.
Reusable structural probes and their composition are an
[open lead](../leads/index.md#reusable-tactic-probes).

(tactics-production)=
## From a session query to an engine

The default rebuild path in
{source}`solver_session.cpp <src/smt/solver_session.cpp#L624-L799>` first
routes quantified assertions to the dedicated quantified-UF implementation.
For ground assertions it prepares and executes the
[preprocessing strategy](preprocessing.md). The resulting alternatives retain
their own reconstruction records throughout the subsequent solver call.

The session drops false alternatives, handles cancellation and other
inconclusive results, and can return SAT immediately for an empty surviving
goal. If a model was requested, even that immediate answer must reconstruct
an interpretation.
For statistics and heuristics based on formula structure, the session collects
assertions from all surviving goals into one vector. That vector is not solved
as a conjunction: the solver receives the original disjunctive `AnyOf`.

{source}`make_solver_dispatch <src/smt/solver_strategy.cpp>` examines each
goal's declared logic, current formula features, and model requirements in
this priority order:

| Condition | Route |
| --- | --- |
| Declared `QF_LIA` or `QF_UFLIA` | Corresponding integer engine. |
| Arithmetic combined with equality/UF features | Mixed Real engine; model requests can also require this route after simplification removes arithmetic atoms. |
| Arithmetic remains | LRA engine. |
| A model needs declared Real symbols, with no equality/UF features | LRA model-construction route. |
| Equality or UF applications remain | EUF engine. |
| Otherwise | Boolean engine, with EUF fallback on an inconclusive Boolean result. |

The test for selecting the mixed engine also accounts for declared `QF_UFLRA` and
Real symbols. An engine cannot be selected solely by counting the remaining
arithmetic atoms: eliminated terms and declared symbols still affect the
requested model. [Theory combination](combination.md) explains which
mixed coordinator each route uses.

When converting the tactic result to a `CheckSatOutcome`,
{source}`outcome_from_tactic_result <src/smt/solver_tactic.cpp#L80-L113>` computes
syntactic truth first, but gives a recorded inconclusive reason precedence
when choosing the outcome. An interruption therefore takes precedence over
an empty or false goal. After SAT,
the session can evaluate the original assertions against the reconstructed
model before printing it; see [model construction](models.md).

(tactics-lia-settings)=
### Per-route LIA settings

The two integer routes also carry their own LIA engine settings.
`make_session_solver_strategy` resolves one
{settingssource}`LiaSettings <src/smt/lia_settings.hpp>` value per route with
`resolve_lia_settings`. That function applies the caller's overrides to the
route's default; see {ref}`lia-settings`. The `solve-qf-lia` and
`solve-qf-uflia` tactics each capture their value.

`SolverStrategyOptions` renders both values into the solver-dispatch leaf's
parameters. Each key is the route prefix followed by the hyphenated setting
name, for example `qf-lia.primitive-int-rows` and
`qf-uflia.primitive-int-rows`. Since `5dec036` the two default sets differ:
`qf-lia.primitive-int-rows` is `true` and `qf-uflia.primitive-int-rows`
is `false`, and likewise for the other three integrality switches. Both
sets are rendered in every session,
whatever the declared logic, so a `--lia-*` flag that changes a setting also
changes the fingerprint of a QF_LRA query. Two option sets that run different LIA settings have different
canonical forms and fingerprints. The change also gives every solver-dispatch
canonical string new keys, so it differs from the one produced by revisions
before `5aaca1f`.

The other engine options are still shared by every route through one
immutable `CheckSatOptions` copy. Of those options, only `mixed_dispatch` is a
leaf parameter. The leaf also records the declared logic and whether Real
constants are declared, both taken from the frontend.

(tactics-model-example)=
## A transformation and its model

Consider the integer goal

$$G=[x=y+2,\quad y\ge1].$$

A substitution step eliminating $x$ returns the reduced assertions and records
$x\mapsto y+2$ in that goal's conversion chain. The defining equality becomes
`true`, leaving $y\ge1$ as the only nontrivial obligation. A solver
model with $y=1$ extends to $x=3$. Model reconstruction supplies this value for
the eliminated symbol $x$.

In the integer preprocessing profile, this step is performed by the
{source}`qf-lia-solve-eqs tactic <src/smt/preprocessing_strategy.cpp#L87-L113>`.
It calls `substitute_solved_constants` with Gaussian elimination disabled and,
when a model is requested, appends a `LiaSubstitutionConversion` to the returned
goal. This makes the division of work concrete: the preprocessing algorithm
finds and applies $x\mapsto y+2$; the tactic preserves the information needed
to recover $x$; the preprocessing pipeline places it before the optional
simplification round. This integer
preparation runs even when the main simplification loop is off. Later passes
may transform the remaining obligation further before a solver sees it.

For a chain of transformations $G_0\to G_1\to G_2$, a model of $G_2$ must
first be converted to a model of $G_1$, then to a model of $G_0$.
{source}`ModelConversionChain <src/smt/model_reconstruction.hpp>` stores typed
steps in execution order and replays them backwards. Its immutable storage
lets split goals share their common history and append different steps.
The [model chapter](models.md#following-a-model-back-through-preprocessing)
explains dependencies, cache invalidation during replay, and the conversion types.

(tactics-experiments)=
## Splitting and session portfolios

The default strategy produces one goal. The optional
{source}`Boolean splitter <src/smt/experimental_tactics.cpp>` appends all
truth assignments to selected Boolean atoms. If it selects $m\le k$ atoms,
it produces $2^m$ alternatives. For $G=[p\lor q]$ and selected atom $p$, these are
$[p\lor q,\neg p]$ and $[p\lor q,p]$. Either satisfying model solves $G$.
Proving only the first alternative unsatisfiable would not settle $G$.

Selection follows a deterministic term traversal, preferring Boolean symbols
and predicate applications before equality/comparison atoms. The option
`--experimental-split-bools` defaults to zero and rejects values above 16.
The implementation stores every alternative before solving, so the limit
caps the exponential growth in storage. Alternatives are
solved sequentially, retaining the first SAT model.

The separate {source}`QF_UF session portfolio <src/smt/session_portfolio.cpp>`
tries configurations in separate session executions. The first attempt uses
basic preprocessing, SAT truth-value preferences based on atom polarity, and
a theory-conflict budget. Only an `Unknown` answer with
typed reason `ResourceLimit` triggers the fallback with the requested options.
Ordinary unknown, cancellation, unsupported results, and exceptions do not
trigger that retry. Unlike `try_in_order`, each attempt rebuilds the session
instead of reusing the same term manager and goals.

(tactics-incremental)=
## Scope and incremental use

An eliminated assertion cannot generally be forgotten when a later user
assertion arrives. In the example above, adding $x=0$ to the reduced formula
$y\ge1$ without restoring or translating the definition would miss the
contradiction. Model conversion reconstructs a completed model; it does not
rewrite new assertions using the eliminated definitions.

The default session avoids this problem by rebuilding from the active original
assertions on each query. Assumptions-based engines use a smaller audited
set of tactics. `IncrementalSafety` distinguishes rewrites valid independently
of other assertions, conservative extensions that add fresh definitions, and
tactics whose incremental use has not been checked. A context requiring
incremental safety rejects this last category. For an extension such as
extended-term lowering, the new constraints must be activated and deactivated
with the assertion that introduced them. General composition, solve-eqs, and
monotone elimination remain unclassified.

The requirements for these engines are documented in the
{source}`LRA assumptions design <docs/assumption-incremental-lra.md>` and
{source}`UF assumptions design <docs/assumption-incremental-uf.md>`.
Primo currently has no user-facing tactic language, proof/unsat-core conversion
framework, or general mechanism for restoring eliminated assertions during
incremental solving.

(tactics-evidence)=
## Checks and implementation status

| Contract | Existing check |
| --- | --- |
| Disjunctive truth, immutable updates, reverse conversion, fallback input, fixed-point output, cancellation, deadlines, and deep strategy construction | {source}`tactic tests <tests/unit/tactic_tests.cpp>` |
| Tactics use the executing context and survive destruction of the construction context where their stored data permits it | {source}`strategy lifetime tests <tests/unit/strategy_lifetime_tests.cpp>` |
| Supported combinations of passes, integer restrictions, and calls to solver engines | {source}`composition tests <tests/unit/tactic_composition_tests.cpp>` |
| Public options, repeated-run statistics, reference answers, direct model validation, and whether the expected tactics ran | {source}`composition matrix workflow <scripts/tactic_composition_matrix.py>` and {source}`profile manifest <benchmarks/tactic-composition-matrix.json>` |

The matrix's {source}`SLURM gate <scripts/slurm_tactic_composition_matrix.sbatch>`
exercises generated formulas under applicable profiles. It checks composition
correctness and whether the expected tactics ran. It is not a performance
comparison. These existing checks were not rerun for this documentation review.
The [preservation map](../coverage/tactics-preprocessing.md) locates earlier
verification and experiment records.

The implemented paper subset is ordered goals, disjunctive subgoals, model
conversion, probes, and sequential composition {cite:p}`deMoura2013`.
Primo represents results through the assertion list and `InconclusiveReason`,
rather than the paper's separate `sat`, `unsat`, `unknown`, and `fail` cases.
Its ordered fallback retries inconclusive strategies, and its timeout is
cooperative. Proof converters, support for over- and under-approximating
transformations, parallel composition, and the paper's full tactic catalogue
are outside the implementation. Profile selection and the
pass schedule are original Primo policies documented in the next chapter.
