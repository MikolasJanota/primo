# Theory combination

Primo has an implemented general combination driver,
{source}`solve_component_recipe <src/smt/component_solver.cpp>`. It operates on
a collection of theory solvers supplied through a
{source}`CombinationRecipe <src/smt/component_solver.hpp#L19-L45>`, sharing the
SAT loop, assignment dispatch, equality decisions, and model coordination.
Each solver implements the [theory solver interface](theory-interface.md),
named `TheoryComponent` in C++; this also accommodates quantifier reasoning.

The current solver routes use these drivers:

| Fragment | Driver | Theory solvers and behavior |
| --- | --- | --- |
| `QF_UFLIA` | Shared SMT driver | {source}`The integer recipe <src/smt/solver_engines.cpp#L10245-L10278>` supplies EUF and integer arithmetic. Model guidance creates reversible equality choices; complete arrangements provide the fallback. Integer search remains bounded and can return `unknown`. |
| Quantified `UF` | Shared SMT driver | {source}`The quantified-UF recipe <src/smt/quantifiers/quantified_solver.cpp#L134-L151>` supplies EUF and quantifier reasoning. It reuses the theory solver lifecycle and SAT loop, adding queued instance lemmas. |
| `QF_UFLRA` | Dedicated Real coordinator | {source}`The Real engine <src/smt/solver_engines.cpp#L10801-L10847>` retains explained EUF/LRA equality exchange, model reconciliation, and exact fallback checks. |

The general driver supports more than two theory solvers. Existing
{source}`three- and four-solver tests <tests/unit/component_combination_tests.cpp#L409-L487>`
exercise its SAT loop, online and offline execution, and model construction.
QF_UFLRA currently remains on its dedicated route; the general driver itself
is already in use.

This chapter focuses on combining uninterpreted functions with arithmetic.
Their solvers must agree on equalities between shared terms while keeping their
own representations. The SMT layer purifies mixed terms, communicates
explained consequences, and coordinates candidate models. The
[DPLL(T) chapter](dpllt.md) describes the surrounding Boolean search;
[EUF](euf.md) and [LRA](lra.md) describe the participating solvers.
[Quantifier instantiation](quantifiers.md) has additional obligations beyond
this arithmetic combination.

## Why the solvers must communicate

Consider these constraints over Real constants `x`, `y` and an uninterpreted
function `f`:

```text
x <= y
y <= x
f(x) != f(y)
```

Arithmetic can satisfy its two inequalities by setting `x = y = 0`. EUF can
satisfy its disequality by giving `x` and `y` different interpretations. Those
two answers do not establish satisfiability of the combined problem.

The arithmetic constraints imply `x = y`. Once EUF receives that equality,
congruence gives `f(x) = f(y)`, contradicting the last constraint. Only the
equality crosses the boundary: EUF does not need the arithmetic inequalities
or tableau rows.

Let `a`, `b`, and `c` denote the true SAT literals selecting the three
constraints, and let `e` name `x = y`. An explained exchange can produce

```text
not a or not b or e       arithmetic explanation
not e or not c            EUF explanation
```

Resolving on `e` gives `not a or not b or not c`. An internal exchange can
compose the explanations without creating a SAT variable for `e`. Either way,
the conflict depends on the selected constraints; it must remain valid when
SAT backtracks or chooses another branch.

## Purification and the shared interface

A theory atom must be understandable by its owner. For example,
`f(x) + 1 <= y` contains both an uninterpreted application and arithmetic.
{source}`purify_mixed_arithmetic_applications <src/smt/solver_engines.cpp#L6765-L6795>`
introduces a fresh constant `p`, gives EUF `p = f(x)`, and gives arithmetic
`p + 1 <= y`.

In the other direction, `P(x + 1)` becomes `P(q)`, with the arithmetic
definition expressed by `q <= x + 1` and `x + 1 <= q`. The
{source}`argument purification code <src/smt/solver_engines.cpp#L6567-L6603>`
also records the original-term/proxy relationship for model reconstruction.
The transformation handles supported Real and Int terms; some helper and
counter names retain the earlier Real-only terminology.

These definitions belong to the transformed query. They are assertions, not
temporary model choices. Memoization by `TermId` reuses proxies and rewritten
subterms. Fresh symbols and terms are created through the solver's purification
scope and term manager, and the conversion record preserves how to interpret
the original expressions after solving.

For the dedicated Real route, shared terms are constants occurring in both
the EUF part and the normalized arithmetic part. An occurrence whose arithmetic
coefficient cancels to zero does not make a constant shared. Constants used by
only one theory remain local. Boolean values are handled through SAT and the
[Boolean argument encoding](sat.md), rather than by an arrangement over an
infinite shared domain.

## The combination conditions

