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
