# Cube models, divisibility checks and cuts

These are distinct integer-completion stages: cubes attempt a checked model, the greatest-common-divisor (GCD) test can refute divisibility, and a Gomory cut excludes a fractional point. The [scheduling chapter](repair.md#when-integer-reasoning-runs) explains their order.

Start with the [LIA overview](../../lia.md) for integration with SAT and default settings.

(lia-cubes)=
## Optional cube model attempt

`--lia-cube=uniform` enables the sufficient model test in Bjørner and
Nachmanson, CAV 2024, §4.2. Each **coordinate** is the value of an original Int
term mapped to a tableau variable. Substitute coordinates fixed to integer values
and write the remaining values as $z=(z_1,\ldots,z_n)$. These are rounded by
the test; an auxiliary such as $s=2z_1+3z_2$ is evaluated from them, not rounded
independently. Fixed coordinates keep their substituted values. Write $az=\sum_j a_jz_j$, with integer coefficients
$a_j$ and integer threshold $b$ after the normalization described below.

For each inequality $az\le b$, the helper solves the strengthened inequality
$az\le b-\frac12\sum_j|a_j|$ in a separate rational tableau. A feasible
solution is the **center**. Moving at most $1/2$ in every coordinate changes
the row's left side by at most $\frac12\sum_j|a_j|$, so the axis-aligned unit
cube around that center satisfies the original inequality. Rounding each
coordinate with $R(t)=\lfloor t+1/2\rfloor$ selects an integer point in this
cube and therefore satisfies the original rows.

`--lia-cube=difference` leaves difference rows $x-y\le k$ with integer $k$
unstrengthened. Monotonicity and $R(y+k)=R(y)+k$ preserve these rows even at
negative rounding ties. General octagon refinements are not implemented.
QF_LIA defaults to `uniform`; `difference` remains an explicit alternative.
QF_UFLIA ignores these controls and retains its existing pipeline. The
[default-profile update](../../../evidence/qf-lia-defaults-20261119.md) records the
combined policy and its qualification status.

The numerical copy {cubesource}`LiaCubeProblem
<src/theory/arithmetic/lia_cube.hpp>` contains the ordered integer
coordinates defined above, sparse definitions of the original tableau variables in those coordinates,
and the original basic-variable flags. {cubesource}`SMT preparation <src/smt/solver_engines.cpp>` exports it from the
prepared `PreparedLraTheory::base_tableau`, before any pivots or cuts, for a fractional completion input; it contains
no terms or references into preparation storage. The helper obtains active
bounds from the rationally feasible working tableau. It substitutes coordinates fixed to
exact integer values, rescales each row by a positive factor to obtain coprime integer coefficients, and applies delta-aware floor to normalize strict
or fractional thresholds over integer values. Slacks are affine expressions,
never independent integer coordinates for computing margins. A coordinate fixed to a fractional value skips the attempt, leaving refutation to the existing stages.

{cubesource}`try_cube_model <src/theory/arithmetic/lia_cube.cpp>` builds a
scratch Bland tableau. {cubesource}`check_bounded
<src/theory/arithmetic/lra_tableau.cpp>` returns
`Feasible`, `Infeasible`, or `LimitReached`; its finite repair-step budget also
counts [sum-of-infeasibilities updates](../lra/search.md#optional-sum-of-infeasibilities)
for other callers. The cube helper uses [Bland pivot selection](../lra/search.md#pivot-selection-policies).
`--lia-cube-steps=1000` is the initial untuned budget; zero permits only an
already feasible center. A step cap does not bound exact-arithmetic bit costs
or construction time. It adds no new global cancellation mechanism.

A feasible center is rounded and checked against every original bound and
all integer obligations, including enabled integer slacks. Adoption reconstructs
the original numerical basis with the same variable IDs, sets its nonbasic
semantic values before asserting original bounds and reasons, and checks that
no repair is needed. There are no private fixing bounds in the adopted model.
Only success replaces the working tableau. Adoption creates a new bound trail;
checkpoint tokens from the replaced copy must be discarded. SMT completion
passes a private copy and reads its winning model. Failed, skipped, budgeted, or
rejected candidates preserve its assignment, basis, bounds and proof state;
no strengthened-system conflict enters a theory explanation. After such a failure, completion continues with cut generation and integer
branching. The call occurs once after patching
and the basic GCD check, before any cuts, and skips an already integral model.

For $m$ original coordinates, snapshot construction sorts them in
$O(m\log m)$ comparisons;
sparse export, normalization and candidate checks then take linear sparse
work apart from exact-arithmetic costs. Scratch/adoption construction also
uses the tableau API's row sorting; no total linear-time construction bound is claimed. Each fractional enabled completion allocates
a numerical copy and may allocate extra tableaux; no speedup is assumed. {cubesource}`Counters <src/util/statistics.cpp>` named `lia.cube-*`
separate calls, skips, constraints, fixed coordinates, difference exemptions,
pivots, centers, infeasible rational subproblems, exhausted limits, rejected candidates and models.
`lia.cube-microseconds` times the helper, excluding SMT snapshot construction.
`lia.branch-nodes` counts subsequent private split nodes. A nonzero rejected
candidate counter requires investigation; it is not evidence of a successful
cube mechanism. {cubesource}`Unit tests <tests/unit/lia_cube_tests.cpp>` cover the
paper example, thin feasible failures, negative ties, scaled difference rows,
fixed substitution, strict bounds, large exact coefficients, budget preservation
and rejected adoption. The QF_LIA integration test checks a false atom and
printed-model reconstruction. See {ref}`the qualification and measured comparison <lia-cube-evidence>`.

(lia-gcd)=
(detecting-impossible-row-divisibility)=
## Detecting integer-infeasible rows with GCDs

The equation $2x-2y=1$ has no integer solution because its left side is
always divisible by $2$. More generally, for integer coefficients $C_i$
and integer variables $z_i$, every value of $\sum_i C_i z_i$ is divisible
by $g=\gcd(C_1,\ldots,C_k)$. An equation $\sum_i C_i z_i=R$ is therefore
impossible when $g$ does not divide $R$.

This is the classical divisibility criterion for a linear Diophantine
equation, a consequence of Bézout's identity. It predates Bjørner and
Nachmanson: Pugh's [*Omega test*](https://www.cs.umd.edu/~pugh/papers/omega.pdf),
§2.1 of the expanded 1992 paper, already uses it when normalizing equalities.
Griggio's 2009 thesis, §2.6.1, Algorithm 2.6, step 4, applies the same
criterion in an SMT equation solver. Bjørner and Nachmanson §4.3 is Primo's
reference for applying the test to **tableau rows after fixed variables
have been substituted**, not for the origin of the criterion.

The read-only procedure {source}`gcd_inconsistent_row
<src/theory/arithmetic/lia_gcd_consistency.cpp>` prepares each row as follows:

1. Rewrite $x_b=\sum_j a_jx_j$ as $x_b-\sum_j a_jx_j=0$.
2. Substitute variables fixed by equal lower and upper bounds with zero
   infinitesimal part, including $x_b$ if it is fixed. The remaining equation
   is $\sum_i c_i z_i=r$, with possibly rational coefficients and residual.
3. Require every remaining variable to belong to `integer_vars`. Otherwise
   skip the row: a noninteger auxiliary variable could absorb a fractional
   value. Pivoting can put such a variable on either side of the row.
4. Scale the coefficients **and the residual together** to integers $C_i,R$
   using `primitive_integer_scale`, and check whether their coefficient GCD
   divides $R$.

If no variables remain after substitution, the row supplies no additional
integer test; rational consistency has already been checked. Otherwise,
failure of divisibility proves a conflict. Passing the test establishes only
that the isolated equation has an integer solution when the remaining bounds
are ignored. It does not establish consistency of the whole tableau.

For $2x-2y=1$, normalization can represent $s=x-y$ fixed to $1/2$.
Substitution leaves $x-y=1/2$. Scaling the coefficients and residual by
$2$ gives $2x-2y=1$; the coefficient GCD is $2$, which does not divide $1$.
Scaling only the coefficients would destroy this argument.

The conflict depends on the bounds used to fix variables. Both bound reasons
are collected for each fixed variable; row equations and the declared integer
sorts are unconditional invariants of the prepared problem. The scan builds
a membership bitmap and visits basic rows with exact rational/GCD operations;
numeric cost depends on coefficient size, not just row length.

The [bounded-variable GCD extension](../../../leads/lia-extended-gcd.md) is an open
lead carried forward from the original register. It can reject an interval
containing no admissible residue even when no variable is fixed. Primo does
not implement that extension or a general Diophantine equation solver.
The {source}`GCD regressions
<tests/unit/lia_gcd_consistency_tests.cpp>` cover fractional normalization,
bounded-but-unfixed rows, and noninteger auxiliary variables in both basic
and nonbasic positions.

(lia-cuts)=
## Excluding a fractional point with a Gomory cut

{source}`apply_gomory_cuts <src/theory/arithmetic/lia_gomory_cuts.cpp>` uses
King's thesis §4.4.3, Lemma 4.1, equation (4.9). This mixed cut requires the
basic variable to be integer in every solution. It treats nonbasic variables
as continuous, so their bounds may be fractional and they need not belong to
`integer_vars`.

The helper {source}`find_eligible_row
<src/theory/arithmetic/lia_gomory_cuts.cpp#L93-L125>` chooses the row from
which to derive a cut. It takes the first eligible variable in the caller's integer list:
it must be basic, its current value must be fractional with **zero
infinitesimal part**, and every nonbasic variable in its row must sit exactly
at a lower or upper bound. A variable merely having a bound is insufficient.
An ineligible row is skipped in its entirety. For a variable at both bounds,
the lower bound is chosen.

Write the row as $x_b=\sum_j a_jx_j$ and let $v=\beta(x_b)$, where
$\beta$ is the current tableau assignment. Define
$f=v-\lfloor v\rfloor$, so $0<f<1$. Let $b_j$ be the rational part of the
bound occupied by $x_j$. Subtracting the bound-site assignment gives

$$x_b-\lfloor v\rfloor=f+\sum_j a_j(x_j-b_j).$$

The left side must be an integer. Group the nonnegative contributions in
the sum into $P$, and write its nonpositive contributions as $-N$, with
$P,N\ge0$. If $P-N>0$, integrality forces $P-N\ge1-f$, hence $P\ge1-f$.
Otherwise it forces $P-N\le-f$, hence $N\ge f$. In either case,

$$\frac{P}{1-f}+\frac{N}{f}\ge1.$$

This is the cut. In terms of the stored row coefficients, it is
$\sum_j C_j(x_j-b_j)\ge1$, with:

| Occupied bound | Sign of $a_j$ | $C_j$ |
| --- | --- | --- |
| Lower | Positive | $a_j/(1-f)$ |
| Lower | Negative | $-a_j/f$ |
| Upper | Positive | $-a_j/f$ |
| Upper | Negative | $a_j/(1-f)$ |

The rational-bound reasoning also applies when a selected bound is strict:
forgetting its infinitesimal gives a weaker bound. The row-selection requirement
that the basic value has zero infinitesimal ensures that its $v$ is the
rational bound-site sum used above. It skips a basic value such as
$3-\delta$ entirely; computing a fractional part from its rational part
would give zero and make the divisions invalid.

### Turning the inequality into a row

Expanding the constants yields

$$\sum_j C_jx_j\ge1+\sum_j C_jb_j.$$

`apply_one_cut` adds a new basic variable $c=\sum_j C_jx_j$ and asserts
the right side as its lower bound, using the synthetic reason
`kDerivedBoundReason`. Then it checks rational feasibility again. A conflict
ends the cut phase; otherwise it searches for another eligible row until
the cut budget is exhausted or no row qualifies. A `false` return means
“no conflict found”, not “no changes made”.

Take $x_b=2u+3w$, with $u$ at lower bound $0$ and $w$ at lower bound $1/2$.
Only $x_b$ is required to be integer. Its value is $3/2$, so $f=1/2$ and

$$4u+6(w-1/2)\ge1
\quad\Longleftrightarrow\quad4u+6w\ge4.$$

The current point $(u,w)=(0,1/2)$ gives $3<4$ and is excluded.
The feasible integer-$x_b$ point $(0,2/3)$ gives equality and $x_b=2$.
For an upper-bound example, $x_b=2u-3w$ with $u=1/3$ at its lower bound
and $w=-1$ at its upper bound gives $f=2/3$ and $6u-9w\ge12$.
It excludes the current value $11$ and keeps $(u,w)=(1/3,-10/9)$,
where $x_b=4$ and the cut is tight.

The {source}`cut tests <tests/unit/lia_gomory_cuts_tests.cpp>` check both
examples by rejecting the fractional point and retaining the integer point.
They also cover a cut that proves infeasibility, a term inside its bounds,
zero budget and a basic variable with nonzero infinitesimal.
The {source}`end-to-end cut regression
<tests/unit/qf_lia_solver_tests.cpp#L276-L306>` uses
$3x+5y=7$, $x,y\ge0$: the real relaxation is feasible, but $y=0$ or $1$
requires a fractional $x$, and $y\ge2$ already exceeds $7$.
Cuts resolve it under a branch budget too small for branching alone.

Cuts live in the private tableau for one integer completion. They remain
present while branching explores descendants, but are not retained for the
next Boolean candidate. No backtrackable row-insertion mechanism is needed
for this schedule. Primo does not use nonbasic integrality to strengthen
King's formula, the CAV 2024 cut-selection heuristics, or that paper's
temporary handling of large-coefficient cuts.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lia.md#sources-and-implementation-status) and [regression obligations](../../lia.md#models-regression-coverage-and-retained-evidence).
