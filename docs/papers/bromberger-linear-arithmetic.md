# Bromberger: integer search, cubes and preprocessing

Martin Bromberger, *Decision Procedures for Linear Arithmetic*,
PhD thesis, Universität des Saarlandes, 2019 {cite:p}`bromberger2019`.
Section numbers below refer to the thesis, not its companion conference papers.

This guide carries the existing thesis review into the replacement book.
The longer {source}`literature record
<docs/papers/bromberger-linear-arithmetic-thesis.md>` already records the
August 29 reading, September 25 source refresh, ablations and research agenda.
LRA and theory combination also cite it. Here the relevant sections are
connected to the integer and [preprocessing](../implementation/preprocessing.md)
chapters.
The thesis covers both implemented engineering choices and possible extensions;
its SPASS-IQ and SPASS-SATT evaluations remain evidence about those
implementations, not measurements of Primo.

## Where each chapter belongs

| Thesis section | Connection to the manual | Implementation boundary |
| --- | --- | --- |
| Chapter 4, especially Corollary 4.2.2 and §§4.3.2–4.3.3 | Find an integer point by fitting a unit cube inside the feasible region. | Optional uniform cube test and difference refinement now implemented for pure QF_LIA; see {ref}`lia-cubes`. General mixed cubes remain deferred. |
| Chapter 5, especially §§5.4–5.7 | Implied equalities, theory combination and bounded directions. | The [equality-basis lead](../leads/lra-equality-basis.md) remains open; current local equality rules are not the complete basis algorithm. |
| Chapter 6, especially Definition 6.2.1, Lemma 6.2.3 and Corollary 6.3.6 | Why bounded row expressions need not bound variables, and how transformations address divergent integer search. | Not implemented. Primo retains a finite branch budget and can return `unknown`. |
| §§7.1.3–7.1.4 | Rollback and sparse arithmetic representation. | [LRA](../implementation/lra.md) maps the relevant choices and deviations to Primo's tableau. |
| §§7.2.3–7.2.4, Figures 7.21–7.22 | Distinguish procedures before branching from procedures at each branch node. | A comparison for [Primo's stage schedule](../implementation/lia.md#when-integer-reasoning-runs), not the schedule Primo implements. |
| §§8.2.2–8.2.4 | Term sharing and arithmetic flattening; `ite` transformations; variable definitions; pseudo-Boolean simplification; CNF construction. | The [preprocessing chapter](../implementation/preprocessing.md) maps the implemented transformations and their reconstruction obligations; the thesis's broader preprocessing suite is not implied. |

(efficiency-review-checklist)=
## Efficiency review checklist

The [8 November review](../evidence/arithmetic-efficiency-20261108.md) rereads the
implementation arguments in §§7.1–7.2, the partial/full verification boundary
in §8.1.1, and the constant if-then-else (CITE), general ITE and CNF mechanisms in §§8.2.3–8.2.4. It also checks
the NEC and ring-family discussions in §8.2.5. These sections explain concrete
engineering choices; they do not make their SPASS-SATT measurements a prediction
of Primo performance.

| Thesis mechanism | Current Primo representation/status | Review consequence |
| --- | --- | --- |
| §7.1.4, pp. 217–219: efficient small/big arithmetic | Implemented in {cubesource}`Rational <src/util/rational.hpp>` as inline 64-bit numerator/denominator with GMP promotion. | The small-number optimization is already present. A library switch alone does not add it. |
| §7.1.4, pp. 221–225, Figure 7.17: sorted row merging, unordered columns, shared row denominator | Partially implemented. {cubesource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` has sorted row vectors, `RowEntry::occ_slot` and unordered column occurrence vectors. Each entry still owns a rational coefficient. | The coefficient traversal is a linear two-way merge. Occurrence deletion adds backlink-repair searches in other rows, as explained below. A common denominator per row is a distinct missing representation, relevant to measured large-number normalization. |
| §§7.1.2, 7.1.4: violated-variable heap, buffered assertion updates and cached `VarInfo` | Adapted subset. Primo maintains a `std::set` of violated IDs, scans it for heuristic selection, and rebuilds it at `pop`. It has no full thesis heap/`VarInfo` implementation. | Maintaining changed information can avoid scans, but fresh profiles must determine priority; the current set already avoids scanning every variable at every pivot. |
| §7.1.3: retain the current tableau/assignment while restoring bounds | The bounds-only rollback principle is present outside SOI; repair occurs at the caller's check boundary. SOI still restores its additional state. | This is not a missing wholesale backtracking scheme. Incremental violation maintenance is a smaller open cost. |
| §§2.7.4, 7.2.4: simultaneous rounding and original-constraint validation | Not implemented as a standalone cheap test. Patching preserves feasibility one move at a time; cubes round a newly found center. | Neither substitutes for rounding the current model, as the example below shows. |
| §§7.2.3–7.2.4: bounded numerical propagation with integer tightening at every node | Missing from the internal DFS. {cubesource}`try_branch <src/theory/arithmetic/lia_branch_and_bound.cpp>` asserts a split and checks simplex. | Existing propagation to already-named SAT literals is a different scope; §8.1.2 makes that distinction explicitly. |
| §7.2.2: most-fractional branching and preferred child | Not implemented. The DFS uses the first fractional variable and tries the low side first. | This is a possible heuristic experiment; it cannot accelerate a prefix still processing root Gomory cuts. |
| §8.2.3: complete CITE/ITE preprocessing pipeline | Partial CITE simplification and structural rewrites are implemented. Two-CITE equality simplification, full shared-tail compression, shared-monomial lifting, leaf/GCD bounding and ITE reconstruction are absent. | The paper describes interacting techniques for NEC and rings; importing one in isolation does not reproduce the full pipeline. |
| §§5.5–6.4: bounded directions and mixed-integer coordinate transformations | Unimplemented. GCD checks and ordinary Gomory cuts are not this procedure. | Complete integer reasoning needs integer-preserving reconstruction and incremental explanations; a search-node cap is not a replacement. |

A column occurrence in Primo identifies a basic row, not the coefficient's
location in that row. Both assignment updates and freedom-interval queries
search the affected rows. Their work can therefore be proportional to the sum
of those row lengths, in addition to exact-arithmetic costs. The column index
alone does not make them linear solely in the number of affected rows. The
existing [column-coefficient lead](../leads/lra-column-coefficients.md) records
this representation tradeoff; linked column entries in the thesis supply the
coefficient directly.

Primo's {cubesource}`unlink_occurrence <src/theory/arithmetic/lra_tableau.cpp#L2025-L2045>`
removes a column entry by swapping in the last occurrence. It then binary-searches
that moved occurrence's row to repair the backlink. Thus the sorted coefficient
merge has linear traversal cost, but total pivot/index maintenance is not
guaranteed linear solely in the two merged row lengths. Figure 7.17 instead
assumes constant-time insertion/deletion through linked entries.

### Contracts for the smaller optimization candidates

These are implementation checklists derived from this review. The patching
interval cache is implemented with qualification pending; the CITE endpoint
fast paths, shared-model bookkeeping and row representation below remain
proposals. The measured findings are in the evidence page.

**Patching interval cache.** The owner is `patch_integer_variables`, with owning
`LraTableau::FreedomInterval` values keyed by `LraVarId`; clearing the map
defines the candidate-selection epoch. Patching does not pivot, and its
bounds and assignment remain unchanged until a move is accepted. An interval
can be reused for all candidates in that epoch. Every accepted move invalidates
the epoch, including intervals of other nonbasics whose dependent rows changed.
Cache only values, not borrowed row pointers. Preserve scoring and tie order.
The intended operation-count improvement is from one column query per candidate
to one per distinct moved variable per epoch. Tests must include repeated
variables across targets and stale-cache detection after an accepted move;
request/computation/hit counters must show that the mechanism is used.
These counters and the reuse/invalidation regressions now exist; see
[the representation and worked example](../implementation/lia.md#patching-interval-reuse).
This cache is Primo's engineering adaptation of §7.1.4's maintenance principle,
not the thesis's variable-information implementation. Measured acceptance remains
pending.

**Shared-model trial bookkeeping.** The owner would be
{cubesource}`diversify_shared_model <src/theory/arithmetic/lra_tableau.cpp#L1554-L1713>`'s
scratch model, using exact `DeltaRational` value-bucket counts. Update affected
shared values during a trial and undo them on rejection; build class vectors
for export afterwards. The count includes singleton buckets. Preserve the
nondecreasing number of classes, forbid reintroducing previously separated
forbidden pairs, and preserve deterministic candidate/output order. This is an
engineering inference from the existing
[mutation contract](../implementation/combination.md#when-model-mutation-runs),
not Bromberger's complete equality-basis algorithm. A stress test should grow
the total interface while keeping the affected column small, with rejected
moves, duplicate input IDs, infinitesimals and several changed shared values.
Count regrouped/changed values and allocations; the benefit can disappear when
a move affects the entire interface.

**CITE uniformity decision.** The owner is `csimp_against_constant`; its pass
already caches sorted unique leaf sets. For `<` or `<=`, evaluating the two
endpoints determines whether all leaves agree. For equality against a constant,
a singleton can be uniformly true, absence from the sorted set is uniformly
false, and membership in a larger set is mixed. Retain exact arithmetic and the
nonempty-leaf-set invariant, with an explicit guard before accessing endpoints.
Keep the full sets for membership and possible future two-CITE equality rules.
The intended comparison costs per cache miss are constant for inequalities and
logarithmic for equality, instead of linear in the leaf-set size. Tests need
negative/zero scales, strictness, mixed outcomes, sharing and a large-leaf probe;
leaf-comparison counters should distinguish this work from cache-key comparisons.
Sorted sibling sets can also be merged linearly; caching every subterm alone
does not bound the sum of all cached leaf-set sizes by the input DAG size.

**Common-denominator rows.** A future row owner would store integer numerators
$n_j$ and a positive denominator $d$, representing coefficient $n_j/d$.
Normalize their joint gcd without changing the row equation. Sorted variable
IDs and valid occurrence backlinks remain invariants. `add_row`, pivot/merge,
assignment updates, bound queries and SOI trail snapshots all need the new
representation; changing only the pivot kernel is insufficient. Figure 7.17
uses integer scaling and a merge, and the thesis notes that the actual
implementation also minimizes common denominators. A linear coefficient traversal does
not establish linear total index-maintenance cost or bit complexity, nor does
it prevent numerator growth. Before
adoption, compare exact coefficients and assignments after pivot sequences and
SOI rollback with the current rational-row implementation. Add counters for
row normalizations, large-number operations and coefficient bit sizes, and
separate representation effects from changed pivot heuristics.

(simultaneous-rounding-gap)=
### Simultaneous rounding is a separate test

The thesis's simple rounding (§2.7.4, pp. 46–47) rounds the semantic integer
variables simultaneously and checks the result against the original constraints.
It does not require the path to that candidate to stay feasible. Consider
integer variables $x,y$ constrained by

$$0\le2x+y\le1,\qquad0\le x+2y\le1.$$

Let the two nonbasic slack expressions be $s_1=2x+y$ and $s_2=x+2y$.
At $s_1=s_2=1$, the basic semantic values are $x=y=1/3$. This is rationally
feasible. Simultaneously rounding $x,y$ to zero gives a valid integer model.
Changing only $s_1$ to zero instead gives $(-1/3,2/3)$; changing only $s_2$
to zero gives $(2/3,-1/3)$. Both still have two fractional semantic variables.
Since each slack ranges over $[0,1]$, those are the only different integer
single-slack moves. Strictly improving integral patching cannot take either.
A uniform cube also cannot fit across either row's width-one interval: its
row norm is three and its two margins sum to three.

This is an example of a possible retained basis/assignment after bounds are
relaxed, not a claim that a fresh solve of this conjunction starts at $1/3$;
zero is already a valid initial assignment. Existing cuts or DFS can still find
the model. The example distinguishes the cheap model tests.

An implementation would round original semantic integer coordinates in owning
scratch storage, validate the original row bounds and strictness, reconstruct
dependent values, and adopt only a fully checked model. It must not round
auxiliary slacks independently. Failure must preserve assignment, basis,
bounds and explanation state. Tests should construct the retained state, test
false/strict atoms and failed adoption, and validate the printed original model.
The thesis repeats rounding at branch nodes; the overhead and scheduling need
separate measurement before that broader variant is adopted.

### Scheduling and guards need their actual preconditions

The theory-verification description (§8.1.1, p. 251) checks partial Boolean
models for rational feasibility and reserves integer feasibility for full
models. This is consistent with Primo's partial/full boundary. It does not
imply doing a full integer search after every newly asserted constraint.

The thesis discusses skipping cube attempts on Boolean-width variables, with
an explicit trivial-unit-cube exception in the Chapter 4 discussion. A blanket
rule copied from the later width-one summary would skip a closed unit box
such as $0\le x\le1$, which does contain a unit cube. For Primo's closed cube
variant, any cheap rejection should instead be justified from the actual
strengthened bounds after fixed-coordinate substitution and mode-specific
margins. Uniform and difference-aware margins need not have the same rejection
conditions. This is a proposed optimization guard, not a correctness defect in
the current conservative cube test.

(bromberger-unit-cube)=
## What a unit cube proves

Let the current conjunction of non-strict inequalities be $Ax\le b$, with
integer variables $x$. Write $a_i$ for the coefficient vector of row $i$ and
$\lVert a_i\rVert_1=\sum_j|a_{ij}|$. A cube of edge length one around a
rational point $z$ consists of the points $z+d$ with $|d_j|\le1/2$.
The largest value of row $i$ over that cube is

$$a_i^Tz+\tfrac12\lVert a_i\rVert_1.$$

Corollary 4.2.2 therefore turns cube containment into rational feasibility:

$$a_i^Tz\le b_i-\tfrac12\lVert a_i\rVert_1\quad\text{for every row }i.$$

If this strengthened system has a solution, round each coordinate of $z$ to
a nearest integer. Rounding moves it by at most $1/2$, so the rounded point
lies in the cube and satisfies the original inequalities. For instance,
$3x+y\le9$ is strengthened to $3x+y\le7$, because its row norm is $4$.
The margin of $2$ covers every simultaneous rounding change within half a
unit in each coordinate.

An inconsistent strengthened system says only that no such cube fits.
It does not refute the original integer problem. For example, the equality
$x=0$ has an integer solution, but its two inequalities become
$z\le-1/2$ and $z\ge1/2$ after strengthening. A fixed variable can instead
be substituted before applying the test in the remaining dimensions.
Section 5.7 explains how eliminating implied equalities can help with this
dimensional obstruction. Over integers, that elimination must preserve
divisibility: replacing $2x=y$ by $x=y/2$ without requiring even $y$ changes
the set of integer solutions.

Section 4.3.3 uses a flat cube for mixed arithmetic: it extends only along
integer coordinates, so the tightening uses only their coefficients.
Auxiliary tableau variables must not accidentally become independent rounding
coordinates. The optional pure QF_LIA cube test now identifies the original
integer coordinates, interprets normalized rows in those coordinates, and
reconstructs dependent values consistently; see {ref}`lia-cubes`. Applying this
to mixed integer/real arithmetic remains future work.

(bromberger-bounded-search)=
## Why the integer search needs more than bounded rows

Chapter 6 distinguishes a system $l\le Dx\le u$, in which each displayed
linear expression has two bounds, from a system in which each variable has
two bounds. The former need not have a finite search space for branching on
the original variables.

The [LIA chapter's incomplete example](../implementation/lia.md#budgets-and-incomplete-cases)
has $2\le4x+4y\le3$. The sum is bounded, but $x$ and $y$ can move arbitrarily
far in opposite directions while satisfying its real relaxation. To see the
role of coordinates, set $u=x+y$ and $v=y$. The inverse is $x=u-v$, $y=v$,
so this change preserves integer assignments in both directions. The
constraint becomes $1/2\le u\le3/4$; $v$ disappears. Now the constrained
integer variable is bounded, and neither branch $u\le0$ nor $u\ge1$ is
feasible. This small example illustrates the purpose of a coordinate change;
Primo does not currently perform this transformation.

The thesis's general route first separates bounded from unbounded row
directions and applies its Double-Bounded reduction (§6.3). It then uses a
Mixed-Echelon-Hermite transformation (§6.2) that preserves the distinction
between integer and rational variables. In the resulting triangular system,
each variable is bounded or has a zero column (Lemma 6.2.3). Section 6.2's
integer transformations use unimodular matrices: integer matrices with
integer inverses, so the coordinate change does not lose integrality.
Solution conversion is part of the procedure, not an optional model-printing
step.

This is a concrete literature direction for the completeness gap. It is not
implemented by Primo's row-local GCD check or its mixed Gomory cuts. Designing
it would require reading the full transformation and incremental algorithms,
then specifying matrices, invariants, explanation and model conversion, and
rollback before coding. This guide does not claim that implementation audit.

## When the thesis runs its procedures

Section 7.2.4 separates two schedules. Before constructing the branch tree,
SPASS-IQ solves the rational relaxation, tries simple rounding, tries the
unit cube test when its guards allow it, and analyzes bounded directions.
It applies the bounding transformations when needed, then chooses branching
preferences. At each branch node it performs bounded propagation, simplex
and simple rounding before selecting another split (Figures 7.21–7.22).

Primo's {source}`integer completion
<src/smt/solver_engines.cpp#L8636-L8684>` instead runs patching, GCD checks and
cuts once before its {source}`internal branch search
<src/theory/arithmetic/lia_branch_and_bound.cpp>`. Each branch asserts a bound
and checks the tableau; it does not repeat those three prechecks or run the
thesis's bounded-direction transformations. Nor is simple rounding the same
as Primo's patching: the thesis rounds a candidate and checks it, whereas
patching restricts each move to preserve feasibility.

The thesis calls these before-branching procedures “preprocessing” inside
its arithmetic solver. That scope is different from Primo's formula
preprocessing and tactic pipeline. Chapter 8 addresses the latter kind of
work and should be read separately when rewriting those chapters.

## Reading and evidence limits

The November efficiency review additionally checks the algorithm/data-structure
text and family-specific preprocessing arguments listed in the efficiency
checklist above. It does not claim a cover-to-cover reading or a completed
implementation audit of Chapter 6’s incremental transformations.

For this guide, the supplied PDF was read at Chapter 4's cube transformation
and unit/mixed-cube tests; §5.7; Chapter 6's overview and selected definitions,
transformations and termination results; §§7.2.3–7.2.4; and selected passages
from §§8.2.2–8.2.4. Earlier readings of §§7.1.3–7.1.4 are recorded with the
LRA chapter. Chapter 8's complete pass-by-pass review and Chapter 6's
incremental algorithms remain further reading before an implementation claim.

Benchmark-family overlap makes the thesis useful for forming hypotheses,
but neither matching family names nor a longer timeout makes its ablation
gains a bound on Primo's gains. Corpus revisions, configurations, interactions
between mechanisms and resource limits differ. New implementations need
their own correctness and performance evidence.
