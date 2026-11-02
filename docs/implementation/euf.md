# EUF: congruence, explanations, and rollback

This is the equality-theory chapter in the [Theories section](theory-interface.md#theories-and-their-shared-contract).
The [theory solver interface](theory-interface.md) defines the assignment,
explanation, and model boundary around the congruence-closure implementation.

Equality with uninterpreted functions (EUF) requires equal arguments to give
equal results: `a = b` implies `f(a) = f(b)`. It does not require `f` to be
injective: equal results need not imply equal arguments. The solver maintains
the equalities implied by the current assignments and detects when an asserted
disequality contradicts them.

The [DPLL(T) chapter](dpllt.md) describes how assignments reach theory solvers
and how SAT uses their explanations. This chapter explains the congruence-closure
implementation behind that interface. The {source}`default options <src/smt/check_sat.hpp#L219-L242>`
select `FlatCongruenceClosure` and one-sided breadth-first search (BFS)
explanations. Propagation scheduling is a separate option; selecting this implementation does not mean
every consequence is propagated immediately.

## Representation and ownership

Primo represents function applications using binary `apply` nodes and gives
each application result an internal name. This follows the currying and
flattening transformations in [Nieuwenhuis and Oliveras, *Fast Congruence
Closure and Extensions*](https://doi.org/10.1016/j.ic.2006.08.009),
Sections 3.1.1 and 3.1.2, respectively.

Three identities have different meanings. A core `TermId` identifies immutable
syntax. A `FlatTermId` indexes a node stored by a
{source}`FlatTermIndex <src/theory/equality/flat_terms.hpp>`; these IDs are local
to that table. A congruence-class representative identifies the terms currently
known equal; that representative can change after a merge or backtrack.

| State | Owner and lifetime |
| --- | --- |
| Typed terms and symbols | {source}`core::TermManager <src/core/term_manager.hpp>` owns the terms. EUF borrows the manager; it must outlive the flattened index and any interpretation of its core IDs. |
| Flattened nodes and definitions | {source}`FlatTermIndex <src/theory/equality/flat_terms.hpp>` owns append-only node/definition vectors and maps from core IDs and application pairs. Numeric IDs remain stable as this index grows; references into its vectors do not. |
| Prepared query structure | {source}`PreparedFlatTheory <src/smt/solver_engines.cpp#L893-L913>` owns an index, atom mappings, and a base closure containing application definitions and `true ≠ false`. Preparation adds no query-specific assignments or reasons. |
| Online equality state | {source}`IncrementalFlatAssignmentState <src/smt/solver_engines.cpp#L1280-L1440>` owns a working index and `FlatCongruenceClosure`, optionally copied from prepared structure. SAT decision levels determine when it saves closure checkpoints. |
| Classes, explanations, and undo | {source}`FlatCongruenceClosure <src/theory/equality/flat_congruence_closure.hpp>` owns these separately: representatives answer equality queries, labelled edges justify equalities, and undo records restore mutations. None owns the SAT assignment. |

`FlatTermId` is a `std::size_t` index, not a public term handle or a SAT variable.
`FlatCongruenceClosure` stores numeric endpoints without core sort information:
callers must supply correctly typed equations. It neither parses SMT-LIB nor
creates core terms. See [core ownership](core.md) for the term manager's separate
lifetime and allocation rules.

Checks outside the live callback can instead copy the prepared base and replay
the candidate's assignments, as in {source}`check_incremental_euf_closure
<src/smt/solver_engines.cpp#L2704-L2726>`. Prepared definitions can be reused
without carrying mutable facts from one candidate into another.

### Turning applications into binary definitions

{source}`FlatTermIndex::flatten <src/theory/equality/flat_terms.cpp#L25-L91>`
performs both transformations. Currying turns `g(a, b)` into
`apply(apply(g, a), b)`. Flattening then names the application results:

```text
apply(g, a) = u
apply(u, b) = v       v represents g(a, b)
```

`g` here is an internal function-symbol node; `u` is an intermediate node.
These are bookkeeping for first-order terms, not new SMT-LIB functions or
assertions. {source}`apply <src/theory/equality/flat_terms.cpp#L120-L130>`
shares identical pairs and records one `FlatDefinition{function, argument,
result, original}` for each new pair. Nested and repeated subterms reuse IDs.

The flat interface accepts the prepared EUF fragment. It rejects arithmetic
nodes and quantified terms; `ite`, integer division/modulus, and `abs` must
have been lowered before reaching it. Boolean formula nodes can be represented
as opaque nodes, but flattening alone does not establish their Boolean meaning.
That obligation belongs to the SMT preparation described below.

## Follow one conflict

Let `p` be the SAT literal for `a = b`, and `q` the literal for
`f(a) = f(b)`. Assume the two application definitions were installed before
search. The following steps show how congruence closure detects a conflict and
undoes its changes on backtracking, with `¬q` retained at the root:

| Step | Equality state | Explanation information |
| --- | --- | --- |
| Assign `¬q` | Record `f(a) ≠ f(b)`; no conflict yet. | Store `q`, the negation of the assigned literal. |
| Open a decision level, then assign `p` | Save a checkpoint and merge the classes of `a` and `b`. | Record a direct equality edge carrying `¬p`. |
| Update affected application signatures | `apply(f, a)` and `apply(f, b)` now have the same signature. Merge their results. | Record a congruence edge referring to the two definitions. |
| Check the retained disequality | Its endpoints are now equal, so the assignment is inconsistent. | Explain the result equality using `a = b`, then include the disequality's reason. |
| Backtrack to the root | Undo the decision's equality, implied merge, and conflict. Keep the root disequality and initial definitions. | Remove discarded edges and reasons; the old explanation must no longer justify a new consequence. |

The returned conflict clause is:

```text
¬p ∨ q               equivalently: a = b ⇒ f(a) = f(b)
```

The experimental [QF_UF exporter](../reference/qf-uf-conflicts.md) records
the conflicting facts `p, ¬q` by complementing this clause. It selects the
lazy engine and records only complete-candidate theory conflicts.

Both literals are false under `p, ¬q`. SAT can use this clause to exclude that
combination. In particular, with `¬q` retained it can derive `¬p`.

The signs come from {source}`assert_flat_assigned_atom
<src/smt/solver_engines.cpp#L1200-L1214>`: each asserted fact carries its
**negated SAT assignment**. `FlatCongruenceClosure` combines those literals without
negating them again. In this example `explain_equal(f(a), f(b))` supplies
`[¬p]`; the disequality conflict supplies `[¬p, q]`. An empty reason is reserved
for an unconditional fact, such as `true ≠ false`, rather than an unexplained
SAT assumption.

Preprocessing and SAT may reach this contradiction in a different order.
The [first input](../index.md#build-and-solve-a-first-problem)
expresses the same contradiction. SAT decision checkpoints also differ from
SMT-LIB assertion `push`/`pop`, as explained in [DPLL(T)](dpllt.md).

## How a merge finds its consequences

The {source}`data structures in FlatCongruenceClosure <src/theory/equality/flat_congruence_closure.hpp#L209-L240>`
implement the flat congruence-closure scheme from Nieuwenhuis and Oliveras,
Section 3:

| Paper operation or structure | Primo representation and invariant |
| --- | --- |
| Representative | `parent_[t]` directly names the current representative of every member. There is no parent-chain walk on an ordinary representative lookup. |
| Class list | `class_members_` and `class_size_` identify all members. A merge moves the smaller class into the larger and updates those members' representatives. |
| Use list | `use_lists_` identifies definitions affected when an operand's representative changes. Processing stamps suppress repeated work on duplicate entries. |
| Signature lookup | `lookup_` maps `(representative(function), representative(argument))` to a result whose definition witnesses that signature. Equal signatures require equal results. |
| Pending merges | `pending_` stores direct equalities or pairs of colliding definitions. `propagate()` removes them in LIFO order until exhausted or a conflict is found. |

{source}`merge_classes <src/theory/equality/flat_congruence_closure.cpp#L362-L503>`
changes representatives, records the explanation edge, checks newly collapsed
disequalities, and processes the disappearing representative's use list.
{source}`process_use_list <src/theory/equality/flat_congruence_closure.cpp#L505-L555>`
recomputes only the affected signatures. A lookup collision enqueues a
congruence merge, which may affect more applications.

The lookup table can retain old keys after a merge; fresh probes use current
representatives. {source}`add_definition
<src/theory/equality/flat_congruence_closure.cpp#L153-L200>` accepts a repeated
identical definition but rejects a different definition for an existing result.
That one-definition-per-result rule makes lookup witnesses unambiguous.

Disequalities have their own per-class index. A merge checks the intersection
of the two classes' disequality lists: only a disequality connecting those
classes can become false at that merge. List traversal still costs work; this
is not a constant-time disequality check. A direct conflict can stop processing
before all pending congruence merges finish. Once `conflict()` is present,
the caller must handle it rather than treat that state as a completed closure.
The implementation is in {source}`check_disequalities_touching
<src/theory/equality/flat_congruence_closure.cpp#L633-L678>`.

### What the cost argument covers

In a monotone sequence of merges, moving the smaller class means each moved
member's class at least doubles. Thus each member changes representative at
most logarithmically many times. Section 3.5 uses this argument for its
`O(n log n)` closure bound. Primo follows that organization, with hash-table
operations expected to be constant time.

This is not an `O(n log n)` bound for a Primo query. Disequality-list work,
explanations, copied preparation, undo storage, propagation scheduling, and
repeated search after backtracking add costs. The
{source}`congruence-closure counters <src/theory/equality/congruence_stats.hpp>` expose
lookup probes/hits, congruence merges, disequality checks, duplicate skips,
explanation visits, and trail entries so those mechanisms can be measured.

## Explaining an equality

The default explanation structure records the actual endpoints of each
successful merge. A direct edge carries SAT reason literals. A congruence edge
carries the two definitions whose function and argument nodes were equal.
{source}`explain_equal_into
<src/theory/equality/flat_congruence_closure.cpp#L1025-L1121>` searches the
adjacency graph; {source}`append_edge_reason
<src/theory/equality/flat_congruence_closure.cpp#L742-L755>` expands congruence
edges into explanations of their operands.

Those recursive searches use only edges **older than the congruence edge being
explained**. To justify `f(a) = f(b)`, the search must establish `a = b`
without using that very result equality, or a later equality that depends on
it. The edge index supplies this bound. Within one reason collection, cached
subproofs can be reused only when their saved bound is no later than the
current bound. A proof found using newer edges cannot answer an earlier query.

Repeated SAT literals are removed while preserving their signs. BFS does not
guarantee a smallest set of premises: even a short graph path can recursively
require many equalities. The result is an internal explanation for a SAT clause,
not a proof object exported to the user.

### Optional explanation algorithms

`--euf-explain-search` chooses among these implemented modes. All except
`one-sided` are opt-in.

| Value | Implemented mechanism and limitation |
| --- | --- |
| `one-sided` | Default BFS over labelled merge edges, with recursive causal bounds and per-explanation caching. This is Primo's graph-search design, not the paper's optimized proof extraction. |
| `bidirectional` | Searches from both endpoints while preserving the same causal restriction. Reason size is still not minimized. |
| `proof-forest` | Earlier representative-oriented forest in `FlatCongruenceClosure`; a partial paper-inspired variant, distinct from the raw-endpoint algorithm below. |
| `reoriented-proof-forest` | {source}`ProofForest::union_nodes <src/theory/equality/proof_forest.cpp#L169-L215>` reorients the smaller tree and joins raw endpoints as in Section 4. {source}`explain <src/theory/equality/proof_forest.cpp#L330-L410>` uses Section 5.2's auxiliary union-find to skip proof paths already covered. |
| `structural-proof-forest` | Uses Section 5.5's structural-class idea. {source}`coalesce_into_structural_class <src/theory/equality/proof_forest.cpp#L95-L167>` aliases eligible fresh members; already connected members require real tree edges. Materializing an alias preserves its equality justification. This is not an unconditional collapse of every congruence merge. |

Theorem 17's near-linear bound in the output proof size belongs to the paper's
extraction algorithm. It is not a bound for the default BFS or a measured
end-to-end guarantee for these options. Primo also adds sparse undo, SAT reason
translation, and structural-alias handling. Detailed implementation checklists
and historical comparisons remain in the {source}`paper design note
<docs/papers/fast-congruence-closure.md>` and {source}`implementation note
<docs/papers/fast-congruence-closure-implementation.md>`.

## Rolling back theory state

{source}`push and pop <src/theory/equality/flat_congruence_closure.cpp#L53-L105>`
save vector lengths and restore recorded mutations. Class-member suffixes let
undo restore the moved members' representatives together. Other records restore
use lists, disequality lists, lookup insertions, explanation adjacency, and
optional proof-forest state. Definitions and nodes added after a closure
checkpoint are also removed from that closure; definitions installed before it
remain. Popping consumes the selected checkpoint and all younger checkpoints.

Representative-oriented proof-parent arrays belong only to `proof-forest` or
explicit proof diagnostics. Their rollback in
{rollbacksource}`FlatCongruenceClosure::pop <src/theory/equality/flat_congruence_closure.cpp>`
uses the same mode condition as allocation in `ensure_node_count`. Default
BFS and the separate reoriented/structural forests keep those unused arrays
unallocated when diagnostics are off. The
{rollbacksource}`nested rollback regression <tests/unit/euf_solver_tests.cpp>`
checks this storage invariant across all five modes and both diagnostic
settings, including nodes created after a checkpoint. This removes an
unneeded allocation path during temporary model-scope teardown. The
[static-analysis triage](../evidence/static-analysis.md) distinguishes that
repair from the remaining review of `ModelScope`'s exception contract.

This undo log records **what a theory operation changed**. The shared SAT
assignment view records **which literals are true**. Both are needed: dropping
`p` from the assignment view alone would not split the classes joined because
of `p`. The {source}`SMT wrapper <src/smt/solver_engines.cpp#L1366-L1410>`
records every SAT level but opens a closure checkpoint only when theory state
first changes at that level. An unobserved SAT decision can therefore create
an empty theory level without confusing later backtracking.

Changed-term notifications have a different lifetime from equality facts.
{source}`drain_changed_terms <src/theory/equality/flat_congruence_closure.cpp#L278-L300>`
consumes them; a subsequent pop must not deliver the same notifications again.
The saved generation distinguishes still-live prefixes from already drained
ones. Temporary model work uses a {source}`protected checkpoint
<src/smt/solver_engines.cpp#L1470-L1480>` so a borrowed closure view cannot be
invalidated by an ordinary pop.

Retaining SAT state across SMT-LIB queries does not retain the preceding
query's mutable equality facts. The assumptions engine reuses prepared
structure and creates a working closure for the current query. The
[incremental-memory lead](../leads/incremental-memory.md#lifetime-of-flattened-euf-terms)
also asks how a reference-counting design would manage flat nodes, their core
term references, and dependent theory state.

## Boolean arguments and propagation

Bool has exactly two values. If `b` occurs in `f(b)`, EUF must agree with SAT
about whether that argument is `true` or `false`. Otherwise it could incorrectly
satisfy `f(b) ≠ f(true)` and `f(b) ≠ f(false)` using a third Boolean value.

### Connecting Boolean terms to SAT

{source}`Boolean-argument registration <src/smt/clausifier.cpp#L104-L155>`
makes the relevant Boolean constants theory-visible under either polarity.
It reuses their literal, allocating their first SAT variable if needed, and
adds no separate equality atoms or linking clauses. On assignment, the SMT
wrapper merges the Boolean node with the chosen canonical value and supplies
the negated assignment as its reason. The base closure enforces
`true ≠ false` without a SAT premise.

For ground SMT-LIB input, the {source}`frontend naming step
<src/smt/frontend.cpp#L342-L357>` replaces every Boolean argument other than
a constant or Boolean value with a fresh Boolean constant and a defining
equivalence. Thus `f(and p q)` becomes `f(b)` with `b ↔ (p ∧ q)`.
This also applies to a predicate used as an argument: `f(P(a))` becomes `f(b)`
with `b ↔ P(a)`. Registration protects `b`; the equivalence makes both
polarities of `P(a)` visible. Pure CNF auxiliary variables that do not denote
theory terms need no EUF node. The [SAT chapter](sat.md) describes that boundary.

These assertions are justified by the SAT assignment, not guessed theory
facts. If the literal for `b` is `p`, assigning `p` entails `b = true` and
assigning `¬p` entails `b = false`. The corresponding reason tails are `[¬p]`
and `[p]`, following this chapter's negated-assignment convention.
`true ≠ false` is unconditional. Registration therefore excludes only
assignments incompatible with the two-element Boolean domain; it cannot
exclude a satisfying SMT-LIB interpretation.

### When pure-theory filtering is sound

The {source}`pure-theory filter <src/smt/solver_engines.cpp#L517-L553>` may omit
an assigned literal from EUF while leaving it in SAT. For an atom occurring
only positively in the asserted Boolean structure, it omits a false
assignment; for a negative-only atom, it omits a true assignment. Mixed-polarity
atoms are retained. `--pure-theory-filter=auto|on|off` controls this rule;
`auto` currently enables it. This is the assignment-filtering technique of
[Sebastiani, *Lazy Satisfiability Modulo Theories*](https://disi.unitn.it/rseba/papers/JSAT-2007.pdf),
§6.10 and Proposition 2.3, rather than SAT pure-literal elimination.

Let `F` be the Boolean abstraction of the prepared assertions and `M` a
satisfying SAT assignment. Suppose atom `a` occurs only positively and
`M(a) = false`. In negation-normal form, changing just `a` from false to true
cannot make `F` false: conjunction and disjunction are monotone in that
argument. The mirror statement holds for a negative-only atom assigned true.
Consequently, after deleting these opposite-polarity literals from `M`,
**every completion of the remaining atom assignment still satisfies `F`**.

If a theory interpretation satisfies the retained literals, its truth values
provide such a completion, so it satisfies the prepared assertions. The same
argument permits several deletions. It concerns the original Boolean
structure: Tseitin auxiliaries can be reevaluated for the completed atom
values and need not keep their old SAT values.

The crucial premise is that the theory check really supplies an interpretation
of those atoms under the intended semantics. For ordinary EUF equalities,
unconstrained classes can receive distinct values in an uninterpreted sort.
For Boolean arguments, congruence closure additionally needs the two-element
domain constraint. Omitting an argument's truth assignment can remove the
very constraint that makes the closure correspond to a Boolean interpretation.
Propositional purity alone does not justify that omission.

### The Boolean-argument counterexample

Consider this complete input, also covered by the
{source}`positive-polarity regression <tests/unit/boolean_solver_tests.cpp#L2836-L2856>`:

```lisp
(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))
(assert (not (= (tb p) (tb true))))
(assert (or p q))
(check-sat)
```

If `p = true`, congruence contradicts the second assertion directly.
If `p = false`, congruence and the first assertion give the same contradiction.
Those cases exhaust Bool, so the input is unsatisfiable.

But the Boolean skeleton admits `p = false, q = true`. Polarity collection
sees `p` only positively in `p ∨ q`; it stops at the surrounding equality
atoms and does not count the argument of `tb`. If filtering dropped `p = false`
from the theory assignment, the closure could keep `p` separate from both
Boolean constants and `tb(p)` separate from their common result. That closure
would be consistent only because it was missing the Boolean-domain constraint.

`TheoryAtom::theory_relevant_at_both_polarities` prevents this. Registration
sets it even when an argument already has an atom and polarity flags;
`should_filter_assignment` checks it before applying the purity rule.
The negative-polarity case has the same obligation. Any future encoding that
uses an atom's assignment to enforce additional theory semantics must review
this condition explicitly.

### Why no third Boolean value is needed

The coverage argument applies to a complete assignment of the ground,
prepared EUF input. Registration and frontend naming cover the following cases:

| Boolean occurrence | Why the two-element domain is respected |
| --- | --- |
| `true` or `false` | These are the canonical values, and the base closure keeps them distinct. |
| Constant in a function argument | Registration keeps its assigned equality to `true` or `false` at either polarity. |
| Compound term or predicate application in an argument | The frontend names it as above. The name is protected, and its defining equivalence fixes its relation to the original expression. |
| Predicate result used as a formula | A retained assignment links it to a canonical value. A filtered result may remain unanchored, as explained below. |
| Source-level Boolean `=` or `distinct` | The {source}`frontend <src/smt/frontend.cpp#L318-L340>` lowers these to equivalence or exclusive-or. They impose propositional constraints rather than disequalities between unconstrained Boolean classes. |
| Term used only in the propositional structure | Its value is handled by SAT; merely encoding a connective does not put a Boolean argument into EUF. |

Thus **every Boolean argument that can affect an application signature is
anchored**. Congruent predicate applications already have the same result
class. A remaining unanchored predicate-result class has no outgoing role as
an unnamed Boolean argument and no source-level Boolean disequality to preserve.
It can be assigned either Boolean value without changing the argument
signatures or invalidating retained facts. Anchored classes keep their values;
the two canonical classes cannot be merged in a consistent closure.

This yields a two-valued interpretation of the Boolean terms. Ordinary EUF
class completion supplies values for the uninterpreted sorts, and the filtering
argument above shows that the completed atom values satisfy the assertions.
It would be too strong to claim that every Boolean node is already anchored
in the *search* closure: filtered predicate results need not be.
The {source}`model builder's completion step
<src/smt/typed_model_builder.cpp#L90-L112>` assigns false to Boolean constants
and applications still unanchored after saturation, then checks consistency.

This is an implementation argument for the prepared ground fragment, not a
machine-checked proof or a finite-domain procedure for arbitrary sorts.
Quantified bodies require the separate [quantifier preparation](quantifiers.md).
The {source}`Boolean-domain regressions <tests/unit/boolean_solver_tests.cpp#L2836-L2951>`
cover both filtered polarities, domain exhaustion, a satisfiable case, and
ordinary predicates that require no argument registration.

### Propagation and bounded Ackermann clauses

Filtered assignments must also be absent from propagation premises, and a
candidate with a filtered polarity must not be propagated. The
{source}`model-time propagation builder <src/smt/solver_engines.cpp#L1228-L1277>`
checks both conditions. Otherwise propagation could reinstate a constraint
that filtering deliberately left out, or use a premise never asserted to EUF.

The ordinary {source}`incremental propagation path
<src/smt/solver_engines.cpp#L1435-L1460>` explains equality atoms and Boolean
truth values, appending the consequence to the explanation clause. It does
not directly explain arbitrary negative equality candidates. Candidate queues,
budgets, and final consistency checks belong to the SMT driver.

The optional bounded mode `--euf-diseq-ackermann` is off by default. It uses
functional-consistency clauses over existing atoms. For a binary function its
clause has the form:

```text
¬(a1 = b1) ∨ ¬(a2 = b2) ∨ (f(a1, a2) = f(b1, b2))
```

Distinct results imply that **at least one** argument pair differs. They do
not imply that every pair differs. The {source}`candidate builder
<src/smt/solver_engines.cpp#L991-L1050>` requires existing result and argument
equality atoms. The {source}`evaluator
<src/smt/solver_engines.cpp#L2378-L2410>` uses an assigned result disequality
to detect a unit clause or conflict. Width, queue, and per-symbol budgets
limit this subset. It is not a complete conflict-driven dynamic
Ackermannization implementation; [the literature survey](../papers/yices.md)
and [open lead](../leads/theory-lemmas.md) separate that remaining work.

## From consistency to a model

A consistent closure is input to model construction, not itself an SMT-LIB
model. {source}`build_typed_model <src/smt/typed_model_builder.cpp#L343-L350>`
constructs an owning `core::Model` with typed values and function tables.
Its {source}`ComponentModelBuilder <src/smt/typed_model_builder.cpp#L33-L60>`
uses a separate, direct-`TermId` `IncrementalCongruenceClosure`. That class's
name must not be confused with `FlatCongruenceClosure`, used during search.
The [model-construction chapter](models.md) follows Boolean completion,
evaluated function tables, and reverse preprocessing replay through a worked
example. It also describes the separate fragment adapter used for combined
QF_UFLIA models and the checks required before printing.

The alternate lazy checking path uses {source}`ExplainedCongruenceClosure
<src/smt/solver_engines.cpp#L2630-L2696>`. Its rebuilding and explanation
strategy is distinct from `FlatCongruenceClosure`. Neither route supplies
general quantified reasoning or arithmetic semantics; those obligations belong
to other components.

## Sources and implementation status

Robert Nieuwenhuis and Albert Oliveras, *Fast Congruence Closure and Extensions*,
Information and Computation **205** (2007), 557–580
{cite:p}`nieuwenhuis2007`, is the
primary algorithmic source.

| Basis | Implemented scope and deviations |
| --- | --- |
| Sections 3.1–3.5 | Currying/flattening, eager representatives, smaller-class merging, use lists, lookup, and pending merges. Primo adds indexed disequalities and can stop saturation at a conflict. |
| Section 3.3, backtracking paragraph | Motivation for undoable closure. Primo restores sparse lookup insertions; it does not implement the paper's timestamped lookup policy. |
| Sections 4 and 5.2–5.5 | Basis of the optional proof modes above. Default BFS, causal caching, alias materialization, and rollback details are distinguished in the local design notes. No shortest-explanation or exported-proof claim. |
| Sebastiani, *Lazy Satisfiability Modulo Theories* (2007), §6.10 and Proposition 2.3 | Opposite-polarity assignment filtering and exclusion of filtered propagation candidates. Primo adds explicit protection for Boolean argument terms; ghost-literal decision avoidance is not implemented. |
| [SMT-LIB 2.6 reference](https://smt-lib.org/papers/smt-lib-reference-v2.6-r2021-05-12.pdf), Definition 8 | Bool has exactly two distinct values. Frontend naming, registration, and the completion argument above are Primo's integration design for that semantic requirement. |
| Primo's local EUF/SMT design | SAT reason signs, Boolean-domain preparation, changed-term delivery, and model-scope protection. These are integration obligations, not algorithms attributed to the paper. |

`FlatCongruenceClosure` does not implement the paper's integer-offset extension.
[Linear real arithmetic](lra.md) covers the arithmetic solver.
[Theory combination](combination.md) explains equality exchange and the
additional congruence checks required when reconciling candidate models.
Lemma-retention policy remains in the [theory-lemma lead](../leads/theory-lemmas.md)
and [safe-forgetting lead](../leads/theory-lemma-forgetting.md).

### Tests tied to the invariants

| Obligation | Existing regression coverage |
| --- | --- |
| Flattening preserves shared applications and enables signature collisions. | {source}`Currying and flat-definition tests <tests/unit/euf_solver_tests.cpp#L414-L489>` also require lookup hits and congruence merges. |
| Disequality indexing avoids unrelated checks and preserves conflict reasons. | {source}`Index and signed-reason tests <tests/unit/euf_solver_tests.cpp#L638-L719>` check counters as well as answers. |
| A congruence explanation uses only sufficiently old edges. | {source}`Causal-edge and cache-bound tests <tests/unit/euf_solver_tests.cpp#L763-L845>` exercise both BFS modes. |
| Undo restores classes and allows a fresh explanation on the next branch. | {source}`Rediscovery and nested-pop tests <tests/unit/euf_solver_tests.cpp#L870-L924>`; {source}`definition rollback <tests/unit/euf_solver_tests.cpp#L1029-L1060>`. |
| Explanations entail the claimed equality, including after backtracking. | {source}`Naive-oracle comparison <tests/unit/euf_solver_tests.cpp#L1149-L1271>` rebuilds from the returned premises across all five explanation modes. |
| Paper-specific proof operations preserve their labels. | {source}`Path reversal and structural-class tests <tests/unit/euf_solver_tests.cpp#L1297-L1458>`. |
| Draining notifications does not resurrect them during pop. | {source}`Drain and generation tests <tests/unit/euf_solver_tests.cpp#L541-L627>`. |
| SAT levels preserve root facts and permit opposite assignments after rollback. | {source}`Production callback regressions <tests/unit/euf_callback_tests.cpp#L134-L209>`. |
| Boolean arguments retain both polarities and cannot take a third value. | {source}`Filtering and Boolean-domain tests <tests/unit/boolean_solver_tests.cpp#L2836-L2951>` include satisfiable and unsatisfiable cases. |
| Bounded disequality propagation requires existing atoms and uses the n-ary disjunction. | {source}`Ackermann propagation tests <tests/unit/boolean_solver_tests.cpp#L2276-L2393>`. |

These links identify tests to maintain when changing the mechanisms. The
[review ledger](../review-history.md#euf-chapter-checks) records this documentation
audit; solver changes still require the [correctness gates](../development/correctness.md).
