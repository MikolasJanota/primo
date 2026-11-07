# The theory solver interface

A theory solver checks the facts assigned to it by Boolean search. It can
report a conflict, explain a consequence, or contribute a model. The SMT layer
owns scheduling, clausification, SAT calls, and agreement between theories;
each theory solver owns its prepared data and reasoning state.

Primo's shared interface is
{source}`TheoryComponent <src/theory/theory_component.hpp#L90-L140>`.
Here, **theory solver** is an architectural term that includes quantifier
reasoning. Quantifiers are logical operators rather than a separate background
theory, but their implementation receives assignments and returns lemmas
through the same interface. This classification does not imply a complete
decision procedure: the [quantifier solver](quantifiers.md) can return
`unknown`. “Component” in the C++ names means a solver registered with the
shared SMT driver; it introduces no additional mathematical notion.

The [DPLL(T) chapter](dpllt.md) explains Boolean search and its callbacks.
This chapter describes what a theory implementation must provide. The
[combination chapter](combination.md) explains how several such solvers agree
on shared equalities.

## Theories and their shared contract

The **Theories** section starts with this interface, then follows each solver's
reasoning and finally the coordination between solvers. Read the individual
chapters as implementations of the same architectural contract: receive the
relevant assigned facts, check consistency, explain conflicts or consequences,
and supply model information where supported. Quantifier reasoning also
returns formulas for the SMT layer to clausify.

| Chapter | Theory-specific responsibility | Connection to the shared contract |
| --- | --- | --- |
| [Equality with uninterpreted functions (EUF)](euf.md) | Maintain congruence and detect contradictory equalities and disequalities. | Its shared-driver adapters implement `TheoryComponent`; the congruence-closure data structure remains behind that boundary. |
| [Linear real arithmetic (LRA)](lra.md) | Check exact linear constraints and explain infeasible bounds. | Dedicated arithmetic checks and propagation callbacks implement the assignment/explanation contract; the Real routes have not all migrated to `TheoryComponent`. |
| [Linear integer arithmetic (LIA)](lia.md) | Extend the real relaxation with integer feasibility checks. | The shared integer-combination route implements `TheoryComponent`; bounded integer search may report `unknown`. |
| [Quantifier reasoning](quantifiers.md) | Generate guarded instances and other supported quantified lemmas. | `QuantifierComponent` uses the same lifecycle and lemma queue; quantifiers are not a separate background theory. |

[Theory combination](combination.md) explains how the SMT layer coordinates
these solvers. It is coordination machinery, not another background theory.
The interface names and production routes below make the distinction between
the common contract and its current C++ implementations explicit.

## Which interface is used?

Several interfaces coexist; they are not interchangeable names for one class.

| Interface | Current use |
| --- | --- |
| {source}`TheoryComponent <src/theory/theory_component.hpp#L90-L140>` | The shared SMT driver uses it for EUF, integer arithmetic, and quantified UF. It covers registration, assignment changes, checking, and optional lemma/model operations. |
| {source}`TheorySolver <src/theory/theory_solver.hpp#L150-L166>` | An older interface receives an assignment and arrangement together at each check. The EUF/LIA adapters behind the shared integer routes still use it. In QF_UFLRA, it serves an opt-in experimental generic-coordinator branch. |
| {source}`IncrementalTheorySolver <src/theory/theory_solver.hpp#L184-L194>` | A separate lifecycle interface exercised by a {source}`test implementation <tests/unit/theory_combination_tests.cpp#L125-L190>`. Production solvers implement `TheoryComponent` directly. |

The {source}`experimental Real branch <src/smt/solver_engines.cpp#L11719-L11750>`
is selected by `experimental_generic_model_combination` and wraps already
checked projections in `TheorySolver` objects.

The driver is
{source}`solve_component_recipe <src/smt/component_solver.hpp#L19-L45>`.
Its `CombinationRecipe` is the configuration that constructs the theory
solvers, selects their checking order, and supplies preparation hooks. The
{migrationsource}`combined integer recipe <src/smt/solver_engines.cpp#L10079-L10121>` supplies
EUF and LIA. The {migrationsource}`standalone QF_LIA recipe
<src/smt/solver_engines.cpp#L10043-L10077>` supplies only LIA, enables its
online rational checks, and disables model-guided combination; the
{source}`quantified-UF recipe <src/smt/quantifiers/quantified_solver.cpp#L134-L151>`
supplies EUF and quantifier reasoning. These are internal interfaces, not yet
the [planned public library API](../leads/library-api.md).

## Representation and ownership

