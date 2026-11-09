# Linear integer arithmetic

This theory follows the [shared assignment and explanation contract](theory-interface.md#theories-and-their-shared-contract).
Both standalone QF_LIA and combined QF_UFLIA use one LIA adapter implementing
`TheoryComponent`; the integer algorithms described here run behind that
boundary. The [interface inventory](theory-interface.md#which-interface-is-used)
explains the adapters and their responsibilities.

An arithmetic assignment can satisfy every linear inequality and still fail
to be an integer model. For example, `2*x - 2*y = 1` has the real solution
`x = 1/2, y = 0`, but no integer solution: its left side is even. Primo first
solves the [real relaxation](lra.md), then tries to repair the assignment or
prove that the current Boolean choice has no integer completion.

This chapter describes the integer stages shared by `QF_LIA` and `QF_UFLIA`,
plus the experimental standalone QF_LIA cube model attempt.
Configured resource limits can stop the search with `unknown`. Removing those
limits alone does not guarantee a terminating decision procedure for all
quantifier-free linear integer inputs: with unbounded variables, the current
branch-and-bound search can follow an infinite path. The
[budgets and incomplete cases](#lia-limits) section distinguishes this
algorithmic limitation from resource exhaustion and gives a concrete example.

(lia-representation)=
## Representation and ownership

The integer procedures reuse {source}`LraTableau
<src/theory/arithmetic/lra_tableau.hpp>`. A row has the form

$$x_b=\sum_j a_jx_j,$$

where $x_b$ is basic, the $x_j$ are nonbasic, and the coefficients are exact
rationals. “Basic” means that the row determines the variable's value from
the nonbasic values; pivoting changes which variable occupies that role.
Bounds and assignments use {source}`DeltaRational
<src/theory/arithmetic/lra_tableau.hpp#L19-L56>`: a pair $q+d\delta$, with
$\delta$ a positive infinitesimal used to represent strict inequalities.

| State | Owner and meaning |
| --- | --- |
| Prepared atoms and term-to-variable map | {source}`PreparedLraTheory <src/smt/solver_engines.cpp#L3332-L3410>` connects arithmetic terms and SAT atoms to tableau variables and bounds. |
| `integer_vars` | The caller owns a vector of `LraVarId`s whose values must become integers. In the pure route it is collected from the `Int` terms in `variables_by_term`; the combined route also registers shared `Int` terms. Under `--lia-integer-slacks` it also starts with the slacks that preparation proved integer (`PreparedLraTheory::integer_slacks`). It is not the set of all tableau variables. |
| Working tableau | SMT integration owns a copy of a rationally feasible tableau and lends it by mutable reference to {integersource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.cpp>`. Patching changes assignments; cuts allocate variables and rows; branching adds temporary bounds. These changes stay within this copy. |
| `LiaExplanation` | This {source}`explanation object <src/theory/arithmetic/lia_explanation.hpp>` stores the blocking literals contributed by GCD checks, cuts and branching in a sorted, duplicate-free vector. The stages share one object so a later conflict retains the reasons for earlier cuts. |
| Branch frames | {source}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>` owns an explicit DFS stack. Each frame stores a variable ID, a split threshold, a tableau checkpoint and which side has been tried. |

`LraVarId` is an index within a tableau and its matching copies, not a core
`TermId` or a SAT variable. Appending a cut gives it a new ID without changing
existing IDs. Neither the temporary cut variable nor a branch bound creates
a core term or a new SAT atom. The working copy is discarded when its Boolean
candidate is rejected; a winning copy supplies the integer model.

An auxiliary variable for a normalized polynomial need not be integer-valued.
For example, dividing `2*x + y` by its leading coefficient produces
$s=x+y/2$. Even with integer $x,y$, $s$ may be a half-integer. Conversely,
normalizing `3*x <= 5` gives the direct bound $x\le5/3$: **no auxiliary row**
is needed. The variable is integer, but its current bound is fractional.
These are different cases of the [normalization described in LRA](lra.md).

The integer set records semantic obligations, independently of the current
basis. A pivot can move a noninteger auxiliary variable into another row as
an ordinary term. No integer rule may assume that nonbasic variables are
integers just because the original input used `Int`. Bjørner and Nachmanson
§4.1 can make that assumption because it also relies on integral bounds and
integer-valued auxiliaries. Primo's preparation provides neither yet; the
[integrality checklist](../papers/mip-presolve-integrality.md) lists the four
gaps, their sources and the counters that measure them.

(lia-schedule)=
## When integer reasoning runs

The standalone {migrationsource}`solve_qf_lia_impl recipe
<src/smt/solver_engines.cpp#L10043-L10077>` selects one `LiaTheoryComponent`
and calls {migrationsource}`solve_component_recipe <src/smt/component_solver.cpp#L70-L502>`. Online rational propagation stays enabled
independently of EUF settings. Model-guided combination is disabled: with only
one component, there are no shared scopes, interface arrangements, equality
bridges, or combination fallback. QF_UFLIA selects the same LIA component
alongside EUF.

### Repair schedule and phase advice

"Repair" refers to two operations with different schedules. The following
table describes the **online arithmetic execution** route:

| Event | Rational feasibility | Integer patching/completion |
| --- | --- | --- |
| SAT assigns an arithmetic literal | Apply its bound immediately; reject direct bound contradictions. Full simplex repair may remain pending. | Does not run. |
| Online theory consistency callback during Boolean search | Batch pending work and run `LraTableau::check()` to restore rational feasibility or report a conflict. | Does not run. |
| SAT returns a complete Boolean candidate and the driver requests `FullModel` checking | Reuse a checked online tableau when the route permits, or rebuild the relaxation and check it. | With `--qf-uflia-tableau-equalities`, QF_UFLIA first checks explained shared equalities; if no delivery is needed, run patching, GCD checks, cuts and branch-and-bound on the separate completion tableau. |
| A cut or internal integer split is added during completion | Recheck rational feasibility within that private tableau. | Continue the completion pipeline; do not restart the patcher after every cut or split. |

QF_UFLIA can instead run without online arithmetic execution, depending on its
online-component option and EUF engine. Assignments then record literals
without immediately asserting their bounds into a live tableau. The full-model
check rebuilds the candidate relaxation, applies those bounds, checks rational
feasibility and then runs integer completion. Integer patching remains late. The
[shared-equality scan](uflia-equality-propagation.md) can return a guarded
consequence after rational repair and before that completion work. It runs at
the full-candidate boundary, not after every online bound assertion.

The {settingssource}`online arithmetic execution <src/smt/solver_engines.cpp>`
records assigned bounds and marks deferred work.
{settingssource}`ComponentDispatcher <src/smt/component_dispatcher.cpp>` requests
partial checks through the external-clause callback; `LiaTheoryComponent`'s
partial-effort branch performs rational checks only. The model-approval callback
inside SAT also requests partial effort, not integer completion. After
`backend.solve()` returns SAT, the
{settingssource}`shared driver <src/smt/component_solver.cpp>` calls
{settingssource}`ComponentModelCoordinator::reconcile <src/smt/component_model_coordinator.cpp>`,
which requests `TheoryCheckEffort::FullModel` from the LIA component. That is
the integer-completion boundary. Online batching is an intermediate schedule
for **rational** repair, not eager integer patching.

A full Boolean candidate selects the currently asserted arithmetic literals;
it is not yet an accepted model of the original formula. Integer feasibility
and, on QF_UFLIA, component-model agreement can still fail. A learned conflict
or another combination choice can lead to another SAT call and another integer
completion within the same `check-sat`. Thus "late" does not mean once after
the entire SMT query has already been solved.

The table also distinguishes the model used by phase advice. When enabled,
`ComponentLraExecution::model_phase_literal` reads the live rational assignment
β. `LiaComponentAdapter` keeps the successful completion model separately;
integer patching does not write its assignment back into that live tableau.
Standalone QF_LIA, and an online LIA component not producing a shared model
projection, start completion from a copy of the checked online tableau. The
model-guided QF_UFLIA route instead requests a projection and rebuilds a
rational tableau from the prepared base and full candidate literals before
completion. That rebuilt assignment may already differ from live β before
integer patching starts. See the
{ref}`phase-control investigation <narrow-region-phase-candidate>`.

Eager integer repair after every assigned bound, or bounded intermediate
integer-repair checkpoints on feasible partial assignments, are possible
alternative schedules; neither is implemented as a scheduling option here.
An integral assignment to a partial arithmetic trail does not establish SAT
for the whole formula. A limited unsuccessful attempt is not an explained
integer conflict. Comparing schedules would require separate correctness and
performance evidence, including the additional repair work.

The {retentionsource}`driver observes otherwise unowned Boolean constants <src/smt/component_solver.cpp#L276-L291>` through SAT's
assignment interface, in the original atom registry order. Their assignments
remain propositional: the dispatcher does not route them into the LIA component.
Observing them preserves the standalone route's SAT elimination policy while
making their values available in the shared assignment view and final model.

At the full-model check boundary, the LIA adapter copies the rationally feasible
online tableau once, or moves a newly rebuilt rational tableau, into integer
completion. It passes {integersource}`LiaCompletionLimits
<src/theory/arithmetic/lia_completion.hpp>` (patch moves, Gomory rounds, and
branch nodes). Both routes run this pipeline:

| Order | Operation | Result used by the caller |
| --- | --- | --- |
| 1 | Patch fractional integer values | A feasible assignment with fewer fractional integer variables, or no progress. |
| 2 | Check row divisibility using GCDs | A conflict, or no contradiction found. |
| 3 | Add a bounded sequence of Gomory cuts and recheck | A conflict, or a feasible tableau strengthened by the cuts. |
| 4 | Branch on fractional integer variables | An integer model, exhaustion of all branches, or a node-limit result. |

GCD checks and cuts run before branching, not at each branch node. A zero
cut or patch budget skips that stage. Even if patching makes every required
variable integral, the final success decision comes through branch-and-bound's
integrality check, which then needs no branch nodes.

A refuted candidate produces a clause over the original arithmetic atoms.
The shared driver adds it to the SAT backend and searches for another Boolean
candidate. It reports `unsat` for the whole query when that search is exhausted;
integer infeasibility of one candidate does not by itself refute a formula
with other Boolean choices.

The {retentionsource}`standalone recipe <src/smt/solver_engines.cpp#L10051-L10057>`
sets {retentionsource}`CombinationRecipe::forgettable_clauses <src/smt/component_solver.hpp#L24-L26>` to false:
its online external theory clauses remain permanent in the SAT backend, preserving the
standalone route's retention policy.
{retentionsource}`ComponentDispatcher <src/smt/component_dispatcher.cpp#L28-L39>` stores this policy
for its lifetime and {retentionsource}`applies it to external clauses <src/smt/component_dispatcher.cpp#L259-L262>`,
including after the driver {retentionsource}`rebuilds the dispatcher for new atoms <src/smt/component_solver.cpp#L264-L274>`. Combined recipes retain the default true
policy, which permits SAT clause deletion. This affects search and memory use,
not the validity of the theory clauses.

`QF_UFLIA` calls the same `complete_integer_assignment` operation through
{migrationsource}`complete_lia_integer_model
<src/smt/solver_engines.cpp#L8455-L8476>`. Its
{migrationsource}`LIA component adapter <src/smt/solver_engines.cpp#L9502-L9645>`
keeps the winning tableau, exports shared integer values, or returns a learned
clause/resource-limit result to the general combination driver. Rational
online propagation and final integer completion have separate responsibilities.
{migrationsource}`LiaTheoryComponent <src/smt/solver_engines.cpp#L9900-L10037>` owns this adapter and the optional online rational
execution state. {migrationsource}`ComponentAdapterSupport <src/smt/solver_engines.cpp#L9651-L9757>` owns the assignment view, registered
atoms, interface terms, and model-token revision. Atom growth destroys the
online execution state before changing the preparation it borrows; assignment
and backtracking invalidate the exported model token.

The recipe passes model-projection policy, {migrationsource}`LiaComponentTiming <src/smt/solver_engines.cpp#L8437-L8441>`
and the route's resolved `LiaSettings` ({ref}`lia-settings`) explicitly.
The adapter does not read QF_UFLIA combination options to select its behavior.
The combined recipe retains its check, rational, and integer timer keys; empty
keys disable timer sampling. Both rebuilt rational checks and completion of an
online tableau use {migrationsource}`LiaComponentAdapter::translate
<src/smt/solver_engines.cpp#L9588-L9635>` to replace the retained
model and return a consistent, learned-clause, or resource-limit result. If
requested projection omits a shared Int term, translation returns `Unknown`
with that diagnostic. Shared
model projection is produced only when the recipe requests it. The shared-sort
contract is Int-only.

The {ref}`combination chapter <the-component-collection-route>`
explains the remaining equality arrangements and model-agreement checks.

{integersource}`LiaCompletionResult <src/theory/arithmetic/lia_completion.hpp>` returns a typed `Sat`, `Unsat`, or `ResourceLimit`,
one accumulated `LiaExplanation`, the number of patched variables, and
`LiaPatchingStats`. The completion operation does not copy or prepare the
tableau and preserves the supplied integer-variable ordering. On success the
caller reads the winning assignment directly; after a failed or limited branch
search, cuts remain but speculative branch bounds are removed. It does not
restore the entry basis or assignment. The caller discards unsuccessful working
copies. A zero branch
budget permits an already integral assignment but no branch node.

Both routes use SMT integration's
{migrationsource}`integer_conflict_clause <src/smt/solver_engines.cpp#L8382-L8396>` to collect blocking
literals from the asserted arithmetic atoms. It handles an empty coarse clause
explicitly; otherwise the existing optional generalization checks the accumulated
explanation against that clause. Individual failed integer branches do not increment the
aggregate conflict budget; the SMT caller counts the reported theory conflict.
The {integersource}`patcher's fully_integral statistic
<src/theory/arithmetic/lia_patching.hpp>` uses its maintained remaining-variable
count, avoiding another scan. {migrationsource}`Both routes publish it <src/smt/solver_engines.cpp#L8372-L8380>` when patching is enabled.

With the cube test disabled, the completion boundary preserves the existing
stage order, variable order, limits, winning assignment, and antecedents from
cuts used by later branching. Enabling cubes inserts one private model attempt
between the basic GCD screen and cuts. It adds sparse preparation, a separate
LP and candidate validation; the other stages keep their existing contracts.

This design keeps integer branch choices inside the theory procedure. SAT
learns the resulting conflict clause, but does not see individual integer
splits as CDCL decisions. That simplifies the boundary and the lifetime of
temporary rows; it also means that internal branch choices and cuts are not
reused across successive Boolean candidates.

(patching-interval-reuse)=
### Patching interval reuse

{patchsource}`patch_integer_variables <src/theory/arithmetic/lia_patching.cpp>` owns an
`unordered_map<LraVarId, LraTableau::FreedomInterval>` for candidate selection.
Each {patchsource}`FreedomInterval <src/theory/arithmetic/lra_tableau.hpp>` entry
owns its optional exact `DeltaRational` endpoints; it borrows no
tableau rows or assignments. The first feasibility query for a moved nonbasic
computes its interval, and further candidates for that variable reuse it,
including candidates generated for other integer targets and lattice candidates.
The query remains before the fractional-candidate rejection, preserving existing
scoring and rejection-counter meanings.

The tableau's basis, bounds and assignment stay fixed during a selection attempt.
An accepted move can change intervals of other nonbasics through shared rows:
the whole map is cleared immediately after the move and before the next attempt.
No cache survives the patching call. Patching still never pivots. If future
candidate generation mutates the tableau, it must invalidate the map first.

For example, use `integer_vars = {x, z}` and start these integer nonbasics at
$x=z=1/2$ with continuous row $s=x+z$
and bound $s\ge1$. Candidate selection chooses $x:=1$ first. This changes
$z$'s lower freedom endpoint from $1/2$ to $0$, so the next selection chooses
$z:=0$. Retaining $z$'s old interval would choose a different second move.

The new `:lia.patch-interval-requests`, `:lia.patch-interval-computations`
and `:lia.patch-interval-cache-hits` counters obey requests = computations + hits.
Computations equal the number of distinct queried variables summed over attempts,
including a final unsuccessful attempt. Cache lookups have expected constant
cost. Live entries are linear in the distinct queried variables in the current
attempt; bucket storage can remain at the peak reached earlier in the call
because `clear()` retains the bucket allocation.
The underlying interval query still searches coefficients in affected rows;
this change does not make that query constant-time or replace its representation.

This is a Primo engineering adaptation of the derived-information maintenance
principle in Bromberger (2019), §7.1.4, pp. 225–226 {cite:p}`bromberger2019`,
not an implementation of the thesis's `VarInfo` scheme or a new patching
heuristic. The [Bromberger checklist](../papers/bromberger-linear-arithmetic.md#efficiency-review-checklist)
states the invariant and test obligations. Tests in
{patchsource}`lia_patching_tests.cpp <tests/unit/lia_patching_tests.cpp>` cover cross-target reuse, both candidate
families, exact strict/unbounded endpoints, accepted-move prefixes and invalidation
through a shared row. Snapshot `cab1ebc4` is qualified against `75e10877` on the
selected diagnostics/development sets; the [measurement and correctness record](../evidence/arithmetic-overhead-20261109.md)
includes fresh generated/mutation gates, every successful native SAT timing
row's direct model check and supplementary runtime controls. This is scoped
acceptance, not a new search heuristic or an overall reference-gap claim.

(lia-cubes)=
## Optional cube model attempt

`--lia-cube=uniform` enables the sufficient model test in Bjørner and
Nachmanson, CAV 2024, §4.2. For each integer inequality $a z\le b$, solve
$a z\le b-\frac12\sum_j|a_j|$ in an isolated rational tableau and round each
semantic coordinate with $R(t)=\lfloor t+1/2\rfloor$. The rounding error in
one row is at most half its coefficient 1-norm, so the rounded point satisfies
the original row. `--lia-cube=difference` leaves primitive integer difference
rows $x-y\le k$ unstrengthened. Monotonicity and $R(y+k)=R(y)+k$ preserve these
rows even at negative ties. General octagon refinements are not implemented.
Both modes are experimental; `off` remains the default. QF_UFLIA ignores these
controls and retains its existing pipeline.

The owning numerical snapshot {cubesource}`LiaCubeProblem
<src/theory/arithmetic/lia_cube.hpp>` contains the ordered semantic integer
coordinates, sparse definitions of every original variable in those coordinates,
and the original basic-variable flags. {cubesource}`SMT preparation <src/smt/solver_engines.cpp>` exports it from the
pristine `PreparedLraTheory::base_tableau` for a fractional completion input; it contains
no terms or references into preparation storage. The helper obtains active
bounds from the rationally feasible working tableau. It substitutes exactly
fixed integral semantic coordinates, clears coefficient denominators with
positive primitive scaling, and applies delta-aware floor to normalize strict
or fractional thresholds over integer values. Slacks are affine expressions,
never independent integer coordinates for computing margins. A fixed fractional
semantic coordinate skips the attempt, leaving refutation to the existing stages.

{cubesource}`try_cube_model <src/theory/arithmetic/lia_cube.cpp>` builds a
scratch Bland tableau. {cubesource}`check_bounded
<src/theory/arithmetic/lra_tableau.cpp>` returns
`Feasible`, `Infeasible`, or `LimitReached`; its finite repair-step budget also
counts SOI updates for other callers, although cubes themselves use Bland.
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
no strengthened-system conflict enters a theory explanation. Ordinary cuts
and DFS resume after any such failure. The call occurs once after patching
and the basic GCD check, before any cuts, and skips an already integral model.

Snapshot construction sorts $n$ coordinates in $O(n\log n)$ comparisons;
sparse export, normalization and candidate checks then take linear sparse
work apart from exact-arithmetic costs. Scratch/adoption construction also
uses the tableau API's row sorting; no total linear-time LP construction is
claimed. Each fractional enabled completion allocates
a snapshot and may allocate extra tableaux; no speedup is assumed. {cubesource}`Counters <src/util/statistics.cpp>` named `lia.cube-*`
separate calls, skips, constraints, fixed coordinates, difference exemptions,
pivots, centers, infeasible LPs, exhausted limits, rejected candidates and models.
`lia.cube-microseconds` times the helper, excluding SMT snapshot construction.
`lia.branch-nodes` counts subsequent private split nodes. A nonzero rejected
candidate counter requires investigation; it is not evidence of a successful
cube mechanism. {cubesource}`Unit tests <tests/unit/lia_cube_tests.cpp>` cover the
paper example, thin feasible failures, negative ties, scaled difference rows,
fixed substitution, strict bounds, large exact coefficients, budget preservation
and rejected adoption. The QF_LIA integration test checks a false atom and
printed-model reconstruction. See {ref}`the qualification and measured comparison <lia-cube-evidence>`.

(lia-input)=
## Input lowering and integer bounds

[Preprocessing profiles](preprocessing.md#profiles-and-repetition) explains the
automatic and explicit integer pass schedules. The transformations below run
even when the optional repeated round is disabled.

Here, **lowering** means translating an input construct into simpler terms
and constraints supported by the solving stages. Each translation below
specifies the replacement and any fresh variables it introduces.

The {source}`frontend <src/smt/frontend.cpp>` admits linear `Int` expressions
and comparisons under `QF_LIA`; uninterpreted functions require `QF_UFLIA`.
The integer logics reject `Real` declarations. Products must have a constant
coefficient. Primo also accepts constant-divisor `div`/`mod` and integer
`abs` as extensions to its QF_LIA input; that is not a claim of strict QF_LIA
grammar conformance.

{source}`Extended-term lowering <src/smt/preprocessing.cpp#L4468-L4616>`
runs even when optional preprocessing is off. For positive integer $k$, a
quotient $q$ for `div(t,k)` is constrained by

$$0\le t-kq\le k-1.$$

`mod(t,k)` becomes the residual $t-kq$. Equal numerator/divisor pairs share
one quotient. The inequalities characterize a unique integer quotient and
a remainder in $[0,k-1]$: two distinct quotients would make the remainders
differ by at least $k$, which that interval cannot contain. For instance,
$-7=3(-3)+2$, so the quotient is $-3$ and the remainder is $2$.
{source}`Term construction <src/core/term_manager.cpp#L364-L393>` normalizes
a negative divisor by negating the positive-divisor quotient and preserving
the remainder. Zero and nonconstant divisors are rejected.

`abs(t)` is lowered through `ite(0 <= t, t, -t)`. Non-Boolean `ite` introduces
a fresh constant with guarded equalities to its two possible values.
Equality lowering, despite its `lower_real_equalities` name, also handles
`Int`: $a=b$ becomes $(a\le b)\land(b\le a)$. Negating the equality therefore
lets SAT choose a false non-strict comparison. Integer strictness must account
for that polarity, not just syntactic `<` atoms.

{source}`bound_assertion <src/smt/solver_engines.cpp#L3193-L3220>` computes
the bound direction and strictness from the normalized relation, orientation
and truth value. When the **bounded variable** is known to be integer, it uses

$$x<r\iff x\le\lceil r\rceil-1,
\qquad x>r\iff x\ge\lfloor r\rfloor+1.$$

Thus `3*x < 5` yields $x\le1$, while a false `3*x <= 5` yields $x\ge2$.
Subtracting or adding one to the rational threshold $5/3$ would be wrong.

With `--no-lia-round-integer-bounds`, the QF_UFLIA default, non-strict
bounds keep their rational threshold: a true `3*x <= 5` asserts
$x\le5/3$. With `--lia-round-integer-bounds`, the QF_LIA default, the same
integer-valued variables also get rounded non-strict bounds,

$$x\le r\iff x\le\lfloor r\rfloor,\qquad x\ge r\iff x\ge\lceil r\rceil,$$

so every bound on such a variable is integral (King §4.3; Achterberg et al.
§4.2 {cite:p}`achterberg2020presolve`). The bound keeps the atom's own
literal as its reason. Each prepared atom stores an `IntegerBoundRounding`
mode (`None`, `StrictOnly` or `All`), and every assertion, entailment check,
propagation and model-phase hint reads it through `bound_assertion`, so all
uses agree. In `3*x <= 5`, `3*y <= 5`, `x + y >= 3` the rounded bounds
$x,y\le1$ refute the candidate in the rational check, without integer
completion. The option is on by default for QF_LIA since `5dec036` and off
for QF_UFLIA; see the
[integrality checklist](../papers/mip-presolve-integrality.md).

{source}`PreparedLraTheory::append_atom
<src/smt/solver_engines.cpp#L3529-L3656>` marks direct bounds on `Int` terms
as integer. For compound polynomials its sufficient condition without
`--lia-primitive-int-rows` is more
conservative: the input is integer-sorted and the normalization scale is
$1$ or $-1$. Other auxiliary variables use the real strict-bound encoding,
even if a stronger analysis could prove some of them integer-valued.

With `--lia-primitive-int-rows`, `Int` atoms are stored with coprime integer
coefficients and a positive leading coefficient instead: `2*x + 3*y <= 5`
gets the row $s=2x+3y$ rather than $s=x+\tfrac32y$ (King §4.3). Every
coefficient is then an integer over `Int` variables, so the slack is integer
in every integer model and is marked integer whatever the scale (King §4.3,
footnote 4). Proportional atoms of either sign still share one row; Real
atoms keep leading-coefficient-one normalization. Together with
`--lia-round-integer-bounds` this gives the GCD rounding of Achterberg et
al. §3.5: `2*x + 4*y <= 5` becomes $x+2y\le2$. In the parity case
$2x+4y=3$, written as two inequalities, the rounded bounds $2\le s\le1$
conflict in the rational check. In a case with $13/2\le2x+3y\le7$ over
unbounded variables, with both options off (the QF_UFLIA default),
completion exhausts its branch-node limit, while the integral bound $s=7$
lets one patch move find $x=2,y=1$. Both options are on by default for
QF_LIA and off for QF_UFLIA. The SLURM evaluation at `2e120fa` found no
wrong answer. It also found that `--lia-primitive-int-rows` loses 30 QF_UFLIA
`wisas` files ({ref}`integrality evaluation <integrality-evaluation>`). The
cause, traced in four of the files, is search order: the leading-coefficient form escapes
them because its first commitment is fractional and cheap to refute.

A slack marked integer this way can also become an integer obligation of
completion. `--lia-integer-slacks` puts such slacks (Achterberg et al. §7.6
call them implied integers) at the front of `integer_vars`. Patching, the GCD
test and Gomory row selection may then use their rows, and branch-and-bound,
which splits the first fractional variable, can split a constraint
expression: for $13/2\le2x+3y\le27/4$, the single split $s\le6\lor s\ge7$
refutes the candidate, with the two bound literals as its explanation, while
splitting only $x$ and $y$ exhausts a 50-node budget. A slack appended after
the inputs would never be split, because it is integral whenever they are.
On QF_LIA at 60 s, this option together with the two above solves 197 fewer
files than all options off, and 301 fewer than the two above alone. Adding
`--lia-patch-integral-moves` as well gives 9,798 solved files, the most of any
configuration ({ref}`integrality evaluation <integrality-evaluation>`).

(lia-infinitesimals)=
### Why the relaxation can still contain infinitesimals

The current invariant is that a **completed integer assignment** has no
infinitesimal component. Intermediate relaxation assignments can have one,
including on variables corresponding to `Int` terms.

For example, let $P=2x+3y$ with integer $x,y$. Normalizing by the leading
coefficient $2$ introduces $s=x+\tfrac32y=P/2$. Consider $P<0$:

1. The current fallback represents it as $s\le-\delta$. The initial
   assignment $x=y=s=0$ violates this bound.
2. If simplex pivots on $x$, the row becomes $x=s-\tfrac32y$. Moving
   $s$ to $-\delta$ gives $x=-\delta$, $y=0$. Every real constraint holds,
   but $x$ is not integer-valued.
3. Patching can move $s$ to $-1$, giving $x=-1$, $y=0$. This satisfies the
   bound and makes both integer variables exact integers.
4. If patching does not resolve the assignment, branch-and-bound uses
   $\lfloor-\delta\rfloor=-1$ and splits on $x\le-1$ or $x\ge0$.
   The first branch admits that same integer solution.

This is a possible tableau trace, not a promised preprocessing or pivot order.

The {source}`integer-value helpers
<src/theory/arithmetic/lia_integer_values.hpp>` require both an integral
rational part and a zero infinitesimal part. In particular,
$\lfloor n-\delta\rfloor=n-1$ and
$\lceil n+\delta\rceil=n+1$ for integer $n$. Treating $n-\delta$ as having
floor $n$ would make branching repeatedly choose an already-satisfied bound.
Dutertre–de Moura's technical report §4.1, pp. 20–21, gives this delta-aware
floor for branch-and-bound {cite:p}`dutertre2006simplexreport`.
Patching targets exact integers; branch bounds have zero infinitesimal
components. The cut generator skips a basic value with a nonzero
infinitesimal, since its rational fractional-part formula does not cover
that case. The {ref}`regression map <lia-models-evidence>` links the existing
branching, patching and cut guards.

There is room to avoid introducing these infinitesimals. Since $P$ is
integer-valued, $P<0$ is equivalent to $P\le-1$, giving $s\le-1/2$ without a
delta. This must use the spacing of $P/2$: rounding $s$ as if it were an
integer would incorrectly require $s\le-1$ and exclude $x=1,y=-1$, where
$s=-1/2$. The strengthened relaxation can still have fractional assignments;
integer completion remains necessary.
King's thesis §4.3 describes the underlying integer tightening, including
strict bounds obtained from negated SAT atoms {cite:p}`king2014`.
[Preserving this spacing during bound preparation](../leads/lia-infinitesimals.md)
is an open lead for the leading-coefficient normalization, which QF_UFLIA
still uses. Under `--lia-primitive-int-rows`, the QF_LIA default, the slack is $P$ itself, so a strict compound
`Int` atom becomes an integral non-strict bound and no infinitesimal is
introduced. Simply discarding a nonzero delta would change a strict
inequality and is not a valid replacement.

(lia-patching)=
## Patching a feasible assignment

{source}`patch_integer_variables <src/theory/arithmetic/lia_patching.cpp>`
tries to find an integer model by moving nonbasic values without violating
any bounds. It derives from Bjørner and Nachmanson §4.1, with a bounded
candidate search and a global improvement criterion specific to Primo.

For a nonbasic variable $y$, its **freedom interval** is the intersection of
its own bounds with the intervals induced by every basic row containing $y$.
If $x_b$ has coefficient $a\ne0$ for $y$, moving $y$ by $\Delta$ changes
$x_b$ by $a\Delta$. To set $x_b$ to an integer $n$, the candidate move is

$$\Delta=\frac{n-\beta(x_b)}{a},$$

where $\beta$ denotes the current assignment. A candidate is admissible only
if the proposed value of $y$ lies in its freedom interval. This preserves
both row equations and feasibility; patching does not call simplex to repair
an inadmissible move. A fixed variable has a singleton freedom interval and
cannot move away from its fixed value.

Primo also considers directly moving a fractional nonbasic integer variable.
Unlike the simplifying assumption in the paper, nonbasic variables here may
sit inside their bounds, and even an integer variable's bound may be fractional.
For each fractional target, the search considers eight integers around its
delta-aware floor $m$: $m,m+1,m-1,m+2,\ldots,m-3,m+4$, ordered by distance
from the rational part. This is a heuristic window, not an exhaustive search
of its feasible values.

The width is currently the hard-coded `kTargetWindow = 4`; the existing
`--lia-patch-limit` controls accepted moves, not this width. A
[separate open lead](../leads/lia-patching.md) proposes a window parameter
and compares more selective candidates with
[MIP rounding and repair heuristics](../papers/lia-primal-heuristics.md).

Every candidate is scored by the total number of required integer variables
that would remain nonintegral. Only a strict reduction is accepted. Among
inspected candidates with the same count, a smaller rational move wins;
the scan stops early on finding a move that makes the whole assignment
integral. Each accepted move decreases a nonnegative integer, so the sweep
terminates even apart from its configured move budget. Without
`--lia-patch-integral-moves`, the QF_UFLIA default, it can make an individual previously integral variable fractional, provided the
total count improves. An integral assignment is still subject to the
ordinary model checks.

`--lia-patch-integral-moves` (on by default for QF_LIA) removes that freedom for
integer nonbasic variables: a candidate that would leave one at a
non-integer value is rejected. In exchange, an integer nonbasic $y$ with
coefficient $a=p/q$ in the row of a fractional basic $x_b$ also gets
**lattice candidates**. With $c=\beta(x_b)-a\beta(y)$, setting $y=k$ gives
$x_b=c+ak$, which is integral exactly when $c$ has no infinitesimal part,
$qc$ is an integer and $pk\equiv-qc\pmod q$. The solutions are $k_0+jq$;
$k_0$ uses the inverse of $p$ modulo $q$, computed by an iterative extended
Euclidean algorithm over integer-valued `Rational`s (`numerator()` and
`denominator()` give $p$ and $q$). The eight nearest to $\beta(y)$ are scored
like any other candidate. For $s=\tfrac{11}{3}y$ with $y=1$, the target
window only offers fractional $y$, while the lattice proposes $y=3$, $s=11$.
This generator is Primo's own inference, adapted from the modular-inverse
reduction of Achterberg et al. §3.5; it is not a procedure from the patching
paper. Counters `:lia.patch-fractional-candidates-rejected`,
`:lia.patch-lattice-candidates-scored` and `:lia.patch-lattice-moves-accepted`
record its work.

### Why a nearest move is insufficient

Use the paper's Example 3:

$$y=x/2,\qquad z=x/3,$$

with $3\le x\le10$, $-3\le y\le4$, $-5\le z\le12$, and initial
values $(x,y,z)=(3,3/2,1)$. All three variables must be integers.
The freedom interval of nonbasic $x$ is $[3,8]$: $y\le4$ supplies the tighter
upper endpoint. Moving $x$ to $4$ fixes $y$ but makes $z=4/3$ fractional;
the total remains one fractional variable, so Primo rejects that move.
Moving $x$ to $6$ gives $(6,3,2)$ and reduces the count to zero. Considering
only the floor and ceiling of $y=3/2$ would miss that target.

The {source}`scoring index <src/theory/arithmetic/lia_patching.cpp>`
stores, for each nonbasic variable, the basic integer rows that contain it.
Scoring adjusts a cached global count using only the moved variable and
those affected rows. No pivot occurs during the sweep, so this incidence
index stays valid. The scoring cost per candidate is proportional to the
affected integer column, rather than all integer variables; generating
candidates and calculating freedom intervals still have their own costs.

{source}`LiaPatchingStats <src/theory/arithmetic/lia_patching.hpp#L10-L26>`
exposes occurrence entries, candidates scored, affected rows inspected,
the equivalent full-scan work, and accepted moves that left an integer
nonbasic variable fractional. The public counters include
`:lia.patch-occurrence-entries`, `:lia.patch-candidates-scored`,
`:lia.patch-affected-rows-inspected`, `:lia.patch-full-scan-equivalent` and
`:lia.patch-fractional-nonbasic-moves`.
The {source}`patching tests <tests/unit/lia_patching_tests.cpp>` cover the
paper example, fixed and strict bounds, and comparison of indexed scoring
with an independent full-scan definition.

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

The [bounded-variable GCD extension](../leads/lia-extended-gcd.md) is an open
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

Write the row as $x_b=\sum_j a_jx_j$, let $v=\beta(x_b)$, and define
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

(lia-branching)=
## Branching and backtracking

{source}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>`
starts from a rationally feasible tableau. It scans `integer_vars` for the
first nonintegral assignment $v$ and splits on

$$x\le\lfloor v\rfloor\quad\lor\quad x\ge\lfloor v\rfloor+1.$$

Every integer satisfies one side, and the current fractional value satisfies
neither. At $v=7/3$, for example, the branches are $x\le2$ and $x\ge3$.
Each side is asserted as an ordinary tableau bound and checked by simplex.
A feasible side is searched further; a conflict closes that side. After both
sides close, the search backtracks to the previous frame. It tries the lower
side first and uses the caller's variable order, without a nearest-fraction
or activity heuristic.

The explicit stack stores checkpoints rather than tableau copies per node.
On `Sat`, the tableau stays at the winning leaf, including the successful
branch bounds, so the caller can extract the model. On `Unsat` or
`LimitReached`, the procedure removes its speculative bounds. With the
default sparse simplex, popping restores bounds rather than the previous
assignment and basis. The optional SOI path also trails assignments and
pivots; see [LRA backtracking](lra.md#incrementality-and-rollback).
Callers should rely on the bound-restoration contract, not require identical
internal tableau states.

The node counter increments when a new split is created, not for every
simplex pivot or every attempted side. Integrality is checked before the
limit, so an already integral assignment succeeds even with a zero node
budget. The search stack grows with depth; the node cap bounds the number
of splits in this call but is not a wall-clock or bit-complexity bound on
the intervening exact arithmetic.

(lia-explanations)=
## Explaining integer conflicts to SAT

The coarse conflict clause contains the blocking literal of every arithmetic
atom used in the current candidate. If their conjunction is integer-infeasible,
its negation is a valid clause. A smaller clause can exclude more Boolean
candidates, so Primo normally attempts to use the integer derivation's
antecedents instead.

{source}`LiaExplanation <src/theory/arithmetic/lia_explanation.cpp>` stores
literals already signed for the outgoing clause, following the tableau's
blocking-literal convention. It drops the synthetic literal `0` used for
cuts and split bounds. Dropping it is justified by different arguments:

- A GCD conflict includes both reasons fixing each substituted variable.
- A cut includes the reasons for the occupied bounds used in its derivation.
  They are recorded when the cut is added, even if that cut does not immediately
  cause a conflict. A later cut or branch conflict may depend on it.
- A split is an integer tautology. If assumptions $A$ refute $x\le n$ and
  assumptions $B$ refute $x\ge n+1$, then $A\land B$ is impossible because
  those cases cover all integers. After **both** sides close, their antecedents
  suffice without mentioning either temporary split bound.

For example, let $p$ mean the normalized bound $x\ge1/2$ and $q$ mean
$x\le1/2$, with integer $x$. These can arise from `2*x >= 1` and `2*x <= 1`.
The lower branch $x\le0$ conflicts with $p$; the upper branch $x\ge1$
conflicts with $q$. Combining the closed sides yields $\neg p\lor\neg q$.
Keeping only the first conflict and dropping its branch assumption would
incorrectly yield $\neg p$. The implementation emits the accumulated clause
only after integer infeasibility, never after a model or a resource limit.

The explanation can contain reasons from cuts or closed branches unnecessary
to the final proof. Such extra disjuncts weaken a valid clause; they do not
invalidate it. This is conservative explanation collection, not minimal-core
extraction or a replayable external proof certificate.

{source}`generalized_lia_lemma
<src/smt/solver_engines.cpp#L8334-L8360>` checks that the explanation is
nonempty and that each literal occurs in the coarse clause. Otherwise it
falls back to the coarse clause. This catches foreign or synthetic literals
and ensures that the chosen clause blocks the candidate. **Containment alone
does not prove theory validity**: an arbitrary subset of a valid clause can
be invalid. Soundness still depends on the GCD, cut and exhaustive-branch
arguments above. The {source}`explanation regressions
<tests/unit/lia_explanation_tests.cpp>` exercise antecedent retention,
synthetic-reason removal and collection across stages.

The counters `:lia.generalized-lemmas`, `:lia.generalized-lemma-literals`,
`:lia.coarse-lemma-literals` and `:lia.generalization-fallbacks` describe the
clauses emitted. A smaller clause can change the SAT search substantially;
it is not by itself evidence of a performance improvement.

(lia-limits)=
## Budgets and incomplete cases

The integer settings below apply to both the pure and the combined route.
Each route resolves its own values, as described in {ref}`lia-settings`.

| Option | Default | Meaning per integer completion |
| --- | --- | --- |
| `--lia-cube` | `off` | Pure QF_LIA only: `uniform` or `difference` model attempt before cuts; failure falls through. |
| `--lia-cube-steps` | `1000` | Optional LP repair-step cap; zero allows only an already feasible center. |
| `--lia-patch-limit` | `100` | Maximum accepted patch moves; zero disables patching. |
| `--lia-gomory-cut-limit` | `100` | Maximum cut-and-recheck iterations; zero disables cuts. Exhaustion falls through to branching. |
| `--lia-branch-limit` | `100000` | Maximum new split nodes. Exhaustion before finding a model or refuting the tree returns a resource-limit result. |
| `--no-lia-conflict-generalization` | Generalization on | Disables the default attempt to replace coarse clauses with accumulated explanations. |
| `--lia-round-integer-bounds` | On for QF_LIA, off for QF_UFLIA | Rounds non-strict thresholds on integer-valued tableau variables inward. Preparation-time, not a per-completion budget. |
| `--lia-primitive-int-rows` | On for QF_LIA, off for QF_UFLIA | Stores `Int` rows with coprime integer coefficients, making every `Int` slack integer-valued. Preparation-time. |
| `--lia-integer-slacks` | On for QF_LIA, off for QF_UFLIA | Adds slacks proved integer to the front of the integer obligations of completion. |
| `--lia-patch-integral-moves` | On for QF_LIA, off for QF_UFLIA | Keeps integer nonbasic variables integral while patching and adds lattice candidates. |

The four patch/cut/branch/generalization controls apply to both routes. The last four switches are on by
default for QF_LIA and off for QF_UFLIA, as {ref}`lia-settings` explains.
Each also has a `--no-` form, for example `--no-lia-primitive-int-rows`,
which turns it off on either route. If both forms are given, the `--no-` form
wins, as for the legacy `--lra-model-phase` / `--no-lra-model-phase` pair.
The current `--arithmetic-model-phase=on|off|auto` form rejects mixing with
those legacy flags; see [option compatibility](../reference/options.md#phase-option-names-and-compatibility).

These are finite operational limits, not empirically optimal settings.
Both routes report typed `ResourceLimit` to the shared driver, producing
`unknown` with the diagnostic `integer branch-and-bound node limit`. An
unfinished search must not become `unsat`. Cancellation and the aggregate
theory-conflict budget remain SMT-driver outcomes, independent of internal
failed integer branches.

Consider unbounded integers $x,y$ with $2\le4x+4y\le3$. No multiple of four
lies in that interval, yet the relaxation admits $x+y=1/2$. The normalized
auxiliary variable $s=x+y$ is bounded, not fixed, so the current GCD check
does not refute the interval. With unbounded nonbasic terms there is no
eligible initial Gomory row. Patching cannot produce an integer model, and
branching can keep translating the fractional assignment along the unbounded
direction. The {source}`node-limit regression
<tests/unit/qf_lia_solver_tests.cpp#L252-L274>` preserves this incomplete case.
Adding GCD checks and cuts before DFS does not supply a completeness proof.

QF_LIA's default refutes this example during preparation. Primitive rows
store the row as $x+y$ with bounds $[1/2, 3/4]$, and rounding them inward
gives the empty range $[1, 0]$. The {settingssource}`route-default regression
<tests/unit/qf_lia_solver_tests.cpp#L280-L301>` checks this, and checks that
the same input under QF_UFLIA still returns unknown within the same 20-node
budget. Rounding helps
only when one row's bounds leave no integer value; it is no completeness
argument either.

Dillig, Dillig and Aiken's *Cuts from proofs* {cite:p}`dillig2011cuts`
provides a different branching rule based on integer-infeasible intersections.
Its §3.3 termination argument uses satisfiability-preserving finite variable
bounds and bounded split coefficients; it is not a guarantee for arbitrary
unbounded cut generation. The [paper overview](../papers/cuts-from-proofs.md)
derives the split for the example above and identifies the mechanisms missing
from Primo.

Bromberger's thesis, Chapter 6, addresses precisely the difference between
bounded row expressions and bounded variables. Its Double-Bounded reduction
and Mixed-Echelon-Hermite transformation provide a route to a finite integer
search while preserving solutions through a change of coordinates. They are
not implemented here. The {ref}`thesis reading guide <bromberger-bounded-search>`
works through this example in coordinates $u=x+y$, $v=y$ and identifies the
remaining implementation obligations.

Extended bounded-variable GCD reasoning, general Diophantine or
Hermite-normal-form reasoning, cuts within branch nodes, and CDCL-visible
integer splits remain outside the implementation. The optional cube test is
implemented for pure QF_LIA as described below; its failure is never an
integer-infeasibility proof. The {ref}`cube-test explanation
<bromberger-unit-cube>` derives its strengthening, rounding guarantee and
equality limitation. The [cube integration note](../leads/lia-cube-test.md)
records the paper checklist and qualification plan. The
{source}`earlier thesis note <docs/papers/bromberger-linear-arithmetic-thesis.md>`
and {source}`integer open leads <docs/open-leads.md#L1765-L1834>` retain the
separate investigations and their prerequisites.

`QF_LIRA`, mixing declared `Int` and `Real` variables, is not admitted. The
stage APIs already take an integer subset, and fractional auxiliary variables
exercise some mixed-tableau cases. That does not establish end-to-end QF_LIRA
support: contextual numeral typing, coercions such as `to_real`, frontend
admission and combined model construction still need design and validation.

(lia-settings)=
## Settings and who sets them

The engine reads its integer settings from one value,
{settingssource}`LiaSettings <src/smt/lia_settings.hpp>`. It has one field
for each row of the {ref}`budget table <lia-limits>`, for example
`patch_limit` for `--lia-patch-limit` and `primitive_int_rows` for
`--lia-primitive-int-rows`.

- **Ownership.** Each `LiaTheoryComponent` holds a copy by value and gives one
  to its `LiaComponentAdapter`. Neither reads the session's `CheckSatOptions`
  for these settings.
- **No knowledge of the caller.** The engine does not know which logic,
  command line or strategy chose the values.

Choosing the values is a separate step. In the order the data flows:

1. The {settingssource}`command line <src/main.cpp#L1034-L1056>` records an
   override in `CheckSatOptions::lia`, a `LiaSettingOverrides` whose fields
   are all optional. It does this only for a flag that is given, including an
   explicit `--lia-round-integer-bounds=false`.
2. {settingssource}`make_session_solver_strategy <src/smt/solver_leaves.cpp#L27-L30>`
   calls `resolve_lia_settings` once for each route that runs the engine:
   `LiaRoute::QfLia` and `LiaRoute::QfUflia`. The declared logic selects the
   route, so a route default is a per-logic default.
3. The {settingssource}`policy <src/smt/lia_settings_policy.cpp>` starts from
   `default_lia_settings(route)` and applies every override that is set. An
   explicit value therefore wins over the route default, except that the
   experimental cube controls are applied only to the QF_LIA route.

The {settingssource}`policy <src/smt/lia_settings_policy.cpp#L7-L20>` gives
QF_UFLIA the engine defaults, `LiaSettings{}`. QF_LIA additionally turns on
the four integrality switches of the [integrality
page](../papers/mip-presolve-integrality.md). They are on since `5dec036`.
The page's {ref}`promotion <integrality-promotion>` section gives the
evidence: in a paired rerun they solve a net 53
more QF_LIA files, but they lose 31 QF_UFLIA files, so only the QF_LIA route
uses them.

Making the policy a separate function did not by itself change any result. A
paired 10 s run of `5aaca1f`, which split the settings out with both route
defaults still off, against `2e120fa` found no answer or search-counter
difference. That held with default options and with all four switches on,
on the full QF_LIA and QF_UFLIA corpora. The only differences in the solved
sets were files at the time limit. Fuzz gates with both option sets found no
wrong answer and no invalid model (run-log entry
`2026-11-04-lia-settings-preservation-and-fuzz-5aaca1f` in the
{source}`run log <docs/run-log.json>`).

The `--help` text takes each switch's default from `default_lia_settings`
({settingssource}`lia_route_default_text <src/main.cpp#L200-L209>`). It shows
`[default: on for QF_LIA, off for QF_UFLIA]` and follows any later policy
change. The default column of the table above and the CLI help snapshot
must still be updated by hand. The documentation checks compare the snapshot
against the binary.

The QF_LIA and QF_UFLIA solver steps each capture their resolved settings by
value. The solver-dispatch leaf renders both sets as strategy parameters, so
the strategy's canonical form and fingerprint record which configuration each
route runs ({ref}`tactics-lia-settings`). That holds only while
`lia_strategy_parameters` renders every field, so a new setting must be added
there too.

Three regressions in {settingssource}`tactic_tests.cpp <tests/unit/tactic_tests.cpp#L1158-L1280>`
pin this design:

- `lia_settings_policy_resolves_route_defaults_and_overrides` checks both
  route defaults, that each override wins against either default, and that an
  explicit `false` turns a QF_LIA default off.
- `lia_overrides_change_the_solver_strategy_identity` checks that the
  canonical form records the two routes' different defaults, and that a
  changed override changes the fingerprint.
- `lia_route_settings_reach_the_engine` checks the route defaults and the
  overrides at the engine. On $2x+2y=3$, QF_LIA's default refutes the
  equation during preparation without patching, and QF_UFLIA's default
  patches. On QF_LIA, the switches turned off bring patching back, and a
  patch budget of 0 then disables it.

The stage tests in
{settingssource}`qf_lia_solver_tests.cpp <tests/unit/qf_lia_solver_tests.cpp#L19-L36>`
run under `set-logic QF_LIA`, but they test the engine's stages with the
switches off. They therefore build their options from `engine_defaults()`
rather than taking the route default.

(lia-models-evidence)=
## Models, regression coverage and retained evidence

An integer result requires every variable in `integer_vars` to have a zero
infinitesimal part and an integral rational part. Both routes export the
winning values through `LiaTheoryComponent` to
{migrationsource}`assemble_component_model <src/smt/component_model_assembler.cpp#L494-L587>`.
The shared assembler restores preprocessing conversions and completes declared
Boolean constants even when rewriting removed their last occurrence. The driver
supplies assignments for otherwise unowned Boolean constants that remain in the
SAT abstraction.
Combined models must additionally agree with EUF on evaluated function
arguments and shared values; see [model construction](models.md).

| Obligation | Existing regression coverage |
| --- | --- |
| Find a model, close both sides, respect the budget, handle $n-\delta$ | {source}`Branch-and-bound tests <tests/unit/lia_branch_and_bound_tests.cpp>` |
| Guard every free row variable before using integrality | {source}`GCD tests <tests/unit/lia_gcd_consistency_tests.cpp>` |
| Exclude fractional vertices while retaining legal integer points | {source}`Gomory tests <tests/unit/lia_gomory_cuts_tests.cpp>` |
| Preserve feasibility and score all affected integer variables | {source}`Patching tests <tests/unit/lia_patching_tests.cpp>` |
| Keep antecedents of cuts used by later conflicts | {source}`Explanation tests <tests/unit/lia_explanation_tests.cpp>` |
| Strict polarity, normalization, extensions, model completion and stage controls | {migrationsource}`QF_LIA tests <tests/unit/qf_lia_solver_tests.cpp>` |
| Integer completion within equality/model combination | {source}`QF_UFLIA tests <tests/unit/qf_uflia_solver_tests.cpp>` |

Tests of hand-built tableaux exercise algorithm preconditions; whole-solver
tests exercise admission, lowering and route integration. Fuzzing must cover
both generated inputs and mutations of real formulas. Historical failures
involved large coefficients and repeated proportional polynomials whose
nearby thresholds shared a fractional auxiliary variable. Small-coefficient
generator campaigns missed them, and merely increasing coefficient width
also missed the original failure in a recorded probe. The lasting requirement
is structural diversity, including pivoted auxiliary variables and original
input model validation; see the [correctness guide](../development/correctness.md).

The [section preservation map](../coverage/lia.md) accounts for the original
chapter, including repaired bugs, competing designs, experiment losses and
incomplete gates. Those records remain attached to their measured revisions
in {source}`the development history <docs/qf-lia-branch-and-bound.md>` and
{source}`run log <docs/run-log.json>`. For example, the August patching
comparison reports gains and losses, and the generalization comparison records
a severe regression on one `rings` instance. Neither fewer fractional values
nor shorter lemmas guarantees a faster subsequent search.

Historical solved counts exclude clean `unknown` results and timeout rows,
even if an answer was printed before termination. This chapter documents
the current mechanism; it does not rerun those campaigns or promote their
measurements to results for the current source revision.

(lia-sources)=
## Sources and implementation status

The local paper-to-code checklist is the stage order, guards, derivations,
state contracts and regression obligations above. The primary sections used
for this chapter are identified in the bibliography; the table records the
specific sections and their implementation status.

| Source | Implemented subset and boundary |
| --- | --- |
| *The Omega Test*, expanded CACM 1992 version of the 1991 paper, §2.1 {cite:p}`pugh1992omega`; *An Effective SMT Engine for Formal Verification*, thesis, §2.6.1, Algorithm 2.6 {cite:p}`griggio2009` | Earlier uses of the classical GCD divisibility criterion. Added for attribution; Primo's row-local check does not implement their equality-elimination procedures. The retained {source}`Griggio thesis note <docs/papers/griggio-mathsat-thesis.md>` covers the broader equation handler. |
| Nikolaj Bjørner and Lev Nachmanson, [*Arithmetic Solving in Z3*](https://doi.org/10.1007/978-3-031-65627-9_2), CAV 2024, §§4.1–4.4 | Freedom-interval patching, basic row GCD consistency and integer splits. Primo adds a finite target window and a strictly improving global patch score, and branches inside a private theory search. Uniform cubes and the difference refinement are optional in pure QF_LIA; general octagons, extended GCD and specialized cuts remain unimplemented. |
| *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*, thesis, §§4.3, 4.4.3, Lemma 4.1, equation (4.9) {cite:p}`king2014` | The mixed-cut derivation, represented by a fresh tableau row and bound. Nonbasic integrality is not used to strengthen the cut; no complete cutting-plane algorithm is claimed. Section 4.3 supplies integer-bound normalization for the open lead on scaled auxiliaries; Primo's current preparation has the limitations described above. |
| Martin Bromberger, [*Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955), PhD thesis, Saarland, 2019, Chapters 4–6 and §§7.2.3–7.2.4 | Cube tests, equality/bounded-direction analysis, bounding transformations and the separation of preparation from per-node work. These supply comparisons and concrete further work; Primo does not implement that complete combination. See the [reading guide](../papers/bromberger-linear-arithmetic.md). |
| Bruno Dutertre and Leonardo de Moura, *Integrating Simplex with DPLL(T)*, SRI-CSL-06-01, §§2–3 and 4.1 {cite:p}`dutertre2006simplexreport` | The rational tableau, bound assertions and consistency checks explained in [LRA](lra.md), plus integer splitting with delta-aware floor in §4.1. Section 3.2.4 saves a successful assignment; Primo's sparse mode retains the current assignment and basis when popping bounds. These implemented mechanisms do not establish completeness of Primo's integer procedure. |
| Original Primo design: private integer completion and accumulated explanations | The owned tableau copy, internal DFS, cut lifetime and union of antecedents connect the cited arithmetic rules to Primo's SAT and component interfaces. Defensive clause containment is not a substitute for those rules' soundness. |

The Griggio and Bromberger tableau
engineering references remain mapped in [LRA's source table](lra.md#sources-and-implementation-status);
they are not additional implemented integer stages.
