# Tableau state, models and rollback

This chapter explains the exact state shared by Real arithmetic and the rational relaxation used for integer solving: preparation, sparse rows, model values and restoration.

Start with the [LRA overview](../../lra.md) for route responsibilities and defaults.

## Setup and maintained state

### Representation and ownership

The input to arithmetic preparation is typed core terms plus their Boolean
abstraction. The tableau itself receives variable IDs, exact bounds and reason
literals. It does not parse SMT-LIB or inspect the SAT solver's internals.

| Representation | Owner, identity and lifetime |
| --- | --- |
| {source}`LinearPolynomial <src/theory/arithmetic/linear_polynomial.hpp>` | Owns a constant and a map from `TermId` to nonzero rational coefficients. Repeated occurrences add; exact cancellation removes an entry. |
| {source}`LraAtom <src/theory/arithmetic/lra_atom.hpp>` | Owns a normalized polynomial, relation `Le`, `Lt` or `Eq`, and right-hand-side rational. It is a value, not a SAT literal. |
| {source}`PreparedLraTheory <src/smt/solver_engines.cpp#L3332-L3388>` | Borrows the term manager and owns the base tableau, term-to-variable map, polynomial-to-slack map, and prepared atom vector. Each atom retains its own threshold and orientation. |
| {source}`LraVarId <src/theory/arithmetic/lra_tableau.hpp#L17>` | A numeric index local to a tableau and its structurally matching copies. It denotes an input leaf or an auxiliary polynomial value, independently of core and SAT IDs. |
| {source}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` | Owns assignments, bounds with reasons, basic/nonbasic membership, sparse rows, reverse occurrences, violated-variable set and undo records. |
| {source}`OnlineLraPropagator <src/smt/solver_engines.cpp#L6972-L7063>` | Borrows prepared data and owns a working tableau copied from the base, decision-level checkpoints, pending consequences and retained reasons. It reads the shared SAT assignment view. |

The term manager must outlive preparation and any interpretation of its IDs.
Tableau IDs survive pivots and rollback; being basic is a mutable property of
the variable. Structural insertion allocates vectors and maps, and pivots can
replace entire rows. References into these containers must not survive such
mutations. Copying a tableau preserves its numbering but creates independent
mutable arithmetic state.

The usual preparation installs equations before search. `append_atom` can
extend the prepared base between SAT calls, for example for a new interface
equality. This is separate from asserting a bound on the live tableau;
`add_row` is not a trailed search operation.

### Tableau equations and bounds

The tableau partitions its variables into **basic** variables, each defined
by a row, and **nonbasic** variables, whose values can be chosen independently
before recomputing the basic values. A row has the form

$$
x_b=\sum_{j\in N}a_{bj}x_j,
$$

where $N$ is the nonbasic set and every coefficient $a_{bj}$ is rational.
Write $\beta(x)$ for the current assignment and $l(x),u(x)$ for the active
lower and upper bounds when present. Bounds and assignments use delta
rationals. Each bound carries the negation of its asserting SAT literal as
its **reason**; those reasons will identify the premises of a conflict or
propagation clause.