The {source}`driver <src/smt/component_solver.cpp#L70-L115>` owns the theory
objects through `unique_ptr`s for one solve. Its online dispatcher borrows
those objects. Theory state may contain congruence classes, a tableau, or
instantiation caches, but term nodes remain owned by `core::TermManager`.
`TermId`s identify those nodes within that manager; storing an ID does not
transfer ownership or keep the manager alive.

| Boundary data | Meaning |
| --- | --- |
| {source}`ComponentDescriptor <src/theory/theory_component.hpp#L16-L53>` | A solver's identity, supported fragment, signature ownership, shared sorts, convexity, and optional capabilities. These are declarations about the implementation. |
| {source}`ComponentAtom <src/theory/theory_component.hpp#L55-L66>` | A registered Boolean term, its naming SAT literal, and occurrence/relevance information. |
| {source}`AssignedTheoryLiteral <src/theory/theory_solver.hpp#L38-L46>` | An atom, its naming literal `lit`, and an assigned Boolean `value`. The true signed fact is `lit` when `value` is true and `-lit` otherwise. |
| {source}`InterfaceRelation <src/theory/theory_solver.hpp#L48-L62>` | Equality or disequality between shared terms of the same sort, with its SAT naming literal when one has been introduced. A collection of such choices is an arrangement. |
| {ufliaeqsource}`TheoryCheckResult <src/theory/theory_solver.hpp>` | A tagged result containing the applicable clause, explanation, shared-equality batch, decision request, model projection, or diagnostic. |

The SAT backend sees literals and clauses, not these term-bearing records.
The theory interface receives no backend handle or SMT-LIB syntax tree.
Every newly constructed term goes through the term manager.

Assignment truth is borrowed through `sat::TrailView`.
{source}`ComponentAssignmentView <src/theory/component_assignment.hpp#L18-L70>`
stores registration metadata and reads that view; it does not maintain a second
assignment stack. It can materialize a temporary vector for a check that needs
a complete assignment. SAT auxiliary variables used only for Boolean structure
are not routed to theory solvers. The dispatcher routes registered theory atoms.
The {migrationsource}`driver <src/smt/component_solver.cpp#L89-L109>` also owns a Boolean constant when no component claims it. Such
constants are excluded from the theory combination plan and component
registration. For online recipes, the {retentionsource}`driver observes their SAT variables
<src/smt/component_solver.cpp#L276-L291>` so their values are present in the shared
assignment view, while the dispatcher skips them during component routing.
Their final SAT values are preserved as model facts. A
Boolean constant claimed by EUF remains with EUF, including Boolean function
arguments. Predicates and other genuine theory atoms still require an owner.
The same rule applies when new atoms arrive at the detached growth boundary.

Input spans and the trail view are borrowed. Implementations must not retain
an input span after its call; they copy any registration data they need.
Returned vectors own their elements, while term and sort IDs still refer to
manager-owned objects. The [term chapter](core.md) explains ID stability and
when references to nodes can be invalidated.

## Registration and preparation

Before search, the SMT layer determines who owns each atom and which terms
must be shared. The
{source}`registration operations <src/theory/theory_component.hpp#L102-L113>`
divide responsibility as follows:

| Operation | Obligation |
| --- | --- |
| `descriptor()` | Declare the supported fragment and capabilities accurately. Advertising model export, for example, commits the solver to providing it when requested. |
| `owns_atom(atom)` | Identify atoms understood by this solver. The combination planner requires exactly one owner for each registered atom. |
| `interface_variables(atom)` | Report purified free constants in the atom that may need to be shared with another solver. The planner rejects other term kinds. |
| `register_interface(scopes)` | Prepare the normalized shared terms for each sort, including unconstrained terms missing from the solver's original atoms. |
| `register_atoms(atoms)` | Register prepared atoms and their SAT names. Later registration can extend the problem between SAT calls. |
| `encode_equality(pair)` | Return a Boolean term expressing equality of the two shared terms in this theory. The SMT layer clausifies it. |

