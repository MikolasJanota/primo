# A cube test for finding QF_LIA models

**Status: uniform/difference variants implemented and correctness-qualified;
200-input and full 13,306-input M124 comparisons complete, including all SAT models;
feature remains off by default.**
See {ref}`the current evidence <lia-cube-evidence>`.
Plan baseline: `71a784e6`; implementation baseline: `aeaf2823`. Start with pure QF_LIA; keep the feature opt-in during
qualification. The immediate target is a cheaper integer-model attempt before
cuts and branch-and-bound. Dillig's proof-derived splitting remains a separate,
larger [research direction](../papers/cuts-from-proofs.md).

## Paper basis and implemented subset to target

Bjørner and Nachmanson, [*Arithmetic Solving in Z3*, CAV 2024,
§4.2](https://link.springer.com/content/pdf/10.1007/978-3-031-65627-9_2.pdf),
explicitly describe Z3's cube test and difference/octagon refinements. For
integer coordinates, the basic sufficient condition is rational feasibility of

$$a_i z\le b_i-\tfrac12\sum_j|a_{ij}|.$$

Rounding a feasible center yields an integer point satisfying the original
inequalities. Failure gives no refutation. The first scope is this test plus
the integer-difference exemption. General octagon refinements and equality-basis
elimination are deferred. Our {ref}`cube reading guide <bromberger-unit-cube>`
explains the geometric argument and the difficulty with equalities.

This is a model-finding heuristic, not a complete integer decision procedure.
It neither requires an extreme-point LP solution nor introduces general
expression splits, HNF certificates or artificial finite variable bounds.

## Original qualification checklist

The numbered steps below retain the planned acceptance requirements. The
{ref}`implementation section <lia-cubes>` describes the
implemented representation. The development and full M124 qualification results
are recorded in the evidence page; the numbered steps remain the original plan.

## 1. Establish applicability before broad evaluation

Read the active normalized inequalities from arithmetic preparation, expressed
over semantic integer variables. Count fixed columns, nontrivial equalities,
difference rows, coefficient sizes and opportunities after patching fails.
Record SAT and UNSAT samples separately. Do not infer availability from the
number of fractional tableau basics: a cube concerns the original integer
coordinates, not the current simplex basis.

Use a deterministic 200-input development manifest: 50 reference-fast/Primo-slow
SAT inputs, 50 analogous UNSAT inputs, 50 node-limit cases, and 50 easy controls,
with family coverage and no duplicate inputs. If a bucket has fewer qualifying
cases, retain all and document the deterministic refill. Establish a fresh
baseline on the current source; the historical M124 result is context, not a
paired baseline for a new implementation. M124 enables integer-bound rounding,
primitive integer rows and integral patch moves, with integer slack obligations
disabled. Its explicit flags are `--lia-round-integer-bounds`,
`--lia-primitive-int-rows`, `--no-lia-integer-slacks`, and
`--lia-patch-integral-moves`. QF_LIA's current default enables all four.
The [existing comparison](../evidence/qf-lia-progress.md) identifies the
13,306-file corpus and {download}`benchmark metadata <../../benchmarks/qf-lia-m124-cactus-20261104.json>`.
Before selecting either manifest, verify the full relative-path/content-hash
inventory against that recorded corpus; freeze the selected paths, hashes,
selection recipe and all effective profile arguments. Any census and solver
execution belong in SLURM and the run ledger.

## 2. Implement a numerical helper with owning input/output

Implemented types under `theory/arithmetic/` (see the {ref}`implementation <lia-cubes>`):

- `LiaCubeProblem`: owns sparse affine definitions and ordered semantic
  integer columns. The helper combines them with current active bounds;
  coefficients, offsets and constant substitutions are exact.
- `LiaCubeResult`: reports attempt status and statistics, including failed,
  skipped or budget-exhausted attempts. It has no input-system UNSAT result;
  `try_cube_model` owns validation and adoption into the supplied tableau.
- `try_cube_model`: builds an isolated rational tableau and solves the
  strengthened inequalities. No SAT variables, clauses or SMT-LIB nodes belong
  in this helper.

The SMT-layer `PreparedLraTheory` in
{ufliaablationsource}`solver_engines.cpp <src/smt/solver_engines.cpp>` owns the
mapping from terms and normalized polynomials to numerical columns. Export an
owning arithmetic snapshot; retain the original affine definitions needed for
auxiliary reconstruction. Snapshot lifetimes belong to one prepared query and
its current asserted assignment. They must be refreshed after atom growth or
backtracking changes that assignment.

Required correctness checklist before coding:

- Normalize each selected literal, including false comparisons and strict
  integer bounds, to an equivalent non-strict integer inequality. Clear
  denominators exactly before computing norms.
- Substitute a fixed semantic column only when its value is integral, and
  retain the reconstruction value. A fixed fractional integer column is a
  failed cube attempt; existing refutation machinery remains responsible.
- Compute margins in semantic coordinates. Auxiliary slacks are affine
  expressions, not independent coordinates to round.
- Use one translation-invariant rounding rule, for example
  $R(t)=\lfloor t+1/2\rfloor$, including negative ties. For integer $k$,
  $x-y\le k$ survives this rounding because it is monotone and
  $R(y+k)=R(y)+k$. Only verified primitive difference rows receive the
  exemption; strengthen every other supported row normally.
- Inspect and validate the rounded candidate against all original active
  inequalities and the integrality obligations. A cube-center success alone
  is not the final solver result.

Constraint extraction and margin construction are linear in the sparse input
size, apart from exact-arithmetic bit costs. The extra rational solve can still
be expensive. Account for its pivots, coefficient growth, copies and elapsed
time rather than assuming that a sufficient condition is cheap.

## 3. Integrate once per completion, preserving the current fallback

Current
{ufliaablationsource}`complete_integer_assignment <src/theory/arithmetic/lia_completion.cpp>`
runs patching, a basic GCD check, bounded Gomory rounds and coordinate DFS.
The proposed order is patching, the cheap GCD screen, one cube attempt if still
fractional, then existing cuts/DFS if no candidate was obtained. This schedule
is our integration choice. Do not start with calls at every private DFS node.

Place the new setting in
{ufliaablationsource}`LiaSettings <src/smt/lia_settings.hpp>` and connect it
through the shared completion boundary. Enable it only for the standalone
QF_LIA route in the first implementation; QF_UFLIA retains its current policy.
Use one experimental control with `off`, `uniform`, and `difference` modes so
we can separate baseline cost from the difference exemption. All remain off
by default while evaluated.

Model adoption is a required milestone. The current completion result leaves a
winning `LraTableau` for its caller. A rounded coordinate vector cannot simply
replace that model. Reconstruct auxiliary values, rows and active bounds on a
private candidate tableau, verify it, and replace the working model only on
success. An initial implementation may validate the selected values using
private fixing bounds on a fresh prepared tableau; those bounds never enter SAT
assumptions or a learned explanation. Any failure discards that candidate.
This must be tested with every supported model conversion.

The existing {ufliaablationsource}`tableau API
<src/theory/arithmetic/lra_tableau.hpp>` permits `add_row()` only before its
first `push()`. Build scratch rows before beginning bound scopes. The implementation adds `check_bounded()` with a nonfatal local repair-step
budget. The ordinary `check()` retains its feasibility/conflict contract. Exhausting the cube
budget resumes the original pipeline; genuine user cancellation still exits.
Failed attempts must preserve the caller's bounds, basis, assignment and proof
state. Never use the strengthened system's conflict as an input-theory lemma.

### Initial implementation checklist (2026-11-08)

The first implementation will retain an owning sparse definition of every
original tableau variable in semantic coordinates, exported from the pristine
prepared tableau. Bounds come from the current rationally feasible completion
input. Fixed integral semantic coordinates are substituted before primitive
normalization and margin computation. The optional LP uses a separate Bland
tableau with a finite repair-step limit; its conflicts are discarded.

For adoption, rebuild the pristine numerical rows with the same variable IDs,
set the original nonbasic semantic coordinates to the rounded values before
asserting bounds, then restore original bounds and their reasons. Validate all
reconstructed values against those bounds and every integer obligation. This
avoids private fixing bounds entirely. Only a validated candidate replaces the
working tableau. Definition construction sorts $n$ coordinates in $O(n\log n)$ comparisons;
sparse export and validation cost linear sparse work, excluding exact arithmetic; the bounded LP limits repair steps, not the
bit cost of a single step. No additional global cancellation mechanism is
introduced by this helper.

## 4. Tests and observability

Before timings, require tests for:

- The paper's `3x+y<=9`, `-3y<=-2` example and a model found with zero
  private branching after the cube test succeeds.
- A satisfiable thin region whose uniform cube test fails, proving that the
  old pipeline still finds its model. Include fixed-variable substitution and
  a non-difference equality between unfixed variables.
- `x-y<=1` at positive/negative half-integer ties; demonstrate why inconsistent
  tie rounding would violate the inequality. Test primitive scaling and
  exclusion of non-integer offsets before normalization.
- False and strict atoms, large exact coefficients, redundant inequalities,
  auxiliary reconstruction, failed candidate adoption and exhausted budgets.
- Repeated calls/backtracking and model conversions, with identical main
  tableau/proof state after a failed attempt. Check QF_UFLIA preservation.

Implemented counters distinguish calls, skips, LP infeasibility, local
budget exhaustion, centers found, validated models, adoption failures,
difference exemptions, subsequent branch nodes and helper time. Validation/adoption
failures indicate defects to triage; they must not silently become successes.

## 5. Correctness and performance decision

Build through `slurm_prepare_benchmark_workspace.py --build-via-slurm`. Run
`make check`, memory-sensitive ASan checks, whole-tree static review, and a fresh
merge gate before broad timings. Before promotion, require at least 3,000
reference-checked QF_LIA generated cases and real-seed mutation coverage,
recording unknowns separately. Independently validate printed SAT models with
Dolmen outside timing.

First compare off/uniform/difference on the development manifest under both
the QF_LIA default and M124. Continue to all 13,306 inputs at 60 seconds when
correctness and opportunity measurements justify it. Keep one static profile
per arm, rotate order, retain per-row data, and separately replay every
successful SAT row. Report SAT/UNSAT, timeouts, node-limit unknowns, errors,
PAR2, paired gains/losses, families and common-solve cost. Reuse reference data
only with the required input/binary/option provenance.

Keep the feature experimental unless the model gains pay for the extra LP and
avoid substantial regressions. An ineffective uniform test may motivate the
difference refinement; it is not evidence that all cube variants fail. Further
work on general octagons or implied-equality elimination needs its own paper
checklist and measurements. Dillig remains deferred while this smaller target
is assessed.
