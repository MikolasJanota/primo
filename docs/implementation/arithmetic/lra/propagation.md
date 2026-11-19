# Arithmetic bound and equality propagation

This chapter explains how arithmetic proves consequences and supplies their reasons to SAT. The bound rules serve QF_LRA and QF_UFLRA and the rational relaxation in QF_LIA and QF_UFLIA. The later sections explain when proved equalities are submitted to SAT or exchanged with another theory solver.

Start with the [LRA overview](../../lra.md) for integration with SAT and default settings. The [tableau notation](tableau.md#tableau-equations-and-bounds) defines basic/nonbasic variables, the assignment $\beta$ and bounds $l,u$.

(propagation-and-online-checking)=
## Propagate and explain consequences

| Contract | Meaning |
| --- | --- |
| Input | Active bounds with reasons, tableau equations, and registered arithmetic atoms with SAT names. |
| Result | An implied SAT literal and a clause consisting of that literal and the negated premises. No consequence is also a valid result. |
| Default method | Compare bounds on the same variable. Forward and reverse row propagation require their separate options. |
| Correctness condition | The asserted premises imply the propagated literal. Discovery of all consequences is not required. |

Arithmetic propagation proves the value of an existing Boolean atom from
active bounds and the tableau equations. It returns that literal with an
explanation clause. [Equality exchange](../../combination.md#explained-equality-exchange)
uses the same implication-to-clause principle to communicate consequences
between theories; its conclusions are equalities of shared terms.

### Bounds on the same variable

Default propagation compares predicates bounding the same tableau variable.
For instance, `p: s <= 2` entails `t: s <= 5`, with reason clause `¬p ∨ t`.
{source}`scan_for_propagations <src/smt/solver_engines.cpp#L7554-L7600>` visits
the prepared `atoms_by_bound_var` index and checks both polarities of each
unassigned atom. It uses bounds, not accidental equalities in the current
candidate assignment. This is the cheap propagation of the technical report's
§3.2.5.

### Deriving a bound from a row

Optional row propagation combines bounds on several variables. Consider the
current tableau row

```text
s = 2*x - 3*y + z
```

Here `s` is basic and `x`, `y`, `z` are nonbasic; the equation is an identity
maintained by pivoting, not an extra SAT assumption. Let `l(v)` and `u(v)`
denote the active lower and upper bounds on variable `v`. Multiplication by
a negative coefficient reverses order, so the bound used for each contribution
depends on its sign:

| Coefficient of `v` | Upper-bound contribution | Lower-bound contribution |
| --- | --- | --- |
| `a > 0` | `a*u(v)` | `a*l(v)` |
| `a < 0` | `a*l(v)` | `a*u(v)` |

Thus this row gives

```text
upper(s) = 2*u(x) - 3*l(y) + u(z)
lower(s) = 2*l(x) - 3*u(y) + l(z)
```

These are candidate implied bounds; they need not be the strongest bounds
entailed by the whole problem. {rowcachetrialsource}`compute_implied_bound
<src/smt/solver_engines.cpp#L7013>` computes one direction at a time
with exact `DeltaRational` arithmetic. A separate helper collects the selected
bounds' reasons only after the endpoint entails an atom. It reads bounds, not the current model's values of `x`, `y` and `z`.

For a concrete upper bound, suppose SAT literals `p`, `q`, `r` assert
`x <= 4`, `y >= 1`, `z <= 2`. The contributions are at most 8, −3 and 2,
respectively, so `s <= 7`. If an unassigned atom has literal `t` and means
`s <= 8`, this entails `t` with the clause

```text
not p or not q or not r or t
```

The threshold may be weaker than the computed bound: `7 <= 8` suffices.
For a lower-bound query the comparison reverses. The
{source}`entailment check <src/smt/solver_engines.cpp#L7601-L7615>` makes this
comparison against the bound imposed by each possible truth value of the
target atom. It can therefore propagate the negation of an atom as well.

The same calculation handles strict bounds using the
[infinitesimal convention](tableau.md#exact-numbers-and-strict-bounds). Replacing `x <= 4`
by `x < 4` contributes `2*(4 - δ)` and gives `s <= 7 - 2*δ`. This is strong
enough to imply the atom `s < 7`, whose upper bound is `7 - δ`.

### Bounding a row variable in reverse

The optional reverse direction isolates a nonbasic variable rather than the
row's basic variable. In a separate candidate with the same row, let literals
`a`, `b`, `c` assert `s <= 1`, `x >= 2`, `z >= 0`. Isolating `y` gives

```text
-3*y = s - 2*x - z <= 1 - 2*2 - 0 = -3
   y >= 1
```

Dividing by −3 reverses the inequality. If literal `v` represents the
unassigned atom `y >= 1`, its explanation is
`not a or not b or not c or v`. This derivation uses the bound on `s` and
the other row variables; it does not need an existing bound on `y`.

In general, for `s = k*y + sum(k_j*x_j)` with nonzero `k`, isolate
`y = (s - sum(k_j*x_j))/k`. Choose the numerator's upper or lower bound
according to the requested direction and the sign of `k`, then apply the
coefficient rule above to the subtracted terms. This is
{rowcachetrialsource}`compute_implied_child_bound <src/smt/solver_engines.cpp#L7052>`.
It returns a derived value without pivoting to make `y` basic. Explanation
collection follows successful entailment, as in the forward direction.

### Delivery, triggers and limits

The separate {rowcachetrialsource}`explanation collectors
<src/smt/solver_engines.cpp#L7135>` collect the bounds' **already negated premise
literals**: the
forward example stores `not p`, `not q`, `not r`. The
{source}`delivery and reason callbacks <src/smt/solver_engines.cpp#L7115-L7180>`
return the target literal and these clause literals without negating them again.
The propagator owns the queued candidates and delivered reasons, following the
[explanation lifetime](../../dpllt.md#reason-lifetime-is-separate-from-clause-retention)
rules. A row identity needs no SAT premise;
changing basis does not invalidate the identity used to derive the clause.

The implementation uses derived bounds to test already registered atoms. It
does not install every derived numeric bound or iterate interval refinement
to a fixed point. Only when SAT assigns a propagated atom does its bound enter
the usual assignment path and become available for further propagation.
In the forward example, deriving `s <= 7` proves the existing atom `s <= 8`.
Assigning that atom submits the bound 8 to the tableau, not the derived bound
7; an already stronger stored bound remains in force.

After a nonconflicting assigned bound, the optional
{rowcachetrialsource}`scan_row_based_propagations <src/smt/solver_engines.cpp#L7955>`
follows `occurs_in(changed_var)` to rows containing the changed variable.
Forward work needs registered atoms on the row's basic variable. Reverse work
has no such prerequisite: it tests atoms on other row variables when enabled.
A notification on a basic variable also scans its own defining row in reverse,
subject to both limits on the number of affected rows (fanout caps) being nonzero and the reverse row-size cap. Thus the
reverse example can propagate when the bound on `s` arrives last. Pivots do not
themselves trigger exhaustive interval rescanning.

Both {rowcachetrialsource}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7955>`
and {rowcachetrialsource}`ComponentLraExecution <src/smt/solver_engines.cpp#L9632>` use these rules. They serve
QF_LRA and QF_UFLRA, and the rational relaxation in QF_LIA and QF_UFLIA. Under
preparation's `IntegerBoundRounding::All` policy, comparisons on integer-valued
targets use delta-aware floor for upper endpoints and ceiling for lower ones,
with the same explaining premises. This remains conditional on the existing
rounding option; non-strict rounding stays off by default in QF_UFLIA.

The counters `:lra.basic-variable-row-scans` and
`:lra.integer-rounding-entailments` count own-row scans and candidate entailments
that require rounding, respectively; the latter is not a delivery count.
The notification regression covers both assertion orders in all four ground
arithmetic logic declarations.

Missing bounds and cost caps can prevent a propagation without changing
satisfiability. Without a lower bound on `y`, the forward example cannot
compute its upper bound by this interval calculation; another direction or
another row may still yield a consequence. A row wider than the size cap is
skipped. If the changed variable occurs in more rows than the fanout cap,
the scan skips all of them for that notification rather than taking a prefix.
The {source}`default caps <src/smt/check_sat.hpp#L173-L178>` are 64 terms/rows
for forward work and 32 for reverse work. Within one synchronous row or child
scan, a {rowcachetrialsource}`cache storing separate lower and upper endpoints <src/smt/solver_engines.cpp#L7082>`
reuses calculated endpoints, including missing
results, and rounded integer endpoints across target atoms. Explanation vectors
are built lazily and copied into each pending consequence. Scans only enqueue
consequences and do not mutate the tableau; the cache is destroyed before any
subsequent assignment, pivot, backtrack or atom registration. The
{ref}`scan-reuse checklist <scan-local-reuse-draft>`
records its complexity, counters and qualification status.

The {propfilterfinaltrialsource}`execution adapters <src/smt/solver_engines.cpp>`
classify successful bound insertions as unchanged bounds
or tighter endpoints by reading the length of the tableau undo trail. This trail
records bound changes, so its length identifies whether an insertion changed an
endpoint without modifying the tableau merely to inspect it. The original
instrument-only snapshot preserved every duplicate; the following filters are
implemented in the frozen qualification candidate `c815a0b4`, with final
correctness and performance acceptance recorded separately in the
{download}`experiment ledger <../../../run-log.json>`.

Each arithmetic execution object (`OnlineLraPropagator` or
`ComponentLraExecution`) owns an
{propfilterfinaltrialsource}`ArithmeticPropagationQueue <src/smt/arithmetic_propagation_queue.hpp>`.
Its entries own the signed SAT literal, copied explanation vector and origin
flags. With the existing row-propagation option enabled, a dense array of two
Boolean markers per SAT variable records pending presence independently for
each sign. A repeated signed literal is suppressed before copying reasons;
the first occurrence retains its position and original explanation. Opposite
signs remain distinct. Pop moves the owning entry out and clears its marker;
backtracking clears every remaining marker and entry. Shared-equality notifications between theory solvers use a separate queue and
are excluded from this duplicate filter. Without row propagation,
the queue preserves duplicates and their order, and never indexes or allocates
the marker array. Those insertions are explicitly counted as untracked for
duplicate classification.

Each adapter also owns an
{propfilterfinaltrialsource}`ArithmeticRowScanMemo <src/smt/arithmetic_row_scan_memo.hpp>`.
Per arithmetic variable it stores only the last scanned backtrack generation
(a counter incremented on each backtrack), undo-trail length and cumulative pivot count. A numerical no-op skips row refinement
only when all three match. Every backtrack invalidates stamps, including when
another branch later reaches the same trail offset; generation wrap clears all
stamps. A changed endpoint or pivot prevents reuse. The component execution is
recreated after atom registration; the legacy online adapter borrows its fixed
prepared arithmetic view. No endpoint or reason cache survives a scan, and no
new rows, variables, atoms or inference rules are introduced. Same-variable
propagation and the equality/interface paths still run. See the
{ref}`design and invalidation obligations <repeated-state-row-scan-filter>`
and [counter denominators](../../../reference/statistics.md#arithmetic-redundancy-audit).

This is ordinary queue coalescing and conservative memoization, motivated by
Schulte–Stuckey's *Efficient Constraint Propagation Engines* §5; it does not
implement their finite-domain fixpoint algorithm. The bounded scan still uses
Dutertre–de Moura's interval bounds with Primo's existing limits. Marker growth
and memo growth cost a total `O(N + V)` initialization and storage over the
execution lifetime, for maximum encountered SAT index `N` and arithmetic index
`V`; this is not an amortized constant-space claim for sparse IDs. The default
path allocates neither dense array. Neither a numerical no-op nor a row event
without a new candidate alone proves that all processing can be omitted.


{source}`The options <src/smt/check_sat.hpp#L245-L269>` enable same-variable
propagation by default; row propagation requires `lra_row_propagation`, and
the reverse direction additionally requires `lra_bidirectional_row_propagation`.
Both row options default to false and depend on `lra_propagation`. These
mechanisms do not perform complete arithmetic consequence discovery.

Dutertre–de Moura's technical report §3.2.5 describes both same-variable
propagation and row-bound refinement; applying it to an isolated row variable
gives the reverse rule above. Primo's atom indexing, triggers and caps are
implementation choices.
Bjørner and Nachmanson's [*Arithmetic Solving in Z3*](https://doi.org/10.1007/978-3-031-65627-9_2)
(CAV 2024), §3.3, makes the same
direct/indirect propagation distinction. Primo's row mode queries implied
bounds to explain existing Boolean atoms; this does not mean it implements
every bound-refinement policy described for Z3.

For mixed Real problems, a proved comparison can also contribute to
[online shared-equality propagation](../../combination.md#online-equality-propagation):
assigned `x <= y` and `y <= x` justify `x = y`. The default
{ref}`tableau equality rules <lra-default-equality-rules>` are another producer,
with their own scheduling and completeness limits. These are separate from
guessing an equality because two current model values happen to coincide.

(equalities-for-combination)=
(propagating-equalities-between-shared-variables)=
## Propagating equalities between variables

The default QF_UFLRA producer uses four rules based on fixed variables and
matching tableau rows. Their worked examples, bound explanations, and
incompleteness are described in
{ref}`the combination chapter <lra-default-equality-rules>`. That chapter also gives
the [model-mutation schedule](../../combination.md#when-model-mutation-runs): the
dedicated Real route attempts scratch diversification before its final
equality-exchange passes and model reconciliation.

Bjørner and Nachmanson's *Arithmetic Solving in Z3*, §3.2, also informs an
optional equality producer for QF_UFLRA. The same query is selectable for
shared Int terms through `--qf-uflia-lia-equality-producer=model-unit`, with
equality export enabled separately. The [integer integration](../../uflia-equality-propagation.md)
retains nonconvex arrangement/integer search; the
{ref}`Real-route exact fallback <lra-exact-equality-fallback>` remains separate. Starting from a feasible tableau,
{source}`model_filtered_unit_equalities <src/theory/arithmetic/lra_tableau.cpp#L1265-L1460>`
groups shared variables by their exact current value. It then looks for
equalities justified by bounds and equations. Two variables fixed to the same
value are equal. For variables related by rows, it checks two shapes:

- `x = y + r`, where every variable in the remainder `r` is fixed by matching
  lower and upper bounds. If `x` and `y` have equal current values, `r` is
  zero and remains zero while those bounds hold.
- `x = s*z + r₁` and `y = s*z + r₂`, where the shared coefficient `s` is
  `1` or `-1` and both remainders contain only fixed variables. Equal current
  values of `x` and `y` establish that the fixed remainders are equal too.

Current values filter candidate pairs; the bounds fixing the remainders
supply the explanations. This avoids constructing the large rational offsets
used by the older producer. A separate union-find structure tracks connected components of the equality
edges already proved during this query. The method emits an edge only when it
joins two different components; transitivity supplies the remaining equalities.
These proof components differ from the initial groups of equal model values,
which only restrict the candidate pairs.
The surrounding combination solver retains its exact fallback checks.

This is the `model-unit` option. The
{source}`default remains legacy <src/smt/check_sat.hpp#L396-L402>`; pure
QF_LRA calls neither producer by default. The full-corpus comparisons and the unpromoted
adaptive follow-up are preserved in the
{source}`2024 paper implementation note <docs/papers/arithmetic-solving-in-z3.md#L280-L464>`.
The paper's integer and nonlinear procedures are outside this chapter's scope.

(experimental-pure-arithmetic-equality-propagation)=
### Experimental pure-arithmetic equality propagation

The default-off `--lra-equality-propagation` experiment reuses the §3.2 model-unit
recognizer on pure QF_LRA. This deliberately changes the scope: the paper
explicitly disables this discovery pass for pure QF_LRA/QF_LIA. It is a
portfolio experiment, not a claim that the paper recommends this setting.
It requires IPASIR-UP and works independently of `--lra-propagation`.
Retained sessions keep their existing option restrictions: monotone elimination
is forbidden in assumptions mode. The combined phase/elimination/equality
profile therefore requires rebuild mode in an incremental session; the {ref}`retained experiment <pure-lra-equality-portfolio-evidence>` used one-shot queries.
The {equalityexperiment}`consumer index <src/smt/solver_engines.cpp#L7217-L7245>`,
{equalityexperiment}`feasible-state scan <src/smt/solver_engines.cpp#L7561-L7634>`,
and {equalityexperiment}`backtrack repair <src/smt/solver_engines.cpp#L7031-L7068>`
are separate from the existing tableau recognizer described above.
The implementation follows this checklist:

- The pure `solve_qf_lra_impl` path enables the feature explicitly on its
  `OnlineLraPropagator`, as does the retained `IncrementalLraEngine` when it
  constructs the per-query propagator. Mixed routes and defaults keep their
  existing behavior.
- The propagator owns an immutable index of existing comparison atoms whose
  canonical polynomial is `x - y`, and the original arithmetic variable IDs.
  It creates no terms, SAT variables or tableau rows, and does not introduce
  comparison atoms for every pair of indexed variables.
  With no indexed targets, it leaves the scan and extra repair scheduling inactive.
- After an arithmetic assignment or backtrack, repair feasibility before
  querying `LraTableau::model_filtered_unit_equalities`. Equal model values
  alone never justify a propagation. No scan runs again until the state changes.
- Treat the returned proved equalities as graph edges and join their endpoints
  into connected components. The union of the bound
  premises on a component's edges justifies equality of any two members;
  these reasons may be larger than a minimal path explanation.
- For an indexed atom whose endpoints are proved equal, evaluate its comparison
  at `x - y = 0` and queue the corresponding SAT literal with those premises.
  An opposite assignment produces a guarded conflict. Existing propagation
  reason lifetimes and backtrack removal apply to delivered literals.
- Discovery costs a model grouping and candidate-row scan; component building
  and target lookup add linear traversal plus disjoint-set work. Materializing
  component explanations adds `O(sum_c R_c log R_c)` sorting work for `R_c`
  collected reason literals in each component; deduplication and copying
  reasons to target atoms add work proportional to their sizes. There is no completeness
  claim: row shapes, the current basis, and existing target atoms restrict it.
- Test fixed equalities, both unit connector signs, accidental model equality,
  strict and scaled/reversed comparisons, transitive edges, backtracking and
  model construction. Qualify on CPU SLURM with fresh differential fuzzing and
  real-seed mutations, then compare default/option and both-flags/option on
  all 1,753 inputs with separate direct SAT-model validation. Report complementary
  solves, not only aggregate runtime. No default or performance claim is made
  before these checks complete.

For example, bounds `x >= 7`, `x <= 7`, `y >= 7`, `y <= 7` prove `x = y`.
Their four premise literals justify an existing `x <= y` atom being true or an
existing `x < y` atom being false. Scaled and reversed comparisons use the
same normalized difference slack. The index excludes expressions such as
`2*x - y`, whose value is not determined by equality alone. Internal bounds
are updated only when SAT assigns the propagated atom, following the existing
notification protocol. The new unit regressions exercise transitive component
reasons, strict/scaled targets, accidental model equality, and conditional
fixing premises; the existing tableau regressions cover the unit-row shapes.
The frozen `4419dc9` implementation passed the generated, real-seed mutation,
retained-session and sanitizer gates recorded in run
`2026-10-21-pure-lra-equality-portfolio`. Full-corpus timings have completed;
every successful SAT row passed separate direct Dolmen model replay. The
{ref}`full-corpus comparison <pure-lra-equality-portfolio-evidence>`
records large aggregate losses and no additional portfolio coverage at 30 seconds.
Exploratory shorter cutoffs show small coverage gains; no shared-budget
portfolio schedule has been measured.
Profiling locates substantial
overhead in repeated discovery scans: many explained edges can produce very
few new target literals. The implementation currently scans even when its
indexed targets are already assigned and may queue duplicate candidates;
delivery skips assigned candidates. This is a performance limitation, not
evidence of extra pivoting or an invalid explanation. Defaults remain unchanged.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lra.md#sources-and-implementation-status) and [regression obligations](../../lra.md#invariants-exercised-by-existing-tests).