For example, EUF encodes equality directly; LIA uses the conjunction of the
two inequalities. The
[combination example](combination.md#materializing-an-interface-equality)
shows the linking clauses. Quantifier reasoning declares no shared equality
terms, so its equality-encoding operation is never applicable.

Registration happens while SAT callbacks are detached, never by changing a
live tableau during a callback. The
{source}`installation path <src/smt/component_solver.cpp#L215-L271>` prepares
new atoms, registers them with their owners, rebuilds dispatch, and observes
their SAT variables before resuming search. Shared-sort declarations must meet
the [combination conditions](combination.md#the-combination-conditions);
implementing the C++ interface alone does not establish those conditions.

Shared scopes are
{source}`planned once from the initial atoms <src/smt/component_solver.cpp#L87-L102>`.
Later atom registration updates assignment routing but does not recompute
those scopes. A recipe that introduces newly shared constants would need an
explicit replanning mechanism; atom registration alone does not provide one.

## Assignment, checks, and rollback

The online driver connects the solvers to the shared assignment view. During
search, the
{source}`dispatcher <src/smt/component_dispatcher.cpp#L64-L175>` performs
these operations as search proceeds:

- `assign(delta)` receives newly assigned owned atoms, after their values are
   present in the shared trail. Each solver updates its reasoning state from
   those facts.
- `push_level()` announces a new SAT decision level. Theory solvers record
   the boundaries needed to undo their own changes.
- `check(Partial, epoch, {})` checks solvers whose assignments changed.
   A partial check may find a conflict or consequence; `Consistent` at this
   effort does not authorize a SAT answer.
- `backtrack(level, removed)` undoes theory changes associated with removed
   assignments. The shared trail already contains the surviving assignment.

Once SAT supplies a complete candidate, the model coordinator calls
`check(FullModel, epoch, arrangement)`. Only this final check can establish
the solver's part of candidate acceptance.

The two check efforts are defined by
{source}`TheoryCheckEffort <src/theory/theory_solver.hpp#L179-L182>`.
`assignment_epoch` identifies the assignment being checked so that later
model operations can reject stale results. The full check must discharge
obligations postponed during search: LIA must resolve integer feasibility,
and quantifier reasoning must justify its restricted SAT completion rule.

A solver advertising `check_on_full_model` is checked at every complete SAT
candidate, even if none of its own atoms changed. The online dispatcher
delays its `Partial` checks until that point. Quantifier instantiation uses
this option because ground search can change the usefulness of instances
without changing any quantified atom. This callback still precedes the
coordinator's separate `FullModel` acceptance check.

The default `backtrack` calls `pop_to_level`. The production EUF/LIA adapters
instead override it to consume the shared removal list; their standalone
`pop_to_level` is rejected because it lacks that information. Theory undo data
records changes to theory state, not another copy of SAT truth. SAT decision
levels are distinct from [SMT-LIB assertion scopes](sessions.md).

The {source}`offline path <src/smt/component_solver.cpp#L384-L406>` obtains a
complete SAT assignment, resets each solver's assignment-dependent state, and
passes its full set of assigned atoms before coordination. `reset()` need not
discard all prepared data: the quantifier solver retains guarded instance,
witness, and enumeration caches across SAT searches within one solve.

## Results and explanations

`TheoryCheckResult::status` determines which payload is meaningful:

| Status | Meaning and driver response |
| --- | --- |
| `Consistent` | No conflict at the requested effort. At `FullModel`, provide the model token and any projection required by the combination policy. |
| `LearnedClause` | Return a theory clause with its final signs. It can express a conflict or a non-convex disjunction, without asserting that one disjunct is entailed. |
| `EntailedLiteral` | Return `entailed_lit` and true antecedents in `reasons`. The driver forms the implication clause. |
| `EntailedEqualities` | At the outer full-model boundary, return `equalities`: copied shared pairs with true antecedents. The driver validates and materializes the batch, learns guarded implications and re-solves. See [QF_UFLIA integration](uflia-equality-propagation.md). This is not an online callback result. |
| `FreshDecision` | Request a choice about a shared equality. It is supported by outer model coordination, not as a new equality introduced inside an online callback. |
| `LemmasPending` | Closed Boolean formulas are queued for the online driver to prepare and clausify between SAT calls. They may introduce atoms. |
| `ResourceLimit`, `Unsupported`, `Cancelled`, `Unknown` | Report an inconclusive check, with a diagnostic. These are never encoded as an empty conflict clause. |

The {source}`online result handler <src/smt/component_dispatcher.cpp#L112-L130>`
accepts clauses, explained literals, and queued lemmas. Other non-consistent
results stop acceptance; an online `FreshDecision` currently becomes
`unknown`. The
{source}`outer coordinator <src/smt/component_model_coordinator.cpp#L96-L134>`
handles fresh equality decisions but does not drain formula queues. The
{ufliaeqsource}`current outer coordinator <src/smt/component_model_coordinator.cpp>`
also validates proved equality batches; the SMT solve loop owns their atom growth. The
{source}`solve loop <src/smt/component_solver.cpp#L310-L350>` requires a lemma
preparation hook for queued formulas; without one it reports unsupported.

Queued formulas become permanent assertions, so being closed and Boolean is
not enough. Ordinary theory lemmas must be logical consequences, with guards
for any assignment-dependent premises. Skolem formulas instead extend the
signature with fresh witnesses while preserving satisfiability. For example,
`exists x. P(x)` does not entail `P(k)` for an arbitrary existing constant;
a fresh `k` can witness it in an extended model. Both kinds of formula must
remain sound when SAT changes its assignments. The
[quantifier chapter](quantifiers.md#universal-instances-and-existential-witnesses)
explains their guards and witness freshness.

For a concrete explanation, let `p`, `q`, and `r` name `a = b`, `b = c`, and
`a = c`. Given true `p` and `q`, EUF can return `r` with reasons `[p, q]`.
The implication clause is

```text
not p or not q or r
```

If `r` is false, that same clause reports a conflict. If SAT backtracks over
`q`, the theory state must no longer use it as a premise for a new propagation.
The clause remains valid: it states an implication, not that `q` is true on
every branch. The
[DPLL(T) explanation contract](dpllt.md#conflicts-propagations-and-explanations)
also explains delayed reason requests and clause lifetimes.

## Optional search and model operations

The remaining
{source}`operations <src/theory/theory_component.hpp#L118-L136>` serve
different purposes:

| Operation | Role |
| --- | --- |
| `propagate()` | Offer a cheap consequence before a more expensive check. An entailed literal needs true antecedents, as above; the default returns no result. |
| `suggest_literal()` | Suggest a registered, unassigned SAT literal as a decision. It is a search choice, so it needs no entailment explanation. |
| `phase_literals()` | Suggest preferred polarities. These preferences do not assert facts. |
| `care_pairs(token)` | Identify shared equality questions worth examining in the current model. A pair supplies neither an answer nor an explanation. The default is empty. |
| `mutate(request)` | Try changing a candidate model to meet requested shared relations. The default reports `FixedPoint`; failure to change a model does not prove the requested relation impossible. |
| `export_model(token)` | Return an owning model fragment for the accepted token when model export is supported. |
| `take_lemmas(limit)` | Transfer a bounded batch of queued Boolean formulas to the driver after `LemmasPending`. The default is empty. |

A {source}`TheoryModelProjection <src/theory/theory_solver.hpp#L63-L85>`
describes which shared terms are equal in a private candidate model. Its
`TheoryModelToken` contains the solver identity, assignment epoch, and model
revision. The coordinator validates the identity and epoch; the solver must
reject operations on an obsolete private model. See
[model reconciliation](combination.md#comparing-complete-model-projections)
for when mutation runs and how unexplained choices remain reversible.

An exported
{source}`ComponentModelFragment <src/theory/theory_component.hpp#L68-L86>`
owns bindings, Boolean facts, and function interpretations. Domain-value
indices are local to that solver until the SMT assembler remaps them.
Numeric and Boolean bindings are authoritative values; EUF contributes
structural facts instead of inventing arithmetic values. The
{source}`export path <src/smt/component_solver.cpp#L451-L467>` validates the
fragments before [model assembly](models.md).

Quantifier reasoning uses the formula queue rather than shared model
partitions. Its [guarded instances and Skolem formulas](quantifiers.md)
are prepared and clausified by SMT, with any new atoms registered before
search resumes. It currently provides no quantified model export.

## Sources, implementation status, and tests

This interface is Primo's own design, recorded in the
{source}`original combination design note <docs/n-way-combination-migration.md>`.
The mathematical requirements for combining theories and their literature
are described in the
[combination source mapping](combination.md#sources-and-implementation-status).
The C++ interface is not itself a standardized SMT API or a completeness
argument.

The implementation is exercised by these existing tests:

| Contract | Tests |
| --- | --- |
| Admission, unique ownership, and shared-scope normalization | {source}`Combination planning <tests/unit/component_combination_tests.cpp#L268-L407>` |
| Assignment routing, rollback, and retained propagation reasons | {source}`Dispatcher lifecycle <tests/unit/component_combination_tests.cpp#L527-L589>` |
| Several theory solvers sharing a SAT loop and model export | {source}`Three- and four-solver checks <tests/unit/component_combination_tests.cpp#L409-L487>` |
| Malformed model projections and stale tokens are rejected | {source}`Projection validation <tests/unit/component_combination_tests.cpp#L489-L525>` |
| Exported model fragments have valid types, tokens, and shared values | {source}`Fragment validation and assembly <tests/unit/model_tests.cpp#L38-L131>` |
| Queued lemmas, preparation hooks, and full-candidate scheduling | {source}`Lemma-growth checks <tests/unit/component_combination_tests.cpp#L824-L937>` |

For a new theory, use the
[contribution requirements](../development/contributing-changes.md#add-syntax-or-a-theory)
to add algorithm-specific tests and fuzzing alongside these interface checks.
