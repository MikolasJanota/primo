# QF_UFLIA equality propagation before integer completion

**Both integrations are correctness-qualified. Equality export remains default off; legacy remains the producer default.**

This implements the integration proposed by the
[active-equality investigation](../leads/arithmetic-equality-exploitation.md).
The legacy producer reuses Primo's four explained tableau rules. The selectable
model-unit producer reuses the CAV 2024 §3.2 implementation already available
on the Real route; it adds no integer equation solver. Its integer integration
has passed the [separate qualification](../evidence/uflia-model-unit-20261111.md); its mixed diagnostic results do not justify default promotion.

Enable it with `--qf-uflia-tableau-equalities`. The initial unconditional
candidate passed correctness gates but substantially regressed native search
on the 44-case development comparison below. The flag controls arithmetic
proof export; existing model-guided arrangement search remains separate.

## CAV 2024 model-unit discovery: integration checklist

The supplied primary source is Bjørner and Nachmanson, *Arithmetic Solving in
Z3*, CAV 2024, §3.2 {cite:p}`BjornerN24`. It restricts candidate pairs to
variables with equal values in the current feasible rational assignment, then
uses one-row and same-sign two-row unit-coefficient shapes whose remaining
variables are fixed. Equal model values alone never prove equality.

This integration reuses the existing
{ufliamodelunitsource}`LraTableau::model_filtered_unit_equalities <src/theory/arithmetic/lra_tableau.cpp>` implementation. Its checklist is:

