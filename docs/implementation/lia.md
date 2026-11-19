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
[budgets and incomplete cases](arithmetic/lia/branching.md#budgets-and-incomplete-cases) section distinguishes this
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
existing IDs. Temporary cuts and private branch bounds create neither
core terms nor SAT atoms. Hybrid handoff instead names an original semantic
term in a SAT bound, in the [hybrid handoff](arithmetic/lia/branching.md#hybrid-handoff). The working copy is discarded when its Boolean
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
integer-valued auxiliaries. Primo's integrality switches provide those preparation conditions. QF_LIA
defaults to integer-bound rounding and primitive rows, with explicit
integer-slack obligations off. QF_UFLIA keeps all four switches off by default,
so conservative checks remain necessary. The {ref}`input preparation section <lia-input>` and
[integrality checklist](../papers/mip-presolve-integrality.md) explain the
implemented switches, their sources and their counters.

The child chapters below group preparation, completion and SAT handoff.
The [arithmetic reading guide](arithmetic/index.md) identifies their shared LRA dependencies and the separate research/evidence records.

```{raw} html
<span id="why-the-relaxation-can-still-contain-infinitesimals"></span>
<span id="lia-input"></span>
<span id="lia-infinitesimals"></span>
```

## Input lowering and integer bounds

[Integer input and integrality](arithmetic/lia/preparation.md) explains lowering, bound rounding, primitive rows, integer slack obligations and strict bounds.

```{raw} html
<span id="when-integer-reasoning-runs"></span>
<span id="repair-schedule-and-phase-advice"></span>
<span id="patching-interval-reuse"></span>
<span id="why-a-nearest-move-is-insufficient"></span>
<span id="lia-schedule"></span>
<span id="lia-patching"></span>
```

## Patching a feasible assignment

[Integer completion scheduling and patching](arithmetic/lia/repair.md) owns rational-repair timing, stage order, patch scoring and interval-cache lifetime.

```{raw} html
<span id="detecting-integer-infeasible-rows-with-gcds"></span>
<span id="excluding-a-fractional-point-with-a-gomory-cut"></span>
<span id="turning-the-inequality-into-a-row"></span>
<span id="lia-cubes"></span>
<span id="lia-gcd"></span>
<span id="detecting-impossible-row-divisibility"></span>
<span id="lia-cuts"></span>
```

## Optional cube model attempt

[Cube models, divisibility checks and cuts](arithmetic/lia/cubes-and-cuts.md) derives the separate sufficient-model attempt, row-local GCD refutation and Gomory inequalities.

```{raw} html
<span id="hybrid-handoff"></span>
<span id="explaining-integer-conflicts-to-sat"></span>
<span id="budgets-and-incomplete-cases"></span>
<span id="lia-branching"></span>
<span id="lia-hybrid-branching"></span>
<span id="lia-explanations"></span>
<span id="lia-limits"></span>
```

## Branching and backtracking

[Integer branching, explanations and limits](arithmetic/lia/branching.md) covers private search, hybrid history and SAT decisions, retained reasons, budgets and incomplete cases.

```{raw} html
<span id="reuse-of-row-endpoints"></span>
```

## Bound propagation repairs

[The shared arithmetic propagation chapter](arithmetic/lra/propagation.md#delivery-triggers-and-limits) owns notifications, integer endpoint rounding and scan reuse across all four ground arithmetic logics. Integer preparation selects the rounding policy; no duplicate propagation implementation is implied.

(lia-settings)=
## Settings and who sets them

The engine reads its integer settings from one value,
{qfliadefaultsource}`LiaSettings <src/smt/lia_settings.hpp>`. It has one field
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
3. The {qfliadefaultsource}`policy <src/smt/lia_settings_policy.cpp>` starts from
   `default_lia_settings(route)` and applies every override that is set. An
   explicit value therefore wins over the route default, except that the
   experimental cube controls are applied only to the QF_LIA route.

The {qfliadefaultsource}`policy <src/smt/lia_settings_policy.cpp#L7-L20>` gives
QF_UFLIA the engine defaults, `LiaSettings{}`. QF_LIA uses **M124 + uniform
cubes + hybrid branching**: integer-bound rounding, primitive rows and integral
patch moves are on; explicit integer-slack obligations are off. The
[default-profile update](../evidence/qf-lia-defaults-20261119.md) records the
current qualification and unchanged budgets.

Historically, `5dec036` enabled all four switches from the [integrality
page](../papers/mip-presolve-integrality.md). That page's
{ref}`promotion <integrality-promotion>` section reports a net 53 additional
QF_LIA solves and 31 lost QF_UFLIA solves in its paired rerun. Those measurements
explain the earlier policy; they do not measure the current combined default.

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
({qfliadefaultsource}`lia_route_default_text <src/main.cpp#L201-L209>`). It shows
per-route values for the Boolean switches, branching and cube mode, and follows any later policy
change. The default column of the [budget table](arithmetic/lia/branching.md#budgets-and-incomplete-cases) and the CLI help snapshot
must still be updated by hand. The documentation checks compare the snapshot
against the binary.

The QF_LIA and QF_UFLIA solver steps each capture their resolved settings by
value. The solver-dispatch leaf renders both sets as strategy parameters, so
the strategy's canonical form and fingerprint record which configuration each
route runs ({ref}`tactics-lia-settings`). That holds only while
`lia_strategy_parameters` renders every field, so a new setting must be added
there too.

Three regressions in {qfliadefaultsource}`tactic_tests.cpp <tests/unit/tactic_tests.cpp#L1243-L1437>`
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
{qfliadefaultsource}`qf_lia_solver_tests.cpp <tests/unit/qf_lia_solver_tests.cpp#L19-L36>`
run under `set-logic QF_LIA`, but they test the engine's stages with the
switches, cubes and hybrid off. They therefore build their options from `engine_defaults()`
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

The [hybrid branching checklist](../leads/lia-hybrid-branching.md) records the
preimplementation contract and qualification obligations. The optional
[ordinary-split policy](arithmetic/lia/branching.md#hybrid-handoff) preserves private-mode behavior; QF_LIA now selects hybrid by default.

The local paper-to-code checklist is the stage order, guards, derivations,
state contracts and regression obligations in these chapters. The primary sections used
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