Nelson–Oppen combination applies to purified theories with disjoint
nonlogical signatures, apart from equality and shared constants. Its usual
stable-infiniteness condition means that every satisfiable quantifier-free
constraint set has a model with an infinite domain. This leaves enough values
to realize the required disequalities between shared constants. The
[source mapping](#sources-and-implementation-status) gives both the original
account and the modern formulation used here.

An **arrangement** specifies which shared constants are equal and which are
different. It can be described as a partition into equality classes. Under the
combination conditions, the component models can be combined when they agree
on that partition.

**Convexity** determines whether deducing individual equalities is sufficient.
A theory is convex if a conjunction implying a disjunction of equalities must
imply at least one of those equalities individually. EUF and rational linear
arithmetic have this property. With complete equality deduction, repeated
exchange reaches either a conflict or an equality fixed point sufficient for
the convex combination.

For a fixed interface of `n` constants, each independent new equality merges
two previously distinct classes. At most `n - 1` such merges are possible.
This bounds progress of the convex closure; it does not bound the cost of
finding an equality or the surrounding Boolean search.

Integer arithmetic is not convex. With `0 <= x <= 1`, `y = 0`, and `z = 1`,
it implies `x = y or x = z`, but neither equality alone. Adding
`f(x) != f(y)` and `f(x) != f(z)` makes the combined problem unsatisfiable.
Waiting for one individually entailed equality would miss this contradiction.
Primo's integer route lets SAT explore interface equalities; the
{source}`non-convex arrangement regression <tests/unit/qf_uflia_solver_tests.cpp#L172-L221>`
exercises this example.

## Representation and ownership

The inputs are purified core terms, their Boolean abstraction, and assigned
theory literals. Outputs are explained clauses, candidate equality decisions,
compatible component models, or a typed inconclusive result.

| Representation | Owner and lifetime |
| --- | --- |
| {source}`InterfaceEqualityRegistryEntry <src/smt/theory_combination.hpp#L62-L71>` | A value in the Real route's `TheoryCombinationManager`. It records endpoints, sort, optional equality term/SAT literal, arrangement value, and classifications. |
| {source}`SharedModelState <src/smt/theory_combination.hpp#L121-L132>` | Owned by that manager for the current model round: value classes, explained equalities, accepted internal model equalities, and separation atoms. A new round or rollback invalidates it. |
| {source}`SharedEqualityDecisionState <src/smt/theory_combination.hpp#L138-L144>` | Solve-scoped pending, active, and tried equality choices. The engine applies them as SAT assumptions. |
| {source}`CombinationRecipe <src/smt/component_solver.hpp#L19-L45>` | Solve-scoped configuration supplied by the calling engine: component factories, check priorities, and preparation hooks. The general driver constructs and owns the resulting components. |
| {source}`TheoryComponent <src/theory/theory_component.hpp#L90-L140>` | Each instance owns its prepared theory data and mutable solving state. `solve_component_recipe` owns instances through `unique_ptr`; coordinators borrow pointers while those instances live. |
| {source}`TheoryCombinationPlan <src/smt/theory_combination_plan.hpp>` | Owns validated descriptor copies, atom-owner mappings, and shared scopes for one solve. Component registration uses this plan before search. |
| {source}`TheoryModelProjection and TheoryModelToken <src/theory/theory_solver.hpp#L63-L85>` | Owning equality-class vectors plus a component identity, assignment epoch, and model revision. A projection describes a candidate model, not logical entailments. |
| {source}`ComponentModelFragment <src/theory/theory_component.hpp#L81-L86>` | An owning export of typed bindings, retained facts, and symbol interpretations for an accepted token. It is consumed by model assembly. |

Terms are referenced by stable `TermId`s. No component owns the term arena,
and the term manager must outlive uses of those IDs. Registration and
purification can allocate terms and grow vectors; callers must reacquire
borrowed nodes after term creation. A copied registry entry or projection
owns its metadata but does not extend the lifetime of the term manager.

The Real registry canonicalizes an unordered pair by its endpoint IDs and
stores entries sparsely. {source}`ensure_interface_pair <src/smt/theory_combination.cpp#L499-L521>`
checks that both terms belong to the same shared sort. Creating an entry does
not create a SAT variable. Materialization is a separate SMT operation.

SAT assignment truth has one owner at the callback boundary:
{source}`AssignmentCoordinator <src/smt/assignment_coordinator.hpp>`.
The Real manager and theory components read a `sat::TrailView`; the manager's
{source}`assignment query <src/smt/theory_combination.cpp#L625-L649>` reads that
view. Its separate undo log restores **combination arrangement values**.
Theories also retain the undo information needed for their own closure or
tableau operations. These are different responsibilities from recording SAT
assignments; see [assignment ownership](dpllt.md).

## Consequences and model choices

An equality can reach the coordinator for two different reasons:

| Evidence | Permitted action |
| --- | --- |
| The component constraints entail `x = y`, with an explanation | Propagate it, learn a guarded clause, or merge internally while retaining the explanation. |
| One satisfying component model happens to give `x` and `y` the same value | Try a compatible model change or make a reversible SAT decision. This coincidence supplies no theory lemma by itself. |

For example, `x >= 0` and `y >= 0` admit the model `x = y = 0`, but also
models with different values. A failed attempt to use that equality does not
prove `x != y`. A conflict derived under the corresponding SAT assumption can
justify a blocking clause using the failed assumptions.

The component result type makes the distinction explicit:
{source}`TheoryCheckResult <src/theory/theory_solver.hpp#L101-L127>` separates
`EntailedLiteral`, `LearnedClause`, and `FreshDecision`. For an entailed literal
`e`, the reasons are true antecedents `r₁, …, rₙ`; the
{source}`coordinator constructs <src/smt/component_model_coordinator.cpp#L97-L114>`
`not r₁ or … or not rₙ or e`. A learned clause already carries its signs.

## The dedicated Real route

### When model mutation runs

De Moura and Bjørner deliberately leave mutation scheduling open. Section 4
of [*Model-based Theory Combination*](https://doi.org/10.1016/j.entcs.2008.04.079)
says, “We do not need to specify when
M-Mutate is applied.” Models may temporarily lag behind assigned literals;
the paper postpones model-based equality choices until case splits on
existing literals have been performed. Section 5.2 gives a greedy
freedom-interval procedure for diversifying a feasible arithmetic model,
but does not prescribe a call frequency for that procedure.

Primo makes that scheduling choice in its outer QF_UFLRA loop. With default
options, a round that has an EUF model and shared Real terms proceeds as
follows:

1. **Obtain a SAT candidate and check both theories.** After the
   {source}`SAT call <src/smt/solver_engines.cpp#L11247-L11288>` returns a
   candidate, EUF checks its assigned literals and LRA obtains a feasible
   tableau for its bounds and selected interface equalities. Conflicts are
   handled here; only consistent checks reach the mutation stage.
2. **Attempt one LRA diversification.** The
   {source}`partition construction <src/smt/solver_engines.cpp#L11547-L11565>`
   calls `lra_model_equal_partition`, which invokes
   {source}`diversify_shared_model <src/smt/solver_engines.cpp#L4266-L4295>`
   on a scratch assignment. The resulting equal-value classes describe the
   candidate arithmetic model for this round.
3. **Repair any remaining mandatory disequalities.** If SAT has selected
   `x != y` but the candidate still gives them equal values, the
   {source}`targeted separation step <src/smt/solver_engines.cpp#L11566-L11599>`
   searches strict orientations. A successful repair rebuilds the partition
   with diversification disabled. Unresolved obligations lead to the exact
   fallback; a probe limit returns `unknown`.
4. **Exchange explained equalities.** The engine runs the final
   {source}`EUF equality pass <src/smt/solver_engines.cpp#L11633-L11652>` and
   {source}`LRA equality pass <src/smt/solver_engines.cpp#L11655-L11708>`.
   An equality or conflict requiring another SAT check restarts the outer
   loop. Otherwise the candidate proceeds to reconciliation.
5. **Reconcile the candidate with EUF.** The
   {source}`reconciliation result <src/smt/solver_engines.cpp#L11836-L11873>`
   leads to acceptance, new equality assumptions followed by another SAT
   call, or exact fallback checks.

Thus the scratch mutation runs **before the final equality-exchange passes**,
without waiting for their fixed point or an EUF/LRA model disagreement.
A propagated equality can discard that round's candidate, and the next
feasible round attempts mutation again. Reusing a checked LRA tableau does
not cache its diversified candidate. This ordering is Primo's current
implementation choice; the paper does not require it. Mutation is performed
at this outer-loop stage, outside SAT's decision and propagation callbacks.

The {source}`enable condition <src/smt/solver_engines.cpp#L10810-L10812>` is
that mutation is allowed and either model reconciliation or model-equality
branching is enabled. Reconciliation and mutation are both enabled by
default. `--no-model-equality-mutation` disables diversification. Disabling
reconciliation also disables it unless `--model-equality-branching` is
enabled. Pure QF_LRA does not call this routine, and the production general
component route has no mutation-capable EUF/LIA adapters, as described below.

One call is a bounded greedy sweep, not a search for the most diverse model.
{source}`diversify_shared_model <src/theory/arithmetic/lra_tableau.cpp#L1554-L1712>`
copies the tableau assignment and considers nonbasic variables that can
change coincident shared values. It tries a bounded set of values within
each variable's freedom interval, updating dependent basic values without
pivoting. It keeps a move only if the number of value classes does not
decrease, no already-satisfied mandatory disequality is broken, and either
a mandatory disequality is repaired or the number of classes increases.
An empty scope needs no sweep. Neither does a model with no coincident scoped
values and no violated mandatory disequalities. A scheduled call can therefore
make no updates.
The live tableau's assignment, bounds, and basis remain unchanged.

For example, `x >= 0` and `y >= 0` allow a candidate with `x = y = 0`.
Moving `x` to 1 removes this accidental equality before EUF reconciliation
considers it. The new values prove neither `x != y` nor `x = y` as a theory
consequence. Likewise, failure to separate a pair is no equality proof:
the bounded sweep can miss feasible alternatives. Any model change still
has to pass the later combination checks.

### Explained equality exchange

EUF projects its shared congruence classes and emits a spanning set of
equalities for each class. A class of `k` terms needs `k - 1` such edges:
transitivity supplies the remaining equalities.

Arithmetic also propagates ordinary bound predicates within its own solver.
The [LRA propagation section](lra.md#propagate-and-explain-consequences) derives
their explanation clauses from individual bounds and tableau rows. Here the
conclusions cross the theory boundary as shared equalities. Both mechanisms
must explain entailment from active constraints, not from coincident values
in one candidate model.

(lra-default-equality-rules)=
#### Default LRA rules

LRA's opportunistic equality producer looks for consequences of the current
bounds and tableau rows. The default producer,
{source}`LraTableau::entailed_equalities <src/theory/arithmetic/lra_tableau.cpp#L1082-L1263>`,
implements four rules from de Moura and Bjørner, *Model-based Theory
Combination*, §5.1. Each conclusion remains true while its explaining bounds
hold, even if the arithmetic model changes.

A variable is **fixed** when its active lower and upper bounds coincide
exactly: `5 <= a <= 5` fixes `a` at 5. A linear expression is fixed when
every variable with a nonzero coefficient is fixed. Its value can then be
computed from those bounds. A current model value alone does not fix a
variable: `a >= 0` permits values other than the current choice `a = 0`.

The equations below are tableau row identities, preserved by every pivot.
The variables `x` and `y` represent shared terms; helper variables need not
be shared. A basic variable is the variable defined on the left of a row;
the right side contains nonbasic variables, as described in
[LRA's row representation](lra.md#sparse-rows-and-reverse-occurrences).

1. **`FixedEq`: two variables fixed at the same value.** Bounds
   `5 <= x <= 5` and `5 <= y <= 5` prove `x = y`. The explanation contains
   the bounds fixing both variables. Neither needs to be basic.

2. **`EqRow`: one row with a fixed zero remainder.** In a row `x = y + d`,
   fixing `d` at zero proves `x = y`. More generally, the remainder can be
   any fixed linear expression whose value is zero. The explanation uses
   the bounds fixing that remainder. Here `x` must be basic and `y` must
   have coefficient **+1**: `x = -y` alone would not prove equality.

3. **`EqOffsetRows`: two rows with the same variable and fixed offset.**
   Suppose `x = z + a` and `y = z + b`, with both `a` and `b` fixed at 2.
   Both rows define `z + 2`, so `x = y` even though `z` can vary. Each
   offset may be a different fixed linear expression; only its exact value
   must agree. The explanation uses the bounds fixing both offsets.

4. **`EqRows`: two rows with the same nonfixed expression and fixed offset.**
   This generalizes the previous rule. For example, `x = 2*u - 3*v + a`
   and `y = 2*u - 3*v + b`, with `a` and `b` fixed at 5, prove `x = y`.
   The coefficients of the nonfixed variables must match exactly. Merely
   evaluating two different expressions to the same number in the current
   model does not suffice. Again, the bounds fixing the offsets explain
   the equality. Both two-row rules require `x` and `y` to be basic.

For example, let SAT literals `p` and `q` assert `d >= 0` and `d <= 0`.
Given the row identity `x = y + d`, `EqRow` justifies the clause
`not p or not q or e`, where `e` represents `x = y`. If either bound is
later undone, this clause remains valid without asserting equality
unconditionally. The special row `x = y` needs no bound premises at all.

Primo applies these rules in a scan when the combination engine requests
equalities. `FixedEq` groups shared variables by fixed value. The two-row
rules group shared basic rows by their nonfixed variable/coefficient list
and exact fixed offset. Each group retains a representative and emits
equalities to it, avoiding enumeration of all pairs. Building these keys
still requires scanning row entries and performing exact arithmetic. This
scan differs from the paper's scheduling of the first three rules eagerly
and the last before a model-based equality choice. The alternative
`model-unit` producer is explained in
[LRA](lra.md#propagating-equalities-between-variables)
and remains opt-in.

These rules are sound but incomplete, and their success depends on the
current basis. For example, suppose the only row is `s = x - y`, the
nonshared slack `s` is fixed at zero, and shared `x` and `y` are nonbasic.
The constraints entail `x = y`, but neither shared variable is fixed or
has a row for these scans to inspect. Pivoting to `x = y + s` exposes an
`EqRow` match. The
{source}`fixed-variable elimination regression <tests/unit/lra_tableau_tests.cpp#L2152-L2166>`
demonstrates this change of basis. Closing this particular gap does not make
the rules complete. Finding no equality is therefore insufficient to accept
a combined model; the route retains model reconciliation and exact checks.

(lra-exact-equality-fallback)=
#### Exact fallback

When the exact fallback is required,
{source}`lra_implied_real_equalities_for_partition <src/smt/solver_engines.cpp#L4907-L4947>`
filters pairs whose endpoints both have mapped arithmetic values: different
values in the checked model disprove entailment, while equal-valued pairs
remain candidates. Pairs with an unmapped endpoint also remain candidates.
For an unresolved candidate, exact probes test the two alternatives `x < y`
and `y < x`.
Only infeasibility of both proves equality. The implementation reuses prepared
arithmetic state and groups already established equalities to avoid redundant
probes; a probe limit produces `unknown`.

Explained equalities can be absorbed into EUF internally. Otherwise the engine
materializes an equality atom and learns the guarded propagation clause before
re-entering SAT. An equality selected false must also be respected: a feasible
arithmetic model that still identifies its endpoints is insufficient.

### Online equality propagation

The Real route also has an opt-in `--nelson-oppen-prop=online` path.
It requires `--model-equality-branching` to be disabled: the
{source}`construction guard <src/smt/solver_engines.cpp#L11194-L11208>` omits
the online LRA propagator when that branching mode is enabled, even if
`--nelson-oppen-prop=online` was selected.

Suppose assigned literals `p` and `q` represent `x <= y` and `y <= x`, with `x` and
`y` shared between arithmetic and EUF. Together they force equality. Writing
`e` for `x = y`, the explanation clause is

```text
not p or not q or e
```

{source}`OnlineLraPropagator's paired-bound hook
<src/smt/solver_engines.cpp#L7502-L7551>` recognizes these oppositely oriented,
true non-strict comparisons and queues the pair with its reasons. It observes
assigned atoms: an ordinary arithmetic propagation can supply one of the
comparisons once SAT assigns it. This hook has its own enable condition;
enabling row propagation alone does not enable online equality exchange.

The {source}`mixed dispatcher <src/smt/solver_engines.cpp#L8064-L8125>` checks
that the endpoints form a shared interface pair and queues it. On delivery it
reuses an existing equality literal or creates and registers an observed one
on demand. Its {source}`propagation callback
<src/smt/solver_engines.cpp#L7920-L7958>` supplies an unassigned equality
literal with the clause above as its reason.

If `e` is already true, no propagation is needed. If `e` is false, SAT requires
`x != y`. The callback skips this assigned literal, but its disequality remains
an obligation for the later
[mandatory-disequality checks](#when-model-mutation-runs) and
[exact fallback](#lra-exact-equality-fallback). With `p` and `q` both true,
that disequality is impossible; skipping the callback does not permit the
combination engine to accept the candidate.

The equality can expose an EUF conflict. For example, if literal `d` asserts
`f(x) != f(y)`, congruence gives the clause `not e or not d`. Combining the
two clauses gives `not p or not q or not d`: the arithmetic bounds and the
EUF disequality cannot all hold. Each theory supplies its own explanation;
EUF does not need to reason about the coefficients or bounds that proved `e`.

This narrow online producer complements the final equality scans and model
checks. It does not replace the [default row rules](#lra-default-equality-rules)
or their [exact fallback](#lra-exact-equality-fallback), and the default
`--nelson-oppen-prop=final` does not enable it. The
[forward and reverse row derivations](lra.md#deriving-a-bound-from-a-row)
explain how arithmetic can first establish bound literals used in such chains.

### Reconciling candidate models

Reconciliation consumes the arithmetic candidate built by the
[mutation and separation stage](#when-model-mutation-runs). It now tries to
adapt the EUF model to that candidate's remaining equalities. Required shared
disequalities remain obligations throughout model construction.

{source}`reconcile_lra_candidate_with_euf <src/smt/solver_engines.cpp#L5082-L5202>`
then tries a spanning presentation of the LRA value classes in the EUF closure:

1. Check that equalities already present in EUF agree with the arithmetic
   values of the shared terms.
2. Checkpoint EUF and try each candidate equality. A direct conflict rejects
   that edge.
3. Project the resulting EUF classes again. Congruence may have merged other
   shared terms; every such merge must agree with the arithmetic model too.
4. If an edge is rejected, undo the candidate merges and return canonical pairs
   for lazy SAT assumptions. If all edges succeed, retain the compatible
   equalities for this model round.

The third step is essential. Trying `x = y` also forces `f(x) = f(y)`.
Even if EUF has no explicit disequality between the applications, arithmetic
may already assign them different values. The
{source}`congruence-consequence regression <tests/unit/qf_lra_solver_tests.cpp#L2530-L2553>`
checks that this causes a split rather than a false `sat` answer.

The {source}`outer engine <src/smt/solver_engines.cpp#L11820-L12002>` accepts
successful shared-model compatibility, re-enters SAT with selected choices, or
uses the exact convex fallback. Some conflicts compose explained equalities
directly without making all pairs visible to SAT. Unexplained candidate merges
remain confined to model reconciliation.

(the-component-collection-route)=
(the-general-component-driver)=
## The shared SMT driver

{source}`solve_component_recipe <src/smt/component_solver.cpp>` owns the SAT
loop and theory solver lifecycle. Its
{source}`ComponentModelCoordinator <src/smt/component_model_coordinator.hpp#L18-L38>`
works with the validated collection of solvers; their selection and checking
order come from the recipe. Standalone QF_LIA also uses this driver with one
LIA component and no shared scopes or equality arrangements; its full-check
pipeline is described in the {doc}`LIA chapter <lia>`. The following sections
describe the QF_UFLIA interaction. Quantified UF uses the same driver with a
lemma-preparation hook for newly generated instances.

The QF_UFLIA LIA adapter also scans the repaired rational tableau for explained
shared equalities before integer completion. The
[pre-completion integration](uflia-equality-propagation.md) describes batched
delivery, registration, explanation conventions and qualification status.

### Planning ownership and shared scopes

{source}`TheoryCombinationPlan <src/smt/theory_combination_plan.cpp#L50-L181>`
validates component identities, interpreted signatures, function-symbol
ownership, and the unique owner of each atom. Components declare their shared
sorts, convexity, and capabilities. An unsupported combination is reported as
unsupported; it is not a proof of unsatisfiability.

For each sort, the planner groups components connected by shared constants.
All participants in a connected group receive its normalized interface. A
component may therefore need to register a constant absent from its original
atoms. This prevents a chain of overlapping interfaces from losing a relation
needed elsewhere in the group. Disconnected groups remain separate.

The many-sorted contract requires stable infiniteness **simultaneously across
all shared sorts** of a component. Bool is excluded from these scopes. Each
group may have at most one component supplying authoritative values for a
shared sort. These normalization and admission rules are Primo's own design;
they are not claimed as a verbatim algorithm from the two-theory papers.

The QF_UFLIA recipe selects {adaptersource}`EufTheoryComponent <src/smt/solver_engines.cpp#L9944-L10083>` and
{adaptersource}`LiaTheoryComponent <src/smt/solver_engines.cpp#L10085-L10215>`.
It passes the active guided/complete policy directly to each factory and owns
the `:time.qf-uflia.*` timer keys. {adaptersource}`ComponentAdapterSupport <src/smt/solver_engines.cpp#L9836-L9942>` shares assignment,
interface, and token handling; neither adapter derives this policy from the
QF_UFLIA options. Their existing capability declarations and Int-only shared
interface are unchanged. The integer completion pipeline is described in the
{doc}`LIA chapter <lia>`.

### Materializing an interface equality

For a pair `x`, `y`, a component's `encode_equality` returns an equivalent
Boolean formula built from terms it understands. EUF returns `x = y`.
{adaptersource}`LiaTheoryComponent <src/smt/solver_engines.cpp#L10125-L10129>` returns
`x <= y and y <= x`.

The {source}`SMT materializer <src/smt/component_solver.cpp#L126-L181>` links
the equality literal `e` to those two bounds, `a` and `b`:

```text
not e or a
not e or b
not a or not b or e
```

All three clauses are needed. If SAT selects `not e`, the last clause forces
at least one bound false, letting arithmetic enforce the disequality. The
component supplies the meaning of equality; the SMT layer owns clausification
and SAT calls.

New atoms are registered at a detached preparation boundary. The
{source}`installation path <src/smt/component_solver.cpp#L215-L271>` updates
the owning components, rebuilds callback routing, and observes the resulting
SAT variables before search resumes. The dispatcher routes assignment changes
through the shared trail view and preserves explanations across unrelated
backtracking.

### Care pairs: which equality questions matter?

A **care pair** `(x, y)` identifies two shared terms of the same sort whose
equality a component wants the coordinator to examine. The pair asks whether
`x = y`; it supplies neither an answer nor an explanation. A **care graph**
has shared terms as vertices and care pairs as edges.

For example, suppose `x`, `y`, and `z` are shared Int constants and the EUF
part contains applications `f(x)`, `f(y)`, and `g(z)`, where `f` and `g` are
distinct unary function symbols:

| Shared pair | Relevance to these applications |
| --- | --- |
| `(x, y)` | If `x = y`, congruence forces `f(x) = f(y)`. This can merge result classes or contradict a disequality between the results. |
| `(x, z)` | Equating these arguments does not by itself connect `f(x)` and `g(z)`: their function symbols differ. |
| `(y, z)` | The same distinction holds for `f(y)` and `g(z)`. |

Primo's EUF producer therefore proposes `(x, y)` for these applications.
Arithmetic might currently assign `x` and `y` the same value while the EUF
model keeps them distinct. The coordinator can then try the equality through
SAT, causing both components to check that choice. Merely putting `(x, y)`
in the care graph does not justify learning `x = y` as a theory consequence.

The {source}`EUF pair generator <src/smt/solver_engines.cpp#L2969-L3072>`
uses the [curried, flattened applications](euf.md). It collects definitions
whose argument is a shared term, groups them by the current congruence class
of their function term, and returns distinct argument pairs within each group.
For a multiargument application, that function term can represent a partially
applied prefix. The proposed pairs can therefore change when congruence classes
change. The QF_UFLIA EUF component
{source}`refreshes its pairs after a successful full check and returns them for the checked model token <src/smt/solver_engines.cpp#L10071-L10093>`.

Jovanović and Barrett's *Being Careful about Theory Combination*, §3,
Definition 8, gives care functions a stronger contract: for a satisfiable
component, an empty care graph must guarantee that every complete arrangement
extending the propagated equalities and disequalities remains satisfiable. This
allows their combination method to restrict arrangement search. Section 4
derives an EUF care function from the argument positions that can trigger
congruence; the [source table](#sources-and-implementation-status) also links
the earlier *Sharing is Caring* paper.

Primo borrows that EUF insight to prioritize choices. Its pair generator does
not implement all the paper's exclusions for already settled argument pairs
and application results. The general component driver accepts suggestions
through {source}`TheoryComponent::care_pairs <src/theory/theory_component.hpp#L123-L125>`
without requiring the paper's care-function guarantee. It retains the complete
model-partition check described below, even when the care list is empty.

### Comparing complete model projections

{source}`ComponentModelCoordinator::reconcile <src/smt/component_model_coordinator.cpp#L140-L424>`
asks each component for a full check under the assigned interface relations.
In model-guided mode it validates a complete partition of every assigned
scope: every shared term appears exactly once, the sort matches, the component
and assignment epoch are current, and all SAT-selected relations hold.

The coordinator first compares the component models on the suggested care
pairs. A pair is a disagreement when one model puts its endpoints in the same
class and another keeps them separate. The
{source}`selection code <src/smt/component_model_coordinator.cpp#L347-L423>`
chooses a disagreeing care pair before considering other pairs, materializes
its equality literal, and returns a positive equality choice for SAT.

If no care pair disagrees, the coordinator compares spanning edges from every
projected partition. Each partition's spanning equalities must also hold for
the other participants in that scope; checking all of them establishes that
the partitions are identical without enumerating all pairs. A disagreement
found here also creates an equality choice. Agreement on care pairs alone is
insufficient for accepting the combined model.

The protocol also supports model mutation. A successful mutation must advance
the model revision and preserve every requested relation. The coordinator
regenerates comparison edges afterwards, including relations not mentioned in
the request. However, the production EUF and LIA descriptors both declare
`model_mutation = false`: their current combination uses equality decisions.
Generic support for mutation is not evidence that these adapters implement it.

### Search, fallback, and inconclusive results

{source}`solve_component_recipe <src/smt/component_solver.cpp#L270-L451>`
maintains the active equality assumptions. If SAT rejects them, it learns a
blocking clause from the failed assumptions and preserves surviving choices.
That rejection concerns the chosen arrangement, not necessarily the input.

Model guidance has a finite decision budget. Reaching it, or encountering a
recoverable unknown, unsupported, or resource-limited model-reconciliation
result, starts a fresh solve with the complete-arrangement policy. That policy
materializes every pair in each normalized scope and requires each component
to satisfy the selected equalities and disequalities. Missing model-projection
support also selects complete arrangements during planning.

The fallback exhausts the interface choices when the component procedures can
decide their obligations. It does not remove integer-search limits: integer
completion, resource exhaustion, or cancellation can still yield `unknown`.
An online dispatcher failure has its own typed exit; it is not converted into
an equality conflict. Cancellation does not trigger another search.

The QF_UFLIA recipe gives arithmetic priority during model reconciliation and
EUF priority during online checking. Online LIA checking uses rational
arithmetic state, followed by integer completion for full acceptance. These
priorities are explicit recipe data, not theory-name tests in the collection
driver.

## Building the combined model

The shared {ref}`integer stages <lia-schedule>` explain how the LIA component
completes a rational assignment and why it can return a resource limit.

Agreement on shared equality classes is the combination criterion. Printed
models must additionally preserve concrete arithmetic values, function
interpretations, and the original expressions removed by preprocessing.

The Real route supplies accepted internal equalities and arithmetic values to
{source}`its typed model construction <src/smt/solver_engines.cpp#L11873-L11898>`.
For the component route,
{source}`validate_component_fragment <src/smt/component_model_assembler.cpp#L425-L492>`
checks that every export belongs to the exact accepted token, covers required
shared bindings, respects the accepted arrangement or projection, and only
exports owned symbols.

{source}`assemble_component_model <src/smt/component_model_assembler.cpp#L494-L582>`
keeps uninterpreted domain labels local to each component until shared terms
identify them. Arithmetic supplies authoritative numeric values. Conflicting
bindings, retained facts, or function rows cause an error. Function arguments
are compared by their evaluated values, so two syntactically different tuples
cannot acquire inconsistent outputs after model assembly. Conversion records
restore eliminated expressions, and final assertions are evaluated before the
immutable `core::Model` is returned.

The [model-construction chapter](models.md) explains evaluated tuple keys,
component-local domain labels, reverse conversion replay, and the boundary
between mutable construction and the frozen interpretation.

## Cost and observable mechanisms

With `n` shared terms in one scope, a complete arrangement contains
`n(n - 1)/2` pairs. Sparse registration delays that cost; it does not remove
the cost of a fallback. Model-guided selection compares class spanning edges
and care pairs, but component checks, exact arithmetic, model exports, and
repeated SAT calls can still dominate.

| Mechanism | Cost boundary and evidence in counters |
| --- | --- |
| Sparse equality creation | Registry entries and materialized SAT pairs grow on demand. Compare `:no.interface.registry-entries` or `:combination.model.materialized-pairs` with the corresponding total-pair count. |
| Scratch LRA diversification | Each call copies all tableau values; trial moves update dependent rows and regroup scoped values. `:no.interface.lra-model-mutation-calls` counts invocations, including calls with no accepted `:no.interface.lra-model-mutation-updates`. These counts need not match SAT calls or successful reconciliation rounds. |
| Convex exact fallback | Equal-valued unresolved pairs may still require many arithmetic probes. `:no.interface.lra-exact-fallback-equalities` counts discovered equalities; it is not a count of all work. |
| Real model reconciliation | Candidate edges can induce larger congruence closures. `:no.shared-model.compatibility-consequence-disagreements` exposes disagreements created by those consequences. |
| Component model comparison | For a fixed number of participants, the ordinary care/spanning pass is linear in the supplied edges, with expected constant-time partition lookups. `:combination.model.care-pair-agreements` and `.spanning-pair-checks` count visits, including duplicates. This does not bound mutation or component solving. |
| Complete-arrangement restart | `:combination.model.complete-fallbacks` and its reason counters show when sparse guidance gave way to the fallback. |

## Defaults and limitations

The {source}`combination options <src/smt/check_sat.hpp#L396-L421>` and
{source}`mutation options <src/smt/check_sat.hpp#L234-L240>` distinguish enabled
behavior from experiments:

| Control | Current setting and scope |
| --- | --- |
| QF_UFLRA model reconciliation | Enabled; `--no-qf-uflra-model-reconciliation` selects the retained exact route. |
| `--no-model-equality-mutation` | Disables scratch LRA diversification, which otherwise runs when reconciliation or model-equality branching is enabled. |
| `--model-equality-mutation-scope={all,care}` | `all` by default. Experimental `care` scores EUF care-pair endpoints, then checks the complete shared partition and rejects a result with more candidate pairs. |
| `--nelson-oppen-prop` | `final` by default. `online` enables the paired-bound propagation path when `--model-equality-branching` is disabled. `exact` disables the optional equality producer while retaining required combination checks; legacy `off` is an alias for `exact`. |
| QF_UFLIA model guidance | Enabled independently of the EUF engine choice. `--no-qf-uflia-model-combination` selects complete arrangements directly. The model-decision budget defaults to 512. |
| QF_UFLIA online components | Enabled when the incremental EUF engine is selected. |
| `--experimental-generic-model-combination` | Off. It selects the older `ModelBasedCombinationCoordinator` inside QF_UFLRA. Production uses of the general component driver do not require this flag. |

The general driver is tested with three and four components. Its production
recipes currently pair EUF with integer arithmetic or quantifier instantiation;
the dedicated Real route pairs EUF with LRA.
Finite shared domains, overlapping interpreted signatures, and unrestricted
combinations of arbitrary new theories are outside its admitted contract.
The default session rebuilds solving state between
queries; online state within a solve does not imply cross-query persistence.

## Sources and implementation status

| Source and sections | Implemented use and boundary |
| --- | --- |
| Greg Nelson and Derek C. Oppen, *Simplification by Cooperating Decision Procedures*, TOPLAS 1(2), 1979, pp. 245–257. The [1978 Stanford report](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/stanford/Stanford_CS_TR_Collection_2025-12-12/OCR/CS-TR-78-652-ocr.pdf), §§2.2–2.4, is the reading copy. | Purification, equality exchange, and the distinction between convex closure and case splitting. Primo's SAT integration and representation are separate adaptations. |
| [Leonardo de Moura and Nikolaj Bjørner, *Model-based Theory Combination*](https://doi.org/10.1016/j.entcs.2008.04.079), ENTCS 198(2), 2008, pp. 37–49, §§2.1, 4, 5.1–5.2 and Appendix A | Candidate-model partitions, reversible equality choices, opportunistic LRA equalities, and freedom-interval mutation. The dedicated Real path implements these mechanisms with its own reconciliation checks. Neither route implements the full Appendix A `DPLL(T^M)` transition system. |
| [Bruttomesso, Cimatti, Franzén, Griggio and Sebastiani, *Delayed Theory Combination vs. Nelson–Oppen for Satisfiability Modulo Theories: A Comparative Analysis*](https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf), LPAR 2006, §§2–3, Figure 4 | SAT-visible arrangements and learned clauses permit incomplete equality deduction and non-convex components. Primo does not claim the paper's NO-emulation branching bound or its particular last-UIP/negative-first strategy. |
| [Dejan Jovanović and Clark Barrett, *Sharing is Caring: Combination of Theories*](https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf), FroCoS 2011, pp. 195–210; expanded as [*Being Careful about Theory Combination*](https://theory.stanford.edu/~barrett/pubs/JB13.pdf), Formal Methods in System Design 42(1), 2013, pp. 67–90, §§3–4 | EUF argument pairs that can trigger congruence motivate Primo's care-pair generator. The component driver uses its output to prioritize model disagreements. It retains complete partition comparison and arrangement fallback rather than relying on the paper's care-function contract to omit other relations. |
| [Alberto Griggio, *An Effective SMT Engine for Formal Verification*](https://iris.unitn.it/handle/11572/368265), PhD thesis, 2009, §1.5.1, pp. 28–35 | Detailed purification, shared-variable definitions, arrangement criterion, and the distinction between classical and SAT-mediated combination. This is an explanatory source, not MathSAT implementation code. |
| [Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*](https://cs.nyu.edu/media/publications/king_tim.pdf), PhD thesis, 2014, §§1.5.5–1.5.6 | Theory-engine ownership, dispatch, and the distinction between partial and full checks. Its care-graph discussion is context; Primo's care-pair selection does not implement a general care-graph algorithm. |
| [Martin Bromberger, *Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955), PhD thesis, 2019, §5.4 | Equality-basis normalization and explanations motivate a further arithmetic equality producer. This complete basis method remains unimplemented; local tableau rules and exact probes have different costs and completeness mechanisms. |
| {source}`Primo's component design <docs/n-way-combination-migration.md>` | Many-sorted scope normalization, component descriptors, token freshness, detached registration, and typed model exports are original implementation contracts. |

The related [LRA source map](lra.md#sources-and-implementation-status) covers
the simplex papers and the newer optional equality producer. The
{ref}`review record <theory-combination-chapter>` records the primary
sections read for this chapter.

## Invariants exercised by existing tests

| Obligation | Regression or mechanism test |
| --- | --- |
| Default LRA equality rules require matching bounds, zero remainders, and identical nonfixed expressions | {source}`FixedEq and EqRow regressions <tests/unit/lra_tableau_tests.cpp#L1653-L1786>`; {source}`two-row matching and rejection regressions <tests/unit/lra_tableau_tests.cpp#L1806-L1899>` |
| Equality must travel in both directions until the convex problem is settled | {source}`EUF–LRA–EUF fixed-point regression <tests/unit/qf_lra_solver_tests.cpp#L1429-L1452>` |
| A propagated equality remains guarded by the branch that proves it | {source}`assignment-guard regression <tests/unit/qf_lra_solver_tests.cpp#L1553-L1594>` |
| Online paired bounds publish an explained equality and register its atom lazily | {source}`online equality regression <tests/unit/qf_lra_solver_tests.cpp#L2818-L2853>` |
| A candidate merge must preserve arithmetic agreement after congruence | {source}`optional-merge consequence regression <tests/unit/qf_lra_solver_tests.cpp#L2530-L2553>` |
| A mixed model can separate a coincident pair without materializing its equality | {source}`single-call mutation and pair-pruning regression <tests/unit/qf_lra_solver_tests.cpp#L2299-L2326>` |
| Mutation preserves tableau state and already-satisfied mandatory disequalities | {source}`scratch-state regression <tests/unit/lra_tableau_tests.cpp#L2420-L2449>`; {source}`mandatory-disequality regressions <tests/unit/lra_tableau_tests.cpp#L2518-L2569>` |
| Combination values roll back and SAT truth comes from the shared view | {source}`manager rollback and assignment tests <tests/unit/theory_combination_tests.cpp#L474-L592>` |
| Scope normalization rejects invalid ownership and respects disconnected sorts | {source}`admission and scope tests <tests/unit/component_combination_tests.cpp#L268-L344>` |
| More than two participants agree with an independent partition oracle | {source}`three/four-component cases <tests/unit/component_combination_tests.cpp#L409-L487>` |
| Stale or incomplete model projections cannot be accepted | {source}`malformed-projection tests <tests/unit/component_combination_tests.cpp#L489-L525>` |
| Mutations cannot hide newly created disagreements; non-convex choices remain alternatives | {source}`mutation and disjunction tests <tests/unit/component_combination_tests.cpp#L591-L650>` |
| EUF care pairs follow function classes, including prefixes of multiargument applications | {source}`EUF care-pair regressions <tests/unit/qf_lra_solver_tests.cpp#L2068-L2140>` |
| Care pairs take priority, while agreements still lead to the spanning check and invalid pairs are rejected | {source}`care selection and validation regression <tests/unit/component_combination_tests.cpp#L652-L701>` |
| A surviving propagated literal keeps its explanation across unrelated rollback | {source}`routing and explanation-lifetime test <tests/unit/component_combination_tests.cpp#L527-L589>` |
| A zero guidance budget reaches complete arrangements | {source}`fallback regression <tests/unit/qf_uflia_solver_tests.cpp#L275-L299>` |
| Purified arguments use their arithmetic values in the printed model | {source}`integer argument-model regression <tests/unit/qf_uflia_solver_tests.cpp#L353-L380>` |

(evidence-and-open-leads)=
## Evidence

The {source}`component migration record <docs/n-way-combination-migration.md>`
retains the paired QF_UFLIA evaluations, fuzzing and independent model checks.
The {source}`Real combination record <docs/nelson-oppen.md>` and
[assignment migration evidence](../evidence/assignment.md) preserve
their separate revision boundaries and limitations. Exact run provenance is in
{source}`the run log <docs/run-log.json>`; these historical runs are not fresh
qualification of this documentation pass.

The [arithmetic evidence summary](../evidence/arithmetic.md#combination-evidence)
connects those records to the current routes and remaining migration work.

## Related open leads

- [Complete implied-equality basis](../leads/lra-equality-basis.md)
- [More selective and cheaper care-pair generation](../leads/care-pair-generation.md)
- [Reducing combination obligations with care graphs](../leads/care-guided-combination.md)
- [Move QF_UFLRA to the general component driver](../leads/real-combination-migration.md)
- [Further model-based combination transitions](../leads/model-based-combination.md)
