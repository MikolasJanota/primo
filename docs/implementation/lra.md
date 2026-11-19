# Linear real arithmetic

This theory follows the [shared assignment and explanation contract](theory-interface.md#theories-and-their-shared-contract).
Its dedicated arithmetic checks and online callbacks currently provide that
integration; the Real routes do not all use the `TheoryComponent` C++ interface.
The [interface inventory](theory-interface.md#which-interface-is-used) and
[combination routes](combination.md#the-shared-smt-driver) distinguish those paths.

Primo's linear real arithmetic solver checks whether the inequalities selected
by SAT have a common solution. It maintains exact values satisfying a fixed
system of linear equations, then repairs violations of the selected bounds.
Failure produces a clause explaining which Boolean assignments cannot hold
together. The [DPLL(T) chapter](dpllt.md) describes how SAT uses that clause.

The implementation follows Dutertre and de Moura's
[Integrating Simplex with DPLL(T)](https://yices.csl.sri.com/papers/sri-csl-06-01.pdf),
with the engineering described in the child chapters: shared polynomial rows, direct
bounds, small-rational arithmetic, sparse pivoting, occurrence indices, deferred
checks and bounds-only rollback. Section numbers for this source refer to the
**technical report**, whose numbering differs from the CAV paper.

The [integer-arithmetic companion](lia.md) explains how the pure and combined
integer routes use this tableau as a real relaxation, then apply patching,
GCD checks, Gomory cuts and branching. It covers their explanations, budgets
and incomplete cases; [its preservation map](../coverage/lia.md) retains
the dated experiments separately.

## Theory responsibilities and defaults

The four main theory operations are collected below. The setup introduces
what their bounds and tableau rows represent; each operation then states its
input, result, default method and correctness condition.

| Operation | Question answered | Default method |
| --- | --- | --- |
| [Check consistency](arithmetic/lra/feasibility.md#check-consistency) | Do the currently asserted arithmetic literals have a common solution? | Exact simplex feasibility repair with Sparse pivot selection and eventual Bland fallback. |
| [Explain a theory conflict](arithmetic/lra/feasibility.md#explain-theory-conflicts) | Which assertions are inconsistent, and how is their clause obtained? | Two contradictory bounds, or a blocked row of the current tableau and its selected bounds. |
| [Propagate and explain consequences](arithmetic/lra/propagation.md#propagate-and-explain-consequences) | Which additional registered literals follow from the active bounds? | Same-variable bound propagation; forward and reverse row propagation are off by default. |
| [Construct a model](arithmetic/lra/tableau.md#model-construction) | How does a feasible symbolic assignment become concrete rational values? | Choose a sufficiently small positive rational for the symbolic infinitesimal, then build and reconstruct the typed model. |

Conflict detection and explanation are sufficient for the arithmetic
consistency decision; theory propagation is an additional search aid.
[sum of infeasibilities (SOI)](arithmetic/lra/search.md#optional-sum-of-infeasibilities) is an optional alternative feasibility procedure. The
[implementation details](#optional-algorithms-and-implementation-details)
also cover pivot policies, sparse storage and theory-combination mechanisms.

The detailed explanations are grouped into the four child chapters below.
The [arithmetic reading guide](arithmetic/index.md) connects these mechanisms to integer completion, preprocessing, combination, open questions and evidence.

```{raw} html
<span id="representation-and-ownership"></span>
<span id="tableau-equations-and-bounds"></span>
<span id="from-atoms-to-bounds"></span>
<span id="exact-numbers-and-strict-bounds"></span>
<span id="model-construction"></span>
<span id="from-delta-rationals-to-rational-values"></span>
<span id="incrementality-and-rollback"></span>
<span id="sparse-rows-and-reverse-occurrences"></span>
<span id="rollback-and-model-values"></span>
```

## Setup and maintained state

[Tableau state, models and rollback](arithmetic/lra/tableau.md) covers atom preparation, exact bounds, sparse rows, concrete models and restoration.

```{raw} html
<span id="feasibility-without-an-optimization-objective"></span>
<span id="repairing-an-assignment"></span>
<span id="deferred-consistency-checks"></span>
<span id="explain-theory-conflicts"></span>
<span id="directly-contradictory-bounds"></span>
<span id="selecting-bounds-from-a-blocked-row"></span>
<span id="minimization-and-alternative-explanations"></span>
<span id="why-one-current-row-suffices-after-pivoting"></span>
<span id="follow-a-strict-conflict"></span>
<span id="a-bound-contradiction-and-its-cancellation-proof"></span>
```

## Check consistency

[Rational feasibility and conflicts](arithmetic/lra/feasibility.md) derives the repair procedure and the explanations returned when bounds cannot be satisfied.

```{raw} html
<span id="bounds-on-the-same-variable"></span>
<span id="deriving-a-bound-from-a-row"></span>
<span id="bounding-a-row-variable-in-reverse"></span>
<span id="delivery-triggers-and-limits"></span>
<span id="propagating-equalities-between-variables"></span>
<span id="experimental-pure-arithmetic-equality-propagation"></span>
<span id="propagation-and-online-checking"></span>
<span id="equalities-for-combination"></span>
<span id="propagating-equalities-between-shared-variables"></span>
```

## Propagate and explain consequences

[Arithmetic propagation](arithmetic/lra/propagation.md) owns same-variable and row rules, their queues and caches, and explained equality discovery. The shared bound rules also serve integer relaxations.

```{raw} html
<span id="pivot-selection-policies"></span>
<span id="optional-sum-of-infeasibilities"></span>
<span id="the-violation-objective-and-its-row"></span>
<span id="selecting-a-step-at-a-bound-crossing"></span>
<span id="a-worked-pivot"></span>
<span id="combining-rows-into-a-conflict"></span>
<span id="shrinking-the-explanation"></span>
<span id="cost-termination-and-implemented-scope"></span>
<span id="fixed-basic-elimination"></span>
<span id="model-phase-hints"></span>
<span id="lra-soi"></span>
```

## Optional algorithms and implementation details

[Pivot policies and optional search methods](arithmetic/lra/search.md) explains selection, sum of infeasibilities, fixed-basic elimination and phase hints, with their route-specific defaults.

## Further implemented work and its evidence

The following notes explain additional arithmetic mechanisms and preserve
their measurements and rejected alternatives. H1–H10 are identifiers in the
historical heuristics register.

The [arithmetic evidence summary](../evidence/arithmetic.md) connects these
mechanisms to dated comparisons and rejected alternatives. Remaining published
table-to-artifact identities are explicit [evidence work](../work/evidence.md),
not inferred from matching aggregate counts.

| Work already done | Current explanation and retained record |
| --- | --- |
| Direct bounds, shared polynomial rows and small rationals (H1/H2) | [Tableau preparation and exact values](arithmetic/lra/tableau.md); {source}`design and measurements <docs/qf-lra-heuristics.md#L217-L439>`. |
| Sparse pivot selection and tuning (H3) | [Pivot policies](arithmetic/lra/search.md); {source}`pivoting investigation <docs/qf-lra-heuristics.md#L440-L538>` and {source}`tuning controls <docs/tuning-parameters.md>`. |
| Sorted row merging, bounds-only rollback, incremental occurrence updates (H8/H9/H10) | [Sparse rows and rollback](arithmetic/lra/tableau.md); {source}`design progression <docs/qf-lra-heuristics.md#L737-L949>`, including the earlier rejected occurrence-maintenance attempt. |
| Fixed-basic-variable elimination (H7) | {source}`try_eliminate_fixed_basic <src/theory/arithmetic/lra_tableau.cpp#L393-L431>` pivots a fixed basic variable out and pins its value. Off by default for pure LRA, on by default for QF_UFLRA; {source}`route defaults <src/smt/check_sat.hpp#L295-L310>` and {source}`design and evidence <docs/qf-lra-heuristics.md#L539-L736>`. |
| Model-based phase hints (H5) | {source}`refresh_model_phase_atom <src/smt/solver_engines.cpp#L7415-L7473>` supplies preferred polarities while SAT keeps variable selection. Enabled by the {ref}`automatic QF_LRA policy <automatic-lra-heuristics>`; {source}`phase-selection investigation <docs/qf-lra-heuristics.md#L982-L1236>`. |
| SOI and alternative pivot policies | [SOI objective, selection, conflicts and historical evidence](arithmetic/lra/search.md#optional-sum-of-infeasibilities); the {source}`base-paper deviation map <docs/papers/fast-linear-arithmetic-dpllt.md>` separates these policies from the default repair loop. |
| Equality sharing and model reconciliation | [Theory combination](combination.md), the {source}`equality-sharing design <docs/qf-lra-equality-sharing.md>` and {source}`tableau equality/model interfaces <src/theory/arithmetic/lra_tableau.hpp#L331-L600>`. Model coincidence is not an entailed equality. |
| Preprocessing and experiment history | {source}`monotone elimination <docs/papers/monotone-variable-elimination.md>`, {source}`guarded equality experiments <docs/qf-lra-heuristics.md#L1237-L1350>` and {source}`arithmetic development record <docs/arithmetic.md>`. |

The [published QF_LRA results](../papers/primo-qflra-results.md) are another
part of this evidence trail. The tuned configurations in that paper have
their own settings and measurement scope. Exact table-to-artifact mapping
remains open.

Open investigations include alternative rational libraries, further fill-in
control, batched assignment updates and
[floating-point-guided basis selection](../leads/lra-floating-point.md). The
{source}`heuristics register <docs/qf-lra-heuristics.md>` retains their
hypotheses and the measurements that would support or reject them.

## Sources and implementation status

The implementation uses the following papers, theses, and sections. The
[review record](../review-history.md#primary-sources-read) identifies the reading copies
and sections checked.

| Source | Role and limits |
| --- | --- |
| Dutertre and de Moura, *Integrating Simplex with DPLL(T)*, SRI-CSL-06-01, 2006, §§3.1–3.3 | Base preparation, repair, conflicts and strict arithmetic. Sparse selection, occurrence layout and the exact rollback policy are Primo's adaptations described above. |
| [Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*](https://cs.nyu.edu/media/publications/king_tim.pdf), PhD thesis, NYU, 2014, §§2.2.7, 2.2.12, 3.2–3.3 | Sparse-operation comparison, heuristic termination and SOI. Primo does not adopt the thesis's full linked-matrix representation or every optimized mechanism. |
| [Alberto Griggio, *An Effective SMT Engine for Formal Verification*](https://iris.unitn.it/handle/11572/368265), PhD thesis, Trento, 2009, §2.5 | Exact-number and tableau engineering; the implemented opt-in least-violated rule. Its reported MathSAT heuristic is distinct from Primo's default. |
| [Martin Bromberger, *Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955), PhD thesis, Saarland, 2019, §§7.1.3–7.1.4 | Backtracking alternatives and sparse data structures. The complete equality-basis algorithms of Chapter 5 remain further work; current local equality rules do not implement that complete method. |
| David Monniaux, *On Using Floating-Point Computations to Help an Exact Linear Arithmetic Decision Procedure*, CAV 2009, §§2.1, 3 and 4 {cite:p}`Monniaux2009` | Polynomial normalization, sparse pivot-cost rationale and two-tier rationals. The floating-point acceleration is not implemented. |
| Tim King, Clark Barrett and Bruno Dutertre, [*Simplex with Sum of Infeasibilities for SMT*](https://theory.stanford.edu/~barrett/pubs/KBD13.pdf), FMCAD 2013, §V {cite:p}`King-fmcad2013` | Optional SOI objective, breakpoint walk and multi-row conflicts; [implemented choices](arithmetic/lra/search.md#optional-sum-of-infeasibilities) distinguish the finite Bland fallback and minimizer from the paper and King's thesis. This does not characterize the default sparse repair loop. |
| Nikolaj Bjørner and Lev Nachmanson, *Arithmetic Solving in Z3*, CAV 2024, §§2, 3 and 6 | Modern comparison for exact sparse arithmetic and propagation; §3.2 is the basis for the optional model-unit equality producer. Integer/nonlinear techniques need their own mappings. |
| Martin Bromberger, Mathias Fleury, Simon Schwarz and Christoph Weidenbach, *SPASS-SATT: A CDCL(LA) Solver*, CADE 2019, §3 and footnote 3 | Theory-model phase recommendations underlying H5. Primo refreshes backend phase hints rather than exposing SPASS-SATT's `GetPhase` callback. |
| Erling D. Andersen and Knud D. Andersen, *Presolving in Linear Programming*, Mathematical Programming 71, 1995, §3.1(iv) | Fixed-variable elimination motivates H7. Primo keeps variables and reasons and pivots fixed basics out; it does not delete columns using the paper's full presolve/restoration pipeline. |

Robert Daniel Brummayer's *Efficient SMT Solving for Bit-Vectors and the
Extensional Theory of Arrays* (PhD thesis, JKU Linz, September 2009),
Chapter 4, informs the grammar-based fuzzing and failure reduction described
in the [contributor correctness guide](../development/correctness.md#fuzzing-and-reduction-sources).

## Invariants exercised by existing tests

| Obligation | Regression or mechanism test |
| --- | --- |
| Exact promotion/demotion preserves arithmetic, equality and hashing | {source}`rational representation and GMP comparison tests <tests/unit/rational_tests.cpp#L272-L383>` |
| A strict boundary cannot be rounded into a satisfying equality | {source}`strict-bound conflict test <tests/unit/lra_tableau_tests.cpp#L177-L197>` |
| Sparse pricing finds candidates and really uses occurrence counts | {source}`sparse selection tests <tests/unit/lra_tableau_tests.cpp#L533-L625>` |
| Heuristic repair eventually invokes the counted Bland fallback | {source}`Bland fallback test <tests/unit/lra_tableau_tests.cpp#L725-L753>` |
| Rows stay sorted; cancelled coefficients lose their occurrences | {source}`row/occurrence tests <tests/unit/lra_tableau_tests.cpp#L1333-L1522>` |
| Rollback preserves equation consistency; SOI restores its extra state | {source}`ordinary rollback tests <tests/unit/lra_tableau_tests.cpp#L238-L416>` and {source}`SOI rollback <tests/unit/lra_tableau_tests.cpp#L992-L1029>` |
| Shared polynomials preserve scaled thresholds and flipped strictness | {source}`preparation tests <tests/unit/qf_lra_solver_tests.cpp#L4363-L4450>` |
| Propagation has a Boolean effect and respects configured caps | {source}`same-variable/row propagation tests <tests/unit/qf_lra_solver_tests.cpp#L3268-L3447>` |
| Reverse row propagation derives another variable's bound and can lead to conflict | {source}`reverse-direction regressions <tests/unit/qf_lra_solver_tests.cpp#L3449-L3535>` |
| SOI exercises the paper's worked pivot example | {source}`SOI worked-example test <tests/unit/lra_tableau_tests.cpp#L798-L829>` |
| Starting-bound slopes distinguish inward/outward motion and count fixed bounds correctly | {source}`SOI endpoint regressions <tests/unit/lra_tableau_tests.cpp#L831-L954>` cover both signs, infinitesimal steps and rollback. |
| SOI reduction can discard unrelated conflicting rows | {source}`independent-conflict minimization test <tests/unit/lra_tableau_tests.cpp#L1066-L1106>` |
| SOI strategies return asserted reasons; deletion order affects clause length | {source}`minimization strategy tests <tests/unit/lra_tableau_tests.cpp#L2621-L2698>` |

Performance investigations additionally use pivot, row-update, term-operation,
Bland-fallback and propagation counters; their publication is visible in
{source}`run_deferred_check_if_needed <src/smt/solver_engines.cpp#L7379-L7413>`.
Correctness qualification still requires the fresh fuzzing and independent
model checks described in the [contributor guide](../development/correctness.md).