The equations are identities introduced by arithmetic preparation. Pivoting
solves one row for a different variable and substitutes that equation into
the other rows. It changes the basis while preserving the represented
system. The linked `LraTableau` owns these rows, values and bound reasons;
[the sparse representation](#sparse-rows-and-reverse-occurrences) describes
their storage and update costs.

### From atoms to bounds

{source}`LinearPolynomial::from_term <src/theory/arithmetic/linear_polynomial.cpp#L12-L50>`
collects additions and constant scaling. Core `Times` stores a rational
coefficient and one child, so it represents linear scaling. Arithmetic leaves
are identified by their `TermId`; any uninterpreted application needs the
surrounding combination machinery to enforce its function semantics.
Unlowered `ite`, `div`, `mod` and `abs` raise an exception at this boundary.

{source}`normalize_atom <src/theory/arithmetic/lra_atom.cpp#L10-L37>` subtracts
the right side and moves the constant to the threshold. Preparation then
orders variables by `TermId` and divides every coefficient and the threshold
by the first coefficient, making that coefficient one. A negative divisor
reverses the inequality. This canonicalization
identifies proportional polynomials; it does not merge their SAT variables.

For example, these predicates can share the variable `s = x + y`:

| Predicate | Bound when true |
| --- | --- |
| `x + y <= 5` | `s <= 5` |
| `2*x + 2*y <= 10` | `s <= 5` |
| `-x - y <= -5` | `s >= 5` |
| `x + y <= 7` | `s <= 7` |

The thresholds and SAT reasons stay separate, including when a predicate is
assigned false. A single-variable polynomial instead becomes a direct bound:
`2*x <= 10` needs `x <= 5`, with no auxiliary variable or equation.
{source}`PreparedLraTheory::append_atom <src/smt/solver_engines.cpp#L3411-L3514>`
implements both savings: bounds on a single variable use that variable
directly, while bounds on proportional compound polynomials share one
auxiliary variable and its defining row. This keeps the tableau smaller and
puts related bounds on the same variable, where they can be compared directly.

Monniaux's [CAV 2009 paper](https://arxiv.org/abs/0904.3525), §2.1, describes
both ideas, but chooses a different standard form for the coefficients.
The paper clears denominators and removes common integer factors, choosing
the sign so the first coefficient is positive. Primo divides by the first
coefficient, so it becomes exactly one; the remaining coefficients may be
fractions. For example, assuming `x` comes before `y` in the variable order:

| Input | Paper's integer form | Primo's form |
| --- | --- | --- |
| `4*x + 6*y <= 10` | `2*x + 3*y <= 5` | `x + (3/2)*y <= 5/2` |

The paper's coefficients `2` and `3` have greatest common divisor one: this
is what “coprime integer coefficients” means here. Both forms express the
same inequality and give proportional polynomials a common representation.
In both cases the threshold must be scaled by the same factor as the
coefficients.

Leading-coefficient normalization gives Primo a simple way to share rows.
{source}`canonical_polynomial <src/smt/solver_engines.cpp#L3296-L3313>` uses
one coefficient as the divisor for every term and the threshold, skipping
those divisions when it is already one. It needs no separate
common-denominator or greatest-common-divisor computation across the vector;
individual rational operations still reduce their results.

The tradeoff is that this can introduce fractions into an initially integer
row. Coprime-integer normalization avoids those initial fractions, but clearing
input denominators can enlarge coefficients, and later pivots can introduce
fractions again. There is no recorded direct performance comparison of the
two normal forms; the [normalization review](../../../review-history.md#why-leading-coefficient-normalization-was-chosen)
records the original row-sharing objective and available evidence. Choosing
an initial normal form and maintaining integer rows throughout pivoting are
separate design questions.

For `Int` atoms, `--lia-primitive-int-rows` (on by default for QF_LIA, off
for QF_UFLIA) selects the coprime-integer form instead; see [integer
slacks](../../lia.md). Real atoms always
use the leading-coefficient form.

Primo also has a separate, optional
{source}`early atom canonicalizer <src/smt/preprocessing.cpp#L499-L599>` that
uses coprime integer coefficients in core terms, so equivalent predicates can
share a SAT atom. It is
{source}`off by default <src/smt/check_sat.hpp#L321-L324>`. Even when enabled,
tableau preparation still applies leading-coefficient division. The
{source}`early canonicalization note <docs/papers/lra-atom-canonicalization.md>`
describes that separate transformation and its evaluation.

Real equality is lowered to two inequalities before this path. Negating it
gives a Boolean choice between strict inequalities, handled by SAT. The
low-level `Eq` case's rejection of a false equality is therefore not a
restriction on surface Real disequality. See the
{source}`equality-lowering design <docs/arithmetic.md#L130-L173>` and
{source}`bound assertion boundary <src/smt/solver_engines.cpp#L3222-L3254>`.

### Exact numbers and strict bounds

{source}`util::Rational <src/util/rational.hpp#L202-L204>` uses an inline signed
64-bit numerator and denominator for values fitting its small representation.
Operations use checked wide intermediates and fall back to GMP rationals.
Results that fit return to the small representation: equal values must compare
and hash alike regardless of the arithmetic path that produced them.
This is the two-tier representation discussed by Monniaux §4 and Griggio
§2.5.2, with 64-bit rather than Monniaux's 32-bit small values. The paper's
floating-point solver and forced-pivoting algorithm remain an
[open lead: floating-point-guided basis selection](../../../leads/lra-floating-point.md).

The [compact rational storage lead](../../../leads/rational-storage.md) investigates
overlapping the small numerator and owned big-number pointer. It includes a
reproducible layout probe and the remaining ownership and evaluation questions.

A strict bound uses an exact symbolic infinitesimal.
{source}`DeltaRational <src/theory/arithmetic/lra_tableau.hpp#L24-L56>` stores
two rationals `(q, k)`, interpreted as `q + k*δ`, ordered lexicographically.
The rational part is compared first; the infinitesimal part breaks ties.
Thus $0<\delta<r$ for every positive rational $r$, and
$2-\delta<2<2+\delta$. There is no floating-point tolerance. For an unflipped Real atom:

| Atom | Assigned true | Assigned false |
| --- | --- | --- |
| `t <= c` | upper bound `(c, 0)` | lower bound `(c, 1)` |
| `t < c` | upper bound `(c, -1)` | lower bound `(c, 0)` |

The flipped orientation reverses the bound direction. All cases are centralized
in {source}`bound_assertion <src/smt/solver_engines.cpp#L3119-L3187>`.
Integer tightening is a separate branch of this function; the infinitesimal
interpretation here is for Real arithmetic. The basis is Dutertre–de Moura
§3.3, including the argument that a sufficiently small positive rational
can replace `δ` in a satisfying model.

## Sparse rows and reverse occurrences

The tableau stores each basic row as a sorted vector of nonzero coefficients,
with one entry per nonbasic variable. The actual entry is
{source}`RowEntry {var, coeff, occ_slot} <src/theory/arithmetic/lra_tableau.hpp#L290-L295>`.
Its reverse link satisfies:

```text
For entry e in rows_[b]:  occurs_in_[e.var][e.occ_slot] == b
```

`occurs_in_[v]` lists the basic rows containing `v`. Its order is immaterial.
`occ_slot` locates the column occurrence, not the row entry. Removing an
occurrence swaps the last column element into its place and repairs that
element's row backlink using binary search. Thus
{source}`unlink_occurrence <src/theory/arithmetic/lra_tableau.cpp#L2004-L2024>`
avoids scanning the column, but repair is logarithmic in the moved row's
length. It is not the constant-time unlink of a doubly linked matrix.

```{figure} ../../../_static/lra-tableau-occurrence-index.svg
:alt: Sorted tableau rows linked by occ_slot to unordered column occurrence lists, with swap-pop removal and backlink repair.

Sorted rows and their reverse occurrence index. Each row entry's `occ_slot`
locates its entry in a column list; `occ` abbreviates `occurs_in_`.
The {source}`editable TikZ source <docs/figures/lra-tableau-occurrence-index.tex>`
is retained with the figure.
```

The distinction among the thesis representations matters. King §2.2.7 uses
unsorted, doubly linked rows and columns, with a dense scratch map for row
addition. Griggio §2.5.2 discusses sorted contiguous coefficient arrays.
Bromberger §7.1.4 uses sorted doubly linked rows, unordered columns and a
common denominator per row. Primo uses sorted vectors, individual rational
coefficients and the indexed occurrences above. These designs support the
same sparse operations with different allocation and removal costs.

{source}`pivot <src/theory/arithmetic/lra_tableau.cpp#L143-L315>` merges sorted
rows, drops exact cancellations and updates occurrences only when membership
changes. The coefficient merge is linear in the two row lengths; occurrence
repairs and arbitrary-precision arithmetic add their own costs. Updating a
nonbasic assignment visits its dependent rows through `occurs_in_`, but the
current {source}`update <src/theory/arithmetic/lra_tableau.cpp#L97-L141>` still
searches each affected row for the coefficient. The occurrence index is not
a direct pointer to that coefficient. The
[column-to-row coefficient lead](../../../leads/lra-column-coefficients.md) lists
every such lookup and the candidate representations.

The 32-bit occurrence slot assumes fewer than `2^32` occurrences in a column.
The representation and its capacity assumption are distinct from SAT literal
encoding. Hash-consing core terms does not bound either quantity.

## Model construction

| Contract | Meaning |
| --- | --- |
| Input | A feasible tableau for the accepted arithmetic assignment, prepared term-to-variable mappings, and model reconstruction information. |
| Result | Concrete rational values incorporated into the typed model. |
| Default method | Instantiate the symbolic infinitesimal with a sufficiently small positive rational and reconstruct the model. |
| Correctness condition | All selected arithmetic literals remain true; tableau equations and required shared-value relationships are preserved. |

### From delta rationals to rational values

For a satisfying Real model,
{source}`choose_concrete_delta <src/smt/solver_engines.cpp#L3516-L3561>`
finds a positive rational small enough to preserve the assigned bounds and
evaluates `q + k*δ` at that value. Where a crossing point limits `δ`, it
chooses half the smallest positive limit; otherwise it uses one. Mixed
theory model construction additionally preserves required shared-value
orders. Reconstruction of eliminated variables and Boolean values remains
part of the model builder; a feasible tableau alone is not the printed model.

For example, $x>2$ and $x\le3$ can have the symbolic assignment
$x=2+\delta$. Preserving its encoded upper bound requires $\delta\le1$.
The crossing limit is $1$, so the choice $\delta=1/2$ yields $x=5/2$,
which satisfies both original inequalities. Each variable uses the same
chosen value of $\delta$, preserving rational linear equations.

The {source}`Real model construction path <src/smt/solver_engines.cpp#L3620-L3651>`
uses `real_model_values_from_lra` to map prepared term IDs to concrete values
and `build_qf_lra_model` to pass those values, Boolean atom assignments and
model conversions to the typed model builder. These values are an output
assignment, not new entailed bounds. For mixed theories,
[model reconciliation](../../combination.md) also has to agree on shared values.

(rollback-and-model-values)=
## Incrementality and rollback

`push()` returns an undo-trail position. With the default sparse policy,
{source}`pop <src/theory/arithmetic/lra_tableau.cpp#L1896-L1966>` restores
bounds and rebuilds the set of violated basic variables. It retains the
current basis and assignment. Relaxing bounds preserves nonbasic feasibility,
and the assignment still satisfies the retained equations. Basic feasibility
is established by `check()`, not by the act of popping alone.

This is an implementation choice within the fixed-equation approach. The
technical report §3.2.4 describes saving a successful assignment;
Bromberger §7.1.3 compares that with keeping the current assignment and
rechecking. Primo neither restores an old assignment against a changed basis
nor unconditionally runs `check()` inside `pop()`. SAT decision rollback and
SMT-LIB assertion scopes remain different operations. The latter's reuse
design is described in the {source}`retained LRA session note <docs/assumption-incremental-lra.md>`.

The [SOI objective](search.md#optional-sum-of-infeasibilities) depends on which basic variables currently
violate which bounds. Restoring bounds alone would leave its cached directions
and row describing the abandoned state. Primo therefore trails assignments,
pivoted rows, and objective changes in SOI mode, as selected by
{source}`trails_assignments_and_pivots <src/theory/arithmetic/lra_tableau.hpp#L843>`.
Each {source}`FRowTrailEntry <src/theory/arithmetic/lra_tableau.hpp#L790-L800>`
saves the previous objective row together with the changed variable's previous
direction. Undo restores both, and `pop` rebuilds the occurrence index after
row restoration and the violated-variable set after all undo operations.
This is Primo's current restoration strategy; an alternative could recompute
the objective from the retained basis and restored bounds.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lra.md#sources-and-implementation-status) and [regression obligations](../../lra.md#invariants-exercised-by-existing-tests). The sections here retain their algorithm-specific derivations, examples and source links.
