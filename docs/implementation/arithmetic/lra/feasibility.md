# Rational feasibility and conflicts

This chapter follows a rational consistency check from an asserted bound to assignment repair or a conflict clause explaining failure. Integer completion is a separate step.

Start with the [LRA overview](../../lra.md) for integration with SAT and default settings. The [tableau notation](tableau.md#tableau-equations-and-bounds) defines basic/nonbasic variables, the assignment $\beta$ and bounds $l,u$.

## Check consistency

| Contract | Meaning |
| --- | --- |
| Input | The prepared tableau equations, plus currently asserted bounds with their reason literals. |
| Result | A successful `check()` returns no conflict; a failed bound assertion or check returns an owned vector of SAT clause literals. |
| Default method | Ordinary feasibility repair with Sparse pivot selection and eventual Bland fallback. |
| Correctness condition | Success means one assignment satisfies every tableau equation and active bound. Failure must be justified by an inconsistent subset of asserted bounds. |

### Feasibility without an optimization objective

Proving UNSAT with delta rationals does not require optimizing $\delta$ or
introducing an objective function. The ordinary repair procedure checks
**feasibility**: whether the tableau equations and active bounds have a common
solution in the lexicographically ordered pairs. Dutertre–de Moura §3.3 shows
that this encoding preserves satisfiability for finite systems of rational
linear constraints with strict inequalities. Consequently, a contradiction
in the encoded system proves that the original constraints are unsatisfiable.

During this check, $\delta$ stays symbolic. Choosing a sufficiently small
positive rational to replace it belongs to
[constructing a satisfying model](tableau.md#model-construction), after feasibility has
been established; an UNSAT proof needs no such choice.

### Repairing an assignment

Dutertre–de Moura §3.2 separates three obligations:

1. Every assignment satisfies the tableau equations.
2. Every nonbasic variable satisfies its current bounds.
3. After a successful `check()`, basic variables satisfy their bounds too.

{source}`assert_upper and assert_lower <src/theory/arithmetic/lra_tableau.cpp#L47-L95>`
tighten a bound and save its predecessor on the undo trail. A weaker bound
does nothing. A new upper bound below the current lower bound, or conversely,
returns an immediate conflict without installing the contradictory bound.
If a nonbasic value falls outside a new bound, `update` moves it to that bound
and updates dependent basic values. Basic variables may then need repair.

For the ordinary repair policies,
{source}`check <src/theory/arithmetic/lra_tableau.cpp#L1797-L1894>` selects a
violated basic variable and a nonbasic variable that can move in the required
direction. It pivots the latter into the basis, then moves the now-nonbasic
leaving variable to its violated bound. For example, pivoting `y` into the
row `s = x + y` yields `y = s - x`; setting `s` to its target updates `y`
and every other row depending on `s`. Each pivot preserves the equations.
If no eligible entering variable exists, current bounds prevent repairing
that row, and the solver explains a conflict.

The solver selects Sparse by default and eventually uses Bland to ensure
termination; a directly constructed `LraTableau` defaults to Bland. The
[policy details](search.md#pivot-selection-policies) describe the choices and fallback
threshold. If no basic variable remains violated, `check()` succeeds. If a
row is blocked, [conflict explanation](#explain-theory-conflicts) identifies
the assertions responsible. The complete rational feasibility procedure
can therefore decide inconsistency without enabling theory propagation or
[sum of infeasibilities](search.md#optional-sum-of-infeasibilities). This completeness claim concerns linear real arithmetic; the
[integer relaxation](../../lia.md) needs additional integer reasoning.

### Deferred consistency checks

The online propagator applies each assigned bound immediately but defers
simplex repair until a consistency check is needed. This batches `check()`
calls, not the bound updates themselves; deferred assignment updates remain
a different proposal. An immediate bound conflict must survive the deferred
check because the rejected bound never entered the tableau. See
{source}`notification and deferred repair <src/smt/solver_engines.cpp#L7310-L7413>`.
Delivered propagation reasons remain available until their assignments are
removed, following the shared-assignment protocol in [DPLL(T)](../../dpllt.md).

## Explain theory conflicts

| Contract | Meaning |
| --- | --- |
| Input | Either two contradictory bounds on one variable, or a violated basic row for which no eligible repair variable exists. |
| Result | A clause containing the negations of the asserting SAT literals for the conflicting bounds. |
| Default method | Direct bound comparison, followed when necessary by a single-row explanation after feasibility pivots. |
| Correctness condition | The selected assertions and tableau identities imply a contradiction. Their negations therefore form a theory-valid clause. |

These are explanations of **theory conflicts**. A propagation explanation
instead justifies an additional literal and is covered in
[propagation](propagation.md#propagate-and-explain-consequences). The ordinary conflict
procedure follows Dutertre–de Moura §3.2.2.

### Directly contradictory bounds

If assertions `p` and `q` impose $x\ge l$ and $x\le u$ with $u<l$,
`assert_upper` or `assert_lower` returns the existing bound's reason and the
incoming bound's reason, forming $\neg p\lor\neg q$. The comparison is exact,
including infinitesimal parts. The rejected bound need not be installed and no pivot is required;
the [bound assertion operations](#repairing-an-assignment) handle this case.

### Selecting bounds from a blocked row

Consider a current basic row $x_b=\sum_j a_jx_j$. If
$\beta(x_b)<l(x_b)$ and no nonbasic variable can move to increase $x_b$,
the positive-coefficient variables are at their upper bounds and the
negative-coefficient variables are at their lower bounds. These bounds imply

$$
x_b\le U=\sum_{a_j>0}a_j u(x_j)+\sum_{a_j<0}a_j l(x_j)
       =\beta(x_b)<l(x_b).
$$

The explanation selects the violated lower bound on $x_b$ and each bound
used in $U$. For a variable above its upper bound, the symmetric calculation is

$$
x_b\ge L=\sum_{a_j>0}a_j l(x_j)+\sum_{a_j<0}a_j u(x_j)
       =\beta(x_b)>u(x_b).
$$

Here the explanation selects the violated upper bound and the bounds used
in $L$. Every selected bound exists: an absent blocking bound would leave
an eligible movement direction. The
{source}`entering-variable selection <src/theory/arithmetic/lra_tableau.cpp#L901-L986>`
establishes that the row is blocked, and
{source}`build_conflict <src/theory/arithmetic/lra_tableau.cpp#L433-L446>`
selects the corresponding reasons according to coefficient signs. The row
identity needs no SAT premise. Collecting its selected bound reasons forms the
conflict clause; the mathematical inequalities explain why that clause is valid.

### Minimization and alternative explanations

The default single-row procedure has no separate minimization pass. It uses
the current blocking bounds, with one active bound per direction on each
variable; it does not search weaker sufficient bounds or expand shared
propagation antecedents. SAT can resolve propagated literals through their
reasons during conflict analysis. Its final learned clause can differ from
the theory explanation.

A derived numeric bound need not have a SAT naming literal. If used to explain
a conflict, its internal proof must first be expanded to SAT-visible premises
by the theory. Compatible assigned stronger bounds do tighten the tableau:
the new value and its asserting literal's reason replace the previous pair;
an immediately contradictory bound returns a conflict instead. For a propagated
literal, the SMT callback separately retains its antecedents for SAT analysis;
see [the worked tightening explanation](../../../papers/conflict-explanation-minimization.md#tightening-a-bound-and-preserving-its-reason).
Current row propagation returns derived values with those
premise reasons and proves registered atoms; it does not install a general
graph of internal derived bounds. The survey distinguishes these cases from
[temporary bounds in private equality probes](../../../papers/conflict-explanation-minimization.md#bounds-without-a-sat-literal).

The optional [SOI minimizer](search.md#optional-sum-of-infeasibilities) reduces selected violated rows. It is
separate from arithmetic bound weakening and from SAT learned-clause
minimization. Classical SAT minimization deletes literals; shrinking based on unique
implication points (all-UIP) can introduce replacement antecedents. King's thesis §2.2.9 describes another
choice: weaken bound premises while preserving the numerical contradiction.
These distinctions, current implementation limits and worked examples are
in the [literature survey and proposed probe](../../../papers/conflict-explanation-minimization.md).

### Why one current row suffices after pivoting

A current tableau row can combine several original equations. For example,
let SAT assert all three literals

```text
p: x + y <= -1       q: x - y <= -1       r: x >= 0
```

Introduce basic variables $s_1=x+y$ and $s_2=x-y$, carrying upper bounds
$s_1\le-1$ from `p` and $s_2\le-1$ from `q`. Initially set $x=y=0$;
both basic variables violate their upper bounds. Each row can be repaired
individually by moving $y$ while keeping $x\ge0$.

One possible feasibility pivot repairs $s_1$ using $y$. Solving the first
row and substituting into the second gives

$$
y=s_1-x,\qquad s_2=x-(s_1-x)=2x-s_1.
$$

Setting the now-nonbasic $s_1=-1$ gives $y=-1$ and $s_2=1$ when $x=0$.
The single current row for $s_2$ is now blocked: decreasing $x$ is prevented
by $x\ge0$, and increasing $s_1$ is prevented by $s_1\le-1$. Its selected
bounds imply

$$
s_2=2x-s_1\ge2\cdot0-(-1)=1,
\qquad s_2\le-1,
$$

a contradiction explained by $\neg p\lor\neg q\lor\neg r$.
This is an illustrative pivot sequence; the selection policy can choose
another sequence leading to a conflict. The
{source}`pivot operation <src/theory/arithmetic/lra_tableau.cpp#L143-L315>`
maintains the substituted identities, so the final explanation needs only
the current row and its bounds. A single original constraint need not
already expose the contradiction. SOI can also combine these two rows
[directly](search.md#combining-rows-into-a-conflict), but is not required to explain it.

(a-bound-contradiction-and-its-cancellation-proof)=
### Follow a strict conflict

Let SAT assign `p`, `q` and `r` true for:

```text
p: x + y <= 2       q: x >= 2       r: y > 0
```

Preparation supplies `s = x + y`, initially with all values zero. The
following is a trace of theory operations, not a prescribed SAT search order:

| Operation | Assignment and bounds |
| --- | --- |
| Assert `p` | Install `s <= 2`, carrying reason `¬p`. |
| Assert `q` | Install `x >= 2`, carrying `¬q`; update `x = 2`, hence `s = 2`. |
| Assert `r` | Install `y >= δ`, carrying `¬r`; update `y = δ`, hence `s = 2 + δ`. |
| Check the violated row | `s` is too high. Decreasing either `x` or `y` is blocked by its lower bound, so no repair pivot is possible. |

The row and the two lower bounds imply `s >= 2 + δ`, contradicting `s <= 2`.
The resulting clause is `¬p ∨ ¬q ∨ ¬r`.

The same contradiction can be checked by rewriting the three encoded bounds
with $\le$ and adding them:

$$
\begin{aligned}
 x+y &\le 2,\\
 -x &\le -2,\\
 -y &\le -\delta
\end{aligned}
\qquad\Longrightarrow\qquad 0\le-\delta.
$$

The variables cancel, and the last inequality is false because
$-\delta=(0,-1)<(0,0)=0$. All three weights in this combination are the
nonnegative rational $1$, so adding the inequalities preserves their
direction. More generally, nonnegative rational combinations of inequalities
can certify a conflict when all variable coefficients cancel and the
right-hand side is negative in the delta ordering. This explains the
mathematical proof; it does not imply that Primo exports those weights.

Primo stores **the negation of the asserting SAT literal** with each bound,
at the {source}`assignment boundary <src/smt/solver_engines.cpp#L7302-L7310>`.
{source}`build_conflict <src/theory/arithmetic/lra_tableau.cpp#L433-L446>`
collects the violated bound's reason and the row bounds that block movement.
It returns clause literals directly; the caller must not negate them again.
This is the row-based explanation of Dutertre–de Moura §3.2.2. The returned
object is a SAT clause, not an exported proof certificate with coefficients.

This clause excludes the current combination of arithmetic assertions.
For a Boolean combination of constraints, SAT uses such clauses to continue
the search; one arithmetic conflict alone need not make the whole formula
UNSAT. See the [DPLL(T) chapter](../../dpllt.md).

The optional [sum of infeasibilities](search.md#optional-sum-of-infeasibilities) mode uses an objective to guide
repair. A zero sum means all bounds hold; a **proven positive minimum**
establishes infeasibility. A positive violation at an arbitrary assignment
does not suffice. This objective is a search method, not a requirement imposed
by delta rationals, and $\delta$ itself is never the optimization variable.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lra.md#sources-and-implementation-status) and [regression obligations](../../lra.md#invariants-exercised-by-existing-tests).
