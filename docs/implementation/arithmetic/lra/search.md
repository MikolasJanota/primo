# Pivot policies and optional search methods

This chapter explains pivot selection and optional search controls: sum of infeasibilities (SOI), fixed-basic elimination and preferred SAT truth values (phase hints). Each section states its own defaults; these controls do not all run on every route.

Start with the [LRA overview](../../lra.md) for integration with SAT and default settings. The [tableau notation](tableau.md#tableau-equations-and-bounds) defines basic/nonbasic variables, the assignment $\beta$ and bounds $l,u$.

## Pivot-selection policies

A feasibility pivot replaces one basic variable (the **leaving** variable) with
a nonbasic variable (the **entering** variable). The selection policy chooses
this pair. A variable's violation is its distance beyond its violated bound.

The solver's default is **Sparse**, although a directly constructed
`LraTableau` defaults to Bland. The
{source}`solver options <src/smt/check_sat.hpp#L282-L294>` select the policy:

| Policy | Implemented choice |
| --- | --- |
| Default sparse leaving | Shortest violated row; greatest violation breaks ties. |
| Sparse entering | Fewest column occurrences among eligible candidates considered; unit coefficient, then variable ID break ties. By default, compare at most 16 eligible candidates. The scan may visit more entries to find them. |
| Optional least-violated leaving | Griggio §2.5.3: smallest violation first, then shortest row. This overrides the default leaving policy under Sparse. |
| Optional Dantzig | Greatest violation first; largest absolute entering coefficient, with variable IDs breaking ties. |
| Bland | Smallest eligible variable IDs on both sides; also the termination fallback. |
| Optional SOI | Maintains a joint objective for all current violations; see [sum of infeasibilities](#optional-sum-of-infeasibilities). |

Monniaux §3 also describes preferring shorter rows and fewer column
occurrences to reduce pivot cost, in the context of forced pivoting. Primo's
sparse policy applies that rationale to ordinary feasibility search;
it does not import the surrounding floating-point/forced-pivot procedure.
King §2.2.12 provides the direct comparison for minimum-column selection
and eventual Bland fallback in SMT.

Heuristic selection eventually switches to Bland within each `check()` call.
The default threshold is `4 * variable_count + 64`; this is a tuning choice,
not a bound on the total pivots needed. King §2.2.12 explains why eventual
Bland selection matters. The selection scan must still find an eligible
candidate beyond an unproductive row prefix; otherwise a cost cap could
become an incorrect conflict. See
{source}`select_entering_variable <src/theory/arithmetic/lra_tableau.cpp#L901-L986>`.

(lra-soi)=
## Optional sum of infeasibilities

`--lra-pivoting-rule soi` selects **sum of infeasibilities (SOI)**. This mode
uses all current bound violations to guide a repair step. The ordinary
sparse policy remains the solver default. The algorithmic basis is
King, Barrett and Dutertre's
[Simplex with Sum of Infeasibilities for SMT](https://theory.stanford.edu/~barrett/pubs/KBD13.pdf),
FMCAD 2013, §V, and [King's thesis](https://cs.nyu.edu/media/publications/king_tim.pdf),
§§3.2–3.3. The implementation choices and departures are described below.

### The violation objective and its row

Write $\beta(x)$ for the current exact assignment, and $l(x)$ and $u(x)$
for the active bounds when present. Define the violation of a variable by

$$
v_\beta(x)=
\begin{cases}
l(x)-\beta(x), & \beta(x)<l(x),\\
\beta(x)-u(x), & \beta(x)>u(x),\\
0, & \text{otherwise}.
\end{cases}
\qquad
V(\beta)=\sum_x v_\beta(x).
$$

Only present bounds participate in these cases. Values and comparisons use
`DeltaRational`, so strict inequalities retain their infinitesimal part.
Nonbasic variables already satisfy their bounds; the error set $E$ consists
of violated basic variables. Thus $V(\beta)=0$ precisely when the current
assignment satisfies all bounds as well as the tableau equations.

The objective is piecewise linear: its slope changes when a variable reaches
a bound. For each basic variable $x_i$, let $d_i=-1$ when it is below its
lower bound, $d_i=+1$ when above its upper bound, and $d_i=0$ otherwise.
Substituting the basic rows gives a linear expression over nonbasic variables:

$$
f=\sum_{i\in E}d_i x_i
 =\sum_{j\in N}c_j x_j,
\qquad c_j=\sum_{i\in E}d_i a_{ij}.
$$

Here $N$ is the nonbasic set and $a_{ij}$ is the coefficient of $x_j$ in
row $x_i$. If $b_i$ is the bound currently violated by $x_i$, then
$V(\beta)=f(\beta)-\sum_{i\in E}d_i b_i$. The row for $f$ therefore records
the current linear part; evaluating it alone does not give the total violation.

{source}`LraTableau's SOI constructor <src/theory/arithmetic/lra_tableau.hpp#L246-L258>`
allocates an unbounded auxiliary basic variable `f_var_` and an initially empty
row. The tableau owns this row and the cached directions `soi_directions_`.
When a direction changes,
{source}`set_soi_direction <src/theory/arithmetic/lra_tableau.cpp#L446-L501>`
adds $(d_i^{\mathrm{new}}-d_i^{\mathrm{old}})$ times that basic row to the objective row.
Bound assertion and assignment updates call this operation. A pivot removes
the leaving variable's contribution before rewriting the basis, and ordinary
row substitution rewrites the objective too. Once the pivot and assignment
update finish, the row must again equal the signed sum of the violated rows.
The selector uses its coefficients, rather than treating the auxiliary
variable's stored assignment as $V$.

### Selecting a step at a bound crossing

{source}`soi_select <src/theory/arithmetic/lra_tableau.cpp#L517-L693>` considers
a nonbasic $x_j$ occurring in the objective row if it can move in the direction
that decreases $f$: decrease it when $c_j>0$, or increase it when $c_j<0$.
Its own lower or upper bound must leave room for that movement. The paper
calls these variables *flexible*.

Let $h$ be the proposed change to $\beta(x_j)$; this step is distinct from the
infinitesimal $\delta$. In a dependent row, the change to $\beta(x_i)$ is
$a_{ij}h$. Reaching a bound $b$ of that basic variable gives a **breakpoint**

$$
h=\frac{b-\beta(x_i)}{a_{ij}}.
$$

The selector collects lower- and upper-bound crossings in the chosen direction,
plus the entering variable's own bound if present. It sorts them by distance
along that direction and discards points beyond the entering variable's bound.
Basic variables may become violated along the walk; the chosen step is judged
by the sum of violations, while nonbasic variables remain within bounds.

The walk starts with slope $c_j$ and zero change in $V$. Between consecutive
breakpoints $h_{old}$ and $h_{new}$, a constant slope $m$ gives

$$
\Delta V(h_{new})=\Delta V(h_{old})+m(h_{new}-h_{old}).
$$

Crossing a basic bound changes that row's contribution to the slope by
$(d_i^{\mathrm{after}}-d_i^{\mathrm{before}})a_{ij}$. Coincident breakpoints are
processed together before continuing. Primo scores candidates with this walk
without temporarily changing the live tableau.

For a fixed entering variable, Primo chooses the breakpoint with the smallest
computed $\Delta V$, then the smallest $|h|$, then the smallest leaving-variable ID.
Across entering variables, it prefers a strict decrease over a zero change,
then the largest $|c_j|$, then the smallest entering ID. It does **not** compare
the size of the decrease across different entering variables.
Starting-bound crossings use the one-sided slope described below.

{source}`check's SOI loop <src/theory/arithmetic/lra_tableau.cpp#L1731-L1795>`
applies the selected step. If the entering variable reaches its own bound,
it only updates that variable. Otherwise, Primo first pivots the selected
basic variable out, then sets it to the reached bound. This produces the same
final assignment as updating the entering variable and then pivoting it in.

### A worked pivot

The paper's Figure 6 starts with

$$
\begin{gathered}
x_1=2x_2-x_3,\\
3\le x_1\le7,\qquad x_2\le3,\qquad x_3\ge1,\\
\beta(x_1)=\beta(x_2)=\beta(x_3)=1.
\end{gathered}
$$

Only $x_1$ is violated, by two, so $f=-x_1=-2x_2+x_3$. Decreasing $x_3$
would help but its lower bound blocks that move. Increasing $x_2$ is possible.
The retained breakpoints for its step $h$ are:

| Step | Bound reached | Resulting values | Total violation |
| --- | --- | --- | --- |
| $h=1$ | $x_1=3$ | $x_1=3,\ x_2=2,\ x_3=1$ | $0$ |
| $h=2$ | $x_2=3$ | $x_1=5,\ x_2=3,\ x_3=1$ | $0$ |

The upper bound $x_1=7$ would give $h=3$, which Primo discards because it
would move $x_2$ beyond its own upper bound. Both retained steps remove the
violation, so the smaller step wins: $x_2$ enters and $x_1$ leaves. Solving
the row for $x_2$ gives $x_2=(x_1+x_3)/2$. Setting the now-nonbasic $x_1=3$
therefore sets $x_2=2$, and all bounds hold after one pivot.

The {source}`worked-example regression <tests/unit/lra_tableau_tests.cpp#L781-L829>`
checks the resulting basis and values. It adds $x_2\ge1$ to establish the
initial assignment through the public bound API; that extra bound does not
restrict the increasing walk used here.

(combining-rows-into-a-conflict)=
### Combining rows into a conflict

If the objective has no flexible variable and $E$ is empty, `check()` succeeds.
If $E$ is nonempty, the current assignment minimizes the linear expression
$f$ using the nonbasic bounds, but still has positive violation. This proves
a conflict that may need several rows.

For example, consider two basic rows and three asserted atoms:

```text
s1 = x + y       p: s1 <= -1
s2 = x - y       q: s2 <= -1
                r: x >= 0
```

At the all-zero assignment both upper bounds are violated. Their directions
are $+1$, so $f=s_1+s_2=2x$. Decreasing $x$ is blocked by `r`, and $y$
cancels from the objective. The rows and `r` imply $s_1+s_2\ge0$, whereas
`p` and `q` require $s_1+s_2\le-2$. The explanation clause is
`¬p ∨ ¬q ∨ ¬r`. Each basic row alone could be repaired by moving $y$;
their combination exposes why both cannot be repaired together.

More generally, for a nonempty subset $S$ of the current error set, form
$f_S=\sum_{i\in S}d_i x_i=\sum_j c_j x_j$. If this restricted row has no
flexible variable, each positive coefficient is blocked at its lower bound
and each negative coefficient at its upper bound. These bounds establish

$$
f_S\ \ge\ \sum_{c_j>0}c_j l(x_j)+\sum_{c_j<0}c_j u(x_j)
\ =\ f_S(\beta)\ >\ \sum_{i\in S}d_i b_i.
$$

The last inequality holds because every variable in $S$ has positive
violation. Its selected bound requires $d_i x_i\le d_i b_i$, so those bounds
together contradict the displayed lower bound on $f_S$.
{source}`build_soi_conflict <src/theory/arithmetic/lra_tableau.cpp#L700-L742>`
collects the blocking nonbasic-bound reasons and each selected violated-bound
reason. They already have their clause signs, as in the
[single-row explanation](feasibility.md#follow-a-strict-conflict).

### Shrinking the explanation

The initial set $E$ can include unrelated conflicts. The default
{source}`QuickXplain-style minimizer <src/theory/arithmetic/lra_tableau.cpp#L744-L814>`
recursively splits the candidate error variables, trying one half against a
background containing the other. Its test rebuilds the restricted row $f_S$
and checks for a flexible variable; the empty set is explicitly rejected.
It searches for a smaller conflict in the **current assignment and basis**,
without another simplex search.

`--lra-soi-minimize deletion` instead
{source}`tries removing one row at a time <src/theory/arithmetic/lra_tableau.cpp#L816-L886>`
and keeps a removal only when the remaining set still passes that conflict
test. `--lra-soi-minimize-order` controls its visit order: `id` uses increasing
IDs, `row-support` tries longer rows first, and `violation` tries larger
violations first. This option has no effect on the default `quickxplain` or
on `none`, which retains all error variables.

This is heuristic explanation reduction, without a guarantee of a globally
smallest clause. Removing a row changes the coefficients of $f_S$ and can
expose or cancel nonbasic variables. The result contains a reason for each
selected violated bound **and** each nonzero coefficient's blocking bound;
minimizing the number of selected rows does not necessarily minimize the
number of clause literals.

### Cost, termination and implemented scope

Primo follows the paper's violation objective, breakpoint walk and multi-row
conflict derivation. It makes several concrete choices:

- The walk considers only the direction that initially decreases $f$, as
  permitted by §V-A, and keeps the entering variable within its own bounds.
  It scans every flexible variable in the objective row. For each candidate
  it scans dependent rows to find coefficients, sorts the breakpoints, and
  scans rows again at crossings. Sorting $k$ points costs $O(k\log k)$
  comparisons, in addition to those row scans and exact arithmetic.
- The objective update merges and sorts the old objective row with a signed
  basic row. This can cost more than an ordinary repair step, especially
  when the objective accumulates many nonzero coefficients.
- Unlike the paper's §V-C rule, Primo retains the coefficient preference
  between entering variables even when the best steps make no progress.
  It instead bounds the SOI prefix using the
  {source}`finite fallback settings <src/theory/arithmetic/lra_tableau.hpp#L223-L237>`:
  by default `4 * variable_count + 64`. The local counter counts selected
  SOI steps, including updates without a pivot. Once it exceeds the threshold,
  ordinary Bland repair handles the rest of that `check()` call. The threshold
  does not cap the total work or make the result `unknown`.
- The minimizer uses the restricted-row test described in §V-B and King's
  §3.3.5. It does not implement the thesis's Figure 3.6 row-selection loop,
  which grows a conflicting subset using coefficient signs before recursive
  reduction. Deletion and its orderings are additional Primo choices.
- The SOI selector does not run Figure 5's preliminary `CheckAllConflicts`
  scan. It detects direct contradictory bounds during assertion and builds
  a row-combination conflict when its objective has no flexible variable;
  ordinary row-conflict checking also remains available after fallback.
- SOI also maintains its objective during bound assertion and SAT rollback.
  Its extra trail state is described in
  [Incrementality and rollback](tableau.md#incrementality-and-rollback).

```{admonition} Starting exactly on a bound
:class: note

At step zero, a variable on a bound has direction zero. Moving into its
feasible interval leaves that direction unchanged; moving out changes it
to `+1` at an upper bound or `-1` at a lower bound. `SoiBreakPoint` retains
the bound kind so the {source}`slope update <src/theory/arithmetic/lra_tableau.cpp#L638-L665>`
can distinguish these cases. A fixed variable has two coincident events but
contributes only once at zero.
At a nonzero crossing, passing through its fixed value still changes the
direction from `-1` to `+1`, or conversely, and contributes twice.

For a concrete example, start at zero with `s = x + y`, `t = x - y`,
`y = 0`, `s >= 1`, and `t >= 0`. Only `s` is violated, giving
`f = -x - y`. Increasing `x` by one removes the violation, while `t` moves
inside its feasible interval. Its zero-length lower-bound event leaves the
slope at `-1`, and the step of one repairs the assignment in one pivot.

The earlier implementation incorrectly changed the slope to `0` at that
starting event, scored the step of one as making no improvement, and selected
a zero-step pivot first. The historical two-pivot diagnostic is recorded as
`2026-09-28-soi-zero-breakpoint-debug` in the
{download}`run log bundled with this draft <../../../run-log.json>`.
The {download}`retained reproduction <../../../examples/soi-zero-step.cpp>` now
asserts a single pivot and the same final values `x = 1`, `y = 0`.
Dedicated tableau regressions cover inward/outward motion, both coefficient
signs and walk directions, coincident fixed bounds, infinitesimal steps,
and replay after rollback. The older Figure 6 regression remains separate.
```

Existing tests cover the worked pivot, rollback, reduction of independent
conflicts, and the selectable minimization strategies. Counters distinguish
multi-row conflicts, minimizer calls, reductions in the error set, emitted
reason literals, and Bland fallbacks. A smaller error set alone is not evidence
of faster solving.

The {source}`run log <docs/run-log.json>` preserves dated SOI evidence:

| Record | Result and limitation |
| --- | --- |
| `2026-07-25-qf-lra-soi-simplex-quick-eval`, `fc06922` | SOI and Bland solved the same 7 of 17 instances at a five-second limit. This was a local exploratory comparison without committed raw artifacts. |
| `2026-07-25-qf-lra-soi-simplex-slurm-smoke-gate`, `bcdf7ef` | Of 500 generated cases, 324 passed and 176 timed out; no crashes or wrong answers were reported. This is historical partial coverage, below today's merge-gate count. |
| `2026-08-10-soi-minimization-arms-measured`, `8ddd62d` | A selected 18-instance local comparison measured reason-literal counts for five strategies. No deletion ordering consistently improved on QuickXplain; runtime/PAR2 was not evaluated. |

These records explain the retained experiments, but do not qualify the current
revision or establish a speed advantage. SOI remains opt-in, with QuickXplain
as its default minimizer.

The correction's fresh SOI fuzz gate generated 1,000 distinct QF_LRA inputs:
958 agreed with Z3 and 42 timed out at five seconds, including two mutual
timeouts. It found no wrong answers, crashes or input rejections. See
`2026-09-29-soi-endpoint-correction` in the bundled run log for qualification
and limits; this is not a new overall performance claim.

## Fixed-basic elimination

When the lower and upper bounds of a basic variable become equal, its value is
fixed while those bounds hold. With fixed elimination enabled,
{source}`try_eliminate_fixed_basic <src/theory/arithmetic/lra_tableau.cpp#L393-L431>`
pivots that variable out of the basis, then updates the now-nonbasic variable
to its fixed value. Dependent rows receive the corresponding assignment update.
A variable fixed at both bounds cannot be a useful entering candidate while
those bounds remain in force.

For example, if the row is $s=x+y$ and bounds fix $s=3$, pivoting on $x$ gives
$x=s-y$. Setting the nonbasic $s$ to 3 makes the equality explicit in the
assignment without repeatedly selecting a violated fixed basic row. This is
a basis change, not deletion of the original variable or its explanation.
When a bound is undone, ordinary bound rollback and feasibility repair apply;
the basis need not return to its original shape.

The operation runs from basic-variable bound assertion. An empty row is left
for the ordinary consistency check, because forcing a nonzero value onto its
constant-zero expression can be a real conflict. A row without an admissible
entering variable is also left unchanged. The rule preserves bound reasons and
does not implement the full column-deletion/restoration pipeline of linear-programming presolve.

The {source}`H7 design <docs/qf-lra-heuristics.md#L539-L736>` records the
original adaptation and the rejected row-freezing extension. Current defaults
are off for pure LRA and on for the dedicated mixed Real route, as declared in
{source}`CheckSatOptions <src/smt/check_sat.hpp#L295-L310>`. Compare elimination
pivots, later repair work and total runtime; fewer fixed basic rows alone is
not a performance conclusion. The [evidence summary](../../../evidence/arithmetic.md)
preserves the distinct pure/mixed evaluations.

The {source}`fixed-elimination regression <tests/unit/qf_lra_solver_tests.cpp#L4549-L4565>`
checks SAT and UNSAT examples with the option both enabled and disabled, and
requires nonzero elimination counters when enabled.

## Model phase hints

`--arithmetic-model-phase=on` lets the arithmetic assignment suggest a Boolean polarity
for an arithmetic atom. If the current tableau assigns $x=2$, it recommends
the positive literal for $x\leq3$ and the negative literal for $x<1$.
This is a search preference: the current assignment is not a proof that either
literal follows from the asserted constraints.

{source}`model_phase_literal <src/smt/solver_engines.cpp#L7415-L7430>` tests
the prepared bound, including its strict/integer normalization, against the
`DeltaRational` assignment. Equality relations are skipped. The selected
literal is passed to the SAT backend's phase interface; SAT still selects its
decision variable and may choose a different search path.

The online propagator tracks which tableau assignments may have changed, so
local updates need refresh only their affected atoms; a pivot can require a
full refresh.
Refresh waits for a conflict-free state, and
{source}`refresh_model_phase_atom <src/smt/solver_engines.cpp#L7459-L7473>`
avoids resending an unchanged hint. Hints may become stale between refresh and
the later SAT decision; no theory lemma is justified by their value.

The adapter delivers these preferences through CaDiCaL's `phase` API, which
forces a variable's default decision phase; it is not a one-decision callback.
Primo exposes no matching `unphase` operation. The
[full-corpus regression investigation](../../../evidence/lra-regressions.md)
records the resulting search risks and separates arithmetic work from refresh
overhead. It does not establish stale phases as the cause of those losses.

The existing option implements the core existing-assignment idea of the
zero-extra-pivot variant in Goldwasser, Strichman and Fine, Sections III-C/III-D
{cite:p}`goldwasser2008theorydecision`: evaluate an arithmetic atom at the
simplex assignment and use the result as a preferred Boolean sign, leaving
variable selection to SAT. Phase selection performs no additional pivots;
ordinary feasibility repair can still pivot and update the assignment.
Primo publishes cached preferences through CaDiCaL rather than consulting the
theory at each decision, so this is not a claim of identical decision timing
or a complete implementation of the paper's variants. The
[phase-control note](../../../papers/arithmetic-phase-control.md) records that mapping
and the limits of the paper's performance evidence.

SPASS-SATT §3 and footnote 3 describe arithmetic decision recommendations
{cite:p}`bromberger2019spass`. Primo tests whether the assignment satisfies each
atom and updates its phase, rather than using the paper's immediate `GetPhase`
callback. It does
not replace SAT variable selection or implement a new entailment test here.
The {ref}`automatic QF_LRA policy <automatic-lra-heuristics>` enables this option
for rebuild queries with automatic preprocessing; it is off elsewhere unless
explicitly enabled. The
[dated arithmetic comparison](../../../evidence/arithmetic.md#different-routes-can-justify-different-choices)
records gains and losses separately by logic.

The {source}`phase-refresh regression <tests/unit/qf_lra_solver_tests.cpp#L3605-L3633>`
forces a bound update to change the recommended phase, then checks that hints
are refreshed and disappear when the option is disabled.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lra.md#sources-and-implementation-status) and [regression obligations](../../lra.md#invariants-exercised-by-existing-tests).
