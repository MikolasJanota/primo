# Integer branching, explanations and limits

This chapter explains private branching and SAT-visible hybrid handoff, how integer conflicts retain their premises, and why bounded completion may remain inconclusive.

Start with the [LIA overview](../../lia.md) for route responsibilities and defaults.

(lia-branching)=
## Branching and backtracking

{source}`branch_and_bound <src/theory/arithmetic/lia_branch_and_bound.cpp>`
starts from a rationally feasible tableau. In private mode it scans `integer_vars` for the
first nonintegral assignment $v$ and splits on

$$x\le\lfloor v\rfloor\quad\lor\quad x\ge\lfloor v\rfloor+1.$$

Every integer satisfies one side, and the current fractional value satisfies
neither. At $v=7/3$, for example, the branches are $x\le2$ and $x\ge3$.
Each side is asserted as an ordinary tableau bound and checked by simplex.
A feasible side is searched further; a conflict closes that side. After both
sides close, the search backtracks to the previous frame. It tries the lower
side first. Private mode uses the caller's variable order; hybrid mode uses
the history score described below.

The explicit stack stores checkpoints rather than tableau copies per node.
On `Sat`, the tableau stays at the winning leaf, including the successful
branch bounds, so the caller can extract the model. On `Unsat` or
`LimitReached`, the procedure removes its speculative bounds. With the
default sparse simplex, popping restores bounds rather than the previous
assignment and basis. The optional SOI path also trails assignments and
pivots; see [LRA backtracking](../lra/tableau.md#incrementality-and-rollback).
Callers should rely on the bound-restoration contract, not require identical
internal tableau states.

The node counter increments when a new split is created, not for every
simplex pivot or every attempted side. Integrality is checked before the
limit, so an already integral assignment succeeds even with a zero node
budget. The search stack grows with depth; the node cap bounds the number
of splits in this call but is not a wall-clock or bit-complexity bound on
the intervening exact arithmetic.

(lia-hybrid-branching)=
### Hybrid handoff

`--lia-branching=hybrid` enables a partial implementation of Griggio's
*A Practical Approach to Satisfiability Modulo Linear Integer Arithmetic*,
§5. It runs the existing patch/GCD/cube/cut stages, then limits private split
nodes to `min(branch_limit, private_branch_factor * N)`. Multiplication
saturates safely at the existing limit. Here `N` counts the distinct mapped
original Int terms, including purified terms used by UF, before split atoms
are generated. Integer slack obligations do not contribute to `N`.
`--lia-private-branch-factor` defaults to `1`; zero requests immediate handoff
after the preceding stages. This factor and the zero convention are Primo
engineering choices. QF_LIA defaults to `hybrid`; QF_UFLIA retains `private`.

The solve-scoped {hybridsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` owns a
{hybridsource}`LiaBranchHistory <src/theory/arithmetic/lia_branch_and_bound.hpp>`.
For each stable numerical variable it records separate left/right observation
counts and exact sums of the number of fractional integer obligations after
feasible branch checks. The selection score is the smaller side's mean;
an unobserved side scores zero and ties use the smallest variable ID.
Immediately infeasible sides increment a conflict counter without supplying
an observation. History survives SAT backtracking and atom registration but
starts empty in a new solving invocation. Both scoring and counting scan
the integer obligations linearly. The private policy keeps its
original variable order.

On private budget exhaustion,
{hybridsource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.cpp>`
removes all speculative branch bounds and checks rational feasibility again.
If the repaired assignment satisfies every integer obligation, completion
succeeds. Otherwise it selects a fractional original Int term with the same
score, and returns `Split` with its numerical variable and an owning exact
floor threshold. Private integer slacks remain checked; they cannot be exported.
Integral original coordinates with a fractional integer slack diagnose a
preparation invariant failure. Gomory rows stay in the private working tableau
and are discarded at handoff. No persistent row-insertion contract changes.

The adapter translates that request into
{hybridsource}`IntegerSplitRequest <src/theory/theory_solver.hpp>`, which owns a
semantic `TermId` and exact threshold. `TheoryCheckStatus::IntegerSplit` and
`ModelBasedCombinationStatus::IntegerSplit` carry it through the coordinator;
neither denotes a conflict or an entailed bound. The
{hybridsource}`component driver <src/smt/component_solver.cpp>` installs
one SAT atom $p\leftrightarrow(x\le k)$ at its detached growth boundary.
False means $x\ge k+1$. For example, at $x=-3-\delta$, the exact floor is
$-4$: the choices are $x\le-4$ and $x\ge-3$.

The registry keys bounds by semantic term and exact integer threshold and
also indexes compatible exact integer input bounds. Fractional input thresholds
left unrounded by configuration are excluded from that index. An already
assigned requested bound diagnoses missing arithmetic progress rather than
retrying indefinitely. Ordinary bounds bind existing variables through
`PreparedLraTheory::append_atom`; the component checks that registration
allocated no arithmetic variable, and the single-variable preparation path
adds no row. Registration rebuilds online assignment/query state and invalidates
model tokens. The history remains in the full-model adapter. Term construction
uses owned IDs and values across allocation boundaries.

The driver prefers $p$ using its managed SAT-choice assumptions. If SAT rejects
the assumed choices, failed-assumption analysis learns their blocking clause,
removes failed choices and resumes search. Failure under a preferred branch
alone never becomes global UNSAT. Arithmetic conflicts retain generated SAT
bound literals as real premises. Only a fully closed private search permits
discarding synthetic private split premises. A pending split prevents acceptance
of the candidate model, in online/offline UFLIA and standalone LIA alike.

`--lia-split-atom-limit` caps fresh generated bound atoms per solve; its default
`0` is unlimited. The public `solve_component_recipe` call owns the consumption
counter and shares it with its internal complete-arrangement fallback
({hybridfallbacksource}`repaired owner <src/smt/component_solver.cpp>`,
{hybridfallbacksource}`regression <tests/unit/qf_uflia_solver_tests.cpp>`;
{download}`fresh qualification <../../../../benchmarks/merge-readiness-20261119/qualification.json>`). The
backend-local bound registries remain separate: allocating the same semantic
bound in a new SAT space consumes another atom. Reaching the cap only blocks
new atom creation; it does not prevent completion without further growth.
The peak includes both backends while recursive fallback keeps them alive.
Exhaustion returns `unknown` with a resource-limit reason.
Cancellation and existing limits still apply. Statistics under `:lia.branching.`
count private nodes/conflicts, budget exits, split requests, unique/reused bound
atoms, generated-atom peak, SAT resumes, rejected assumption batches containing a failed arithmetic choice, and split-cap
exhaustions. Existing-coordinate registrations record ordinary bounds added
without arithmetic variables. Reuse counts currently diagnose a nonprogress
request; ordinary successful handoffs introduce a fresh threshold.
`:time.lia.branching.private-completion-us` covers the private waterfall and
cube-problem preparation, excluding the incoming tableau copy, while
`:time.lia.branching.resumed-sat-us` sums SAT calls after the first handoff.
These elapsed timers are nested in route timers and must not be added to them.

Griggio §5.1's proof-derived linear-combination splits are not implemented.
Neither this ordinary-split subset nor its optional atom cap establishes
termination or completeness on unbounded integer problems; §5.2 and the
[implementation checklist](../../../leads/lia-hybrid-branching.md) explain the boundary.
Hybrid is the QF_LIA default and remains opt-in for QF_UFLIA. The
[current QF_LIA profile](../../../evidence/qf-lia-defaults-20261119.md) records
the default qualification; the [original qualification record](../../../evidence/lia-hybrid-20261111.md) reports the requested
LIA/UFLIA comparisons, independently validated models and repeated regressions.

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
| `--lia-cube` | `uniform` for QF_LIA; ignored by QF_UFLIA | Pure QF_LIA only: `uniform` or `difference` model attempt before cuts; failure falls through. |
| `--lia-cube-steps` | `1000` | Optional LP repair-step cap; zero allows only an already feasible center. |
| `--lia-patch-limit` | `100` | Maximum accepted patch moves; zero disables patching. |
| `--lia-gomory-cut-limit` | `100` | Maximum cut-and-recheck iterations; zero disables cuts. Exhaustion falls through to branching. |
| `--lia-branch-limit` | `100000` | Maximum private split nodes. Private-mode exhaustion returns a resource-limit result; hybrid mode repairs and hands off to SAT. |
| `--lia-branching` | `hybrid` for QF_LIA, `private` for QF_UFLIA | `hybrid` enables the handoff described in {ref}`lia-hybrid-branching`. |
| `--lia-private-branch-factor` | `1` | Hybrid private-node factor per original Int term; zero skips private split nodes. |
| `--lia-split-atom-limit` | `0` | Fresh hybrid bound atoms per solve; zero is unlimited and exhaustion returns a resource-limit result. |
| `--no-lia-conflict-generalization` | Generalization on | Disables the default attempt to replace coarse clauses with accumulated explanations. |
| `--lia-round-integer-bounds` | On for QF_LIA, off for QF_UFLIA | Rounds non-strict thresholds on integer-valued tableau variables inward. Preparation-time, not a per-completion budget. |
| `--lia-primitive-int-rows` | On for QF_LIA, off for QF_UFLIA | Stores `Int` rows with coprime integer coefficients, making every `Int` slack integer-valued. Preparation-time. |
| `--lia-integer-slacks` | Off for both routes | Adds slacks proved integer to the front of the integer obligations of completion. |
| `--lia-patch-integral-moves` | On for QF_LIA, off for QF_UFLIA | Keeps integer nonbasic variables integral while patching and adds lattice candidates. |

The patch/cut/branch/generalization controls apply to both routes. Integer-bound
rounding, primitive rows and integral patch moves default on for QF_LIA and off
for QF_UFLIA. Explicit integer-slack obligations default off for both routes, as
{ref}`lia-settings` explains.
Each also has a `--no-` form, for example `--no-lia-primitive-int-rows`,
which turns it off on either route. If both forms are given, the `--no-` form
wins, as for the legacy `--lra-model-phase` / `--no-lra-model-phase` pair.
The current `--arithmetic-model-phase=on|off|auto` form rejects mixing with
those legacy flags; see [option compatibility](../../../reference/options.md#phase-option-names-and-compatibility).

These are finite operational limits, not empirically optimal settings.
In private mode, both routes report typed `ResourceLimit` to the shared driver, producing
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
unbounded cut generation. The [paper overview](../../../papers/cuts-from-proofs.md)
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
Hermite-normal-form reasoning, cuts within branch nodes, and proof-derived
splits on linear combinations remain outside the implementation. Ordinary
semantic-term splits are exposed to SAT by the {ref}`hybrid handoff
<lia-hybrid-branching>`. The optional cube test is
implemented for pure QF_LIA in the [cube chapter](cubes-and-cuts.md#optional-cube-model-attempt); its failure is never an
integer-infeasibility proof. The {ref}`cube-test explanation
<bromberger-unit-cube>` derives its strengthening, rounding guarantee and
equality limitation. The [cube integration note](../../../leads/lia-cube-test.md)
records the paper checklist and qualification plan. The
{source}`earlier thesis note <docs/papers/bromberger-linear-arithmetic-thesis.md>`
and {source}`integer open leads <docs/open-leads.md#L1765-L1834>` retain the
separate investigations and their prerequisites.

`QF_LIRA`, mixing declared `Int` and `Real` variables, is not admitted. The
stage APIs already take an integer subset, and fractional auxiliary variables
exercise some mixed-tableau cases. That does not establish end-to-end QF_LIRA
support: contextual numeral typing, coercions such as `to_real`, frontend
admission and combined model construction still need design and validation.

## Sources and regression map

The overview collects [primary sources and implementation status](../../lia.md#sources-and-implementation-status) and [regression obligations](../../lia.md#models-regression-coverage-and-retained-evidence). The sections here retain their algorithm-specific derivations, examples and source links.