1. {ufliamodelunitsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` owns the selected producer for one component lifetime.
   `--qf-uflia-lia-equality-producer=legacy|model-unit` selects discovery;
   `--qf-uflia-tableau-equalities` independently enables delivery. Legacy and
   disabled export remain the defaults.
2. Query only the rationally feasible tableau before integer completion. The
   rebuilt/model-projection path already repairs its own tableau. The model-unit
   online path without model projection (complete arrangement) copies the
   callback tableau, explicitly repairs the owned snapshot, and
   moves that snapshot into integer completion when no batch is delivered.
   This guards bounds-only rollback, which can retain an infeasible assignment
   after a failed branch. Internal integer branch/cut assumptions cannot enter
   these proofs. The legacy online schedule remains unchanged.
3. Pass mapped shared Int variable IDs. The existing query groups exact
   `DeltaRational` values, inspects nonsingleton classes and recognizes fixed
   equalities, `x = y + fixed`, or `x = ±z + fixed`, `y = ±z + fixed` with the
   same unit sign. It avoids constructing fixed-offset sums and indexes.
4. Each query owns its result vectors and explanation literals. The adapter
   owns its variable-to-term map. No model/tableau borrow survives atom growth.
   Each fixing bound supplies its blocking literal; the adapter negates it
   once and rejects zero or inactive antecedents using the existing checks.
5. Deterministic spanning output avoids enumerating every class pair. Current
   model grouping uses exact values; candidate-pair counts describe potential
   pairs, not a quadratic enumeration by the unit-row query. Row work scales
   with inspected nonzeros; integer numerals can still make exact grouping
   costly. This is not an allocation-free or incremental discovery algorithm.
6. Preserve `all`, `fixed`, `scan` and `names` delivery policies, complete
   arithmetic bridges, false-equality conflicts, finite progress and the full
   arrangement fallback. Discovery may miss non-unit/general-row equalities;
   that is incompleteness of the optional producer, not permission to accept
   incompatible component models.
7. Record model-unit selection, classes, class terms, potential candidate pairs,
   rows/nonzeros inspected, fixed/one-row/two-row matches, rejected non-unit or
   multi-unfixed rows and reason literals. Record skipped legacy scans and a
   discovery-only timer separately from the existing scan/translation timer.
8. Regressions cover shared Int fixed/one-row/two-row consequences before
   completion, equal-valued but unconstrained terms, non-unit fallback, both
   online/rebuild execution, disabled selection, all delivery policies and
   original-model reconstruction. Existing tableau tests exercise large fixed
   offsets, both connector signs, deterministic spanning and bound rollback.
9. Compare legacy and model-unit with identical delivery policies and arithmetic
   settings. First require fresh differential/model gates; preserve baseline
   default behavior. Performance and default acceptance require separate
   measured evidence, not merely the availability of this selector.

For example, `x = z + d`, `y = z + e`, with both `d` and `e` fixed and
`beta(x) = beta(y)`, proves `x = y` using the fixing bounds without computing
and hashing their exact offsets. In integer problems the proof follows from
the rational relaxation, so it holds in every integer solution as well.
The existing `FixedEq` rule is retained alongside the two §3.2 row shapes.
No integer-specific or complete equality algorithm is claimed.

## State and ownership

The {ufliamodelunitsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` owns prepared arithmetic state and receives the current
assignment and shared arrangement. It copies the option into
`propagate_equalities_` for its lifetime; a disabled scan returns immediately.
The LIA component advertises opportunistic equality propagation only while
this option is enabled. Its rational check builds or receives a
feasible tableau whose bounds have SAT antecedents. Before calling integer
completion, it queries `LraTableau::entailed_equalities` (legacy) or
`model_filtered_unit_equalities` (model-unit) for the registered shared Int variables. The input-to-tableau map translates variable IDs back
to copied `TermId` values; terms remain owned by the term manager. No tableau or term-storage reference crosses a
term-creation boundary.

{ufliaeqsource}`TheoryCheckResult::equalities <src/theory/theory_solver.hpp>` owns a batch of explained interface equalities. Each
entry owns an `InterfacePair` and true SAT antecedent literals. Tableau
explanations instead contain blocking literals; the adapter negates them once
and verifies that each resulting antecedent occurs true in its current
assignment. Zero reasons are rejected, not dropped. Already-true arrangement
pairs are omitted; an already-false equality remains a propagation conflict.

`TheoryCheckStatus::EntailedEqualities` and
{ufliaeqsource}`ModelBasedCombinationStatus::PropagateEqualities
<src/smt/model_based_combination.hpp>` distinguish these batches
from SAT-literal propagation and model decisions. The result carries no model token. It returns before patching, cuts or private
branch bounds exist. The completed integer model remains a separate result,
and coincident values in that model do not justify this propagation.

## Operations and invariants

1. Assert the candidate's arithmetic literals and restore rational feasibility,
   or reuse the online route's checked rational tableau.
2. With a nonempty shared interface, run the selected equality scan. Row
   identities and both reasons fixing each required remainder justify every
   candidate. Map only in-scope, same-sort shared endpoints.
3. In proof-delivery modes (`all`, `fixed`), return new retained consequences
   before integer completion. `scan` records discoveries and returns no batch;
   `names` returns only previously unregistered pairs. With no deliverable
   result, continue through the unchanged completion pipeline.
4. {ufliaeqsource}`ComponentModelCoordinator <src/smt/component_model_coordinator.cpp>` validates the producer capability, pair scopes,
   nonzero antecedent literals, uniqueness and progress. It returns a typed equality
   batch to the SMT driver rather than a model-based decision request.
5. {ufliamodelunitsource}`solve_component_recipe <src/smt/component_solver.cpp>` snapshots conflict classification before any atom
   growth, materializes all required equality literals through its existing
   shared-pair registration boundary. Proof-delivery modes add one clause
   `not r1 or ... or not rk or equality` for each explanation; `names` retains
   bridges and suppresses these optional proof clauses. SAT is solved
   again after registration; no new assumption is introduced.

Every learned clause remains valid after backtracking because its premises
are explicit. A batch must materialize a new pair, force an unassigned equality,
or conflict with the current assignment; already-true pairs cannot generate
an endless sequence of empty-effect rounds. The interface is finite within a
ground query. Batch delivery avoids one SAT round for every discovered pair.

Fresh SAT atoms invalidate model-query assumptions in the backend. Consequently,
the driver must not query the old SAT model after the first materialization or
clause insertion. Theory components never receive a SAT backend handle.

## Example: a conflict before integer search

Suppose SAT asserts the four bounds `0 <= x`, `x <= 0`, `0 <= y`, `y <= 0`,
together with `p(x)` and `not p(y)`. An unrelated integer `t` has
`1 <= 2*t` and `t <= 1`; its rational assignment may be fractional.

After rational feasibility repair, `FixedEq` proves `x = y` from the four
bounds, regardless of the value of `t`. The adapter returns that proof before
trying to make `t` integral. The driver names the shared equality and learns
its implication from those four bound literals. On the next SAT solve,
online EUF can use congruence to refute `p(x)` and `not p(y)`. Rebuild mode
also refutes the formula with a normal integer budget; its zero-budget
limitation is described below.

## Algorithmic basis and limitations

Legacy discovery uses `FixedEq`, `EqRow`, `EqOffsetRows` and `EqRows`, described
with their paper mapping in
{ref}`the combination chapter <lra-default-equality-rules>`. This document is
the original design basis for the new adapter/coordinator integration. The
model-unit subset retains fixed equalities and the unit one-row/two-row
shapes; it never emits the general `EqRows` rule. Both producers use identities
and fixed bounds, so their rational consequences also
hold for integer solutions. Their basis-dependent incompleteness remains;
the nonshared `s = x-y` example in the lead is not repaired by this integration.

The scan has the existing rule implementation's expected hashing/sorting and
row-traversal cost, with exact arithmetic depending on coefficient bit size.
The batch uses owning storage proportional to the emitted pairs and reasons.
This is a full-candidate boundary, not a new scan after every asserted bound.
The standalone integer route has no shared interface and skips the scan.
Nonconvex model-guided/complete-arrangement fallbacks remain required.
In rebuild mode, the next round checks LIA completion before EUF. With a zero
integer-search budget it can therefore return `unknown` after emitting a valid
equality batch; online EUF can refute the equality conflict before completion.
The integration does not change that component scheduling policy.

## Verification checklist

- Exercise all four legacy rules and the narrower model-unit fixed/unit-row
  subset through QF_UFLIA, including a multi-pair batch,
  newly registered and already-false equality literals, and both online and
  rebuild/complete-arrangement routes.
- Check guarded activation/deactivation and negative equality behavior, with
  printed SAT models validated against the original assertions.
- Confirm that the equality scan happens before an integer search with a zero
  budget, and that coincident integer model values are not exported as proofs.
- Reject malformed batches, foreign pairs, zero reasons and already-true
  no-progress batches in coordinator tests.
- Count scans, candidates, rule hits, skipped known truths, reason literals,
  batches, fresh pairs, clauses, and conflicts. Compare unchanged baseline and
  candidate under identical limits, preserving per-family outcomes and PAR2.
- Run solver/unit checks, ASan, fresh reference fuzzing and separate model
  validation in SLURM. Local editing, lint and documentation checks stay local.


## Qualification path handling

The {download}`comparison runner <../../scripts/qualification/uflia_equalities/run.py>`
resolves its corpus root once before constructing input paths and relative
provenance keys. Both `--root tmp/QF_UFLIA --manifest selected.txt` and an
absolute corpus root therefore produce corpus-relative file names in the
output. Manifest entries are resolved beneath that root; an entry escaping it
is rejected, and duplicate resolved paths are rejected.

A focused Python regression executes the actual runner with mocked solver and
version calls, checking the selected absolute input and all four profiles'
relative output identities. It launches no solver. This workflow repair
does not change arithmetic solving or replace the separate solver/model gates.

## Counters and current qualification

`:lia.equality.scans`, `candidates`, `already-true`, `proposed`, and `reason-lits`
measure the adapter work; `fixed-eq`, `eq-row`, `eq-offset-rows`, and `eq-rows`
count emitted consequences by discovery rule. `:time.lia.equality-scan-us`
measures scanning and translation. The SMT driver records
`:combination.equality.batches`, `fresh-pairs`, `clauses`, and `conflicts`.
An existing-false equality counts as a theory conflict; adding a fresh
consequence does not manufacture a conflict count.

The opt-in snapshot `8d84fdca` (API source `6e46d0dd`) passed fresh SLURM
qualification in both enabled and disabled configurations:

- 45 C++ tests and 311 Python tests, plus lint;
- all 45 C++ tests under ASan, followed by 150 generated runs;
- 7,000 generated reference comparisons: 6,998 pass and two `unknown`
  results in enabled complete-arrangement mode; the latter are not passes;
- direct Dolmen acceptance for all 3,510 successful generated SAT cases;
- 200 real-seed mutants and 20 original-input answer checks (10 real-seed
  checks plus 10 tracked-regression checks), with all SAT outputs in those stages independently validated;
- clang-tidy/analyzer/include-cleaner coverage of all 133 tracked C++ files.
  Existing baseline advisories remain; no warning-free-tree claim is made.

The integration does not replace model-guided or complete-arrangement
correctness boundaries.

## Paired evaluation and default policy

Equality export remains off by default; legacy remains the producer default.
Both producers and the delivery controls are implemented and qualified. The
[initial 44-input comparison](../evidence/qf-uflia-equality-sharing.md#initial-44-input-development-comparison)
retains its original profiles, model checks and search-cost diagnosis.

## Larger comparison

The [659-input follow-up](../evidence/qf-uflia-equality-sharing.md) records mixed
family results. It does not justify default promotion. Its measurements are
complete; they are not evidence that selective delivery or equation-specific
integer reasoning has been implemented.

## Delivery ablation follow-up

The [completed five-policy ablation](../evidence/qf-uflia-equality-ablation.md)
retains fixed-only delivery experimentally. The
[original checklist](uflia-equality-ablation.md) preserves its design; the
[remaining equality investigation](../leads/arithmetic-equality-exploitation.md)
identifies further work beyond those implemented controls.
