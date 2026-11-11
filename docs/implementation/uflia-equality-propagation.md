# QF_UFLIA equality propagation before integer completion

**Implemented and correctness-qualified experiment; default off.**

This implements the integration proposed by the
[active-equality investigation](../leads/arithmetic-equality-exploitation.md).
The legacy producer reuses Primo's four explained tableau rules. The selectable
model-unit producer reuses the CAV 2024 §3.2 implementation already available
on the Real route; it adds no integer equation solver. Its integer integration
is undergoing separate qualification below.

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
`LraTableau::model_filtered_unit_equalities` implementation. Its checklist is:

1. `LiaComponentAdapter` owns the selected producer for one component lifetime.
   `--qf-uflia-lia-equality-producer=legacy|model-unit` selects discovery;
   `--qf-uflia-tableau-equalities` independently enables delivery. Legacy and
   disabled export remain the defaults during qualification.
2. Query only the rationally feasible tableau before integer completion, for
   both rebuilt and online components. Internal integer branch/cut assumptions
   cannot enter these proofs.
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

The {ufliaeqsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` owns prepared arithmetic state and receives the current
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
3. If new consequences exist, return their batch without integer completion.
   Otherwise continue through the unchanged completion pipeline.
4. {ufliaeqsource}`ComponentModelCoordinator <src/smt/component_model_coordinator.cpp>` validates the producer capability, pair scopes,
   nonzero antecedent literals, uniqueness and progress. It returns a typed equality
   batch to the SMT driver rather than a model-based decision request.
5. {ufliaeqsource}`solve_component_recipe <src/smt/component_solver.cpp>` snapshots conflict classification before any atom
   growth, materializes all required equality literals through its existing
   shared-pair registration boundary, and adds one clause
   `not r1 or ... or not rk or equality` for each explanation. SAT is solved
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

The mathematical discovery rules remain `FixedEq`, `EqRow`, `EqOffsetRows` and
`EqRows`, described with their paper mapping in
{ref}`the combination chapter <lra-default-equality-rules>`. This document is
the original design basis for the new adapter/coordinator integration. The
rules use identities and fixed bounds, so their rational consequences also
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

- Exercise all four rules through QF_UFLIA, including a multi-pair batch,
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

The {download}`recorded comparison <../../benchmarks/qf-uflia-tableau-equalities-20261107.json>`
preserves the initial unconditional `f526ed8e` experiment and a six-arm
repeat of `8d84fdca`, both against unchanged baseline `1b2cc307`.
The fixed development selection contains 30 historical M124 losses and 14
controls, with a 15-second timeout. Arithmetic model phase is `off` in all
arms. M124 means bound rounding, primitive integer rows and integral patch
moves, with integer slacks off.

The final repeat gives the following solved counts and PAR2 sums in seconds:

| Profile | Old baseline | New default (off) | Option enabled |
| --- | ---: | ---: | ---: |
| Native | 44/44; 32.76 | 44/44; 32.38 | 18/44; 826.16 |
| M124 | 14/44; 906.57 | 14/44; 906.58 | 16/44; 866.82 |

The disabled path performs no equality scans and has no solved-count gains
or losses. The small runtime difference is not a speedup claim. Enabling
export loses 26 native cases, including two controls. M124 recovers
`wisas/xs_25_35.smt2` and `wisas/xs_26_36.smt2` and loses none. All 114
successful SAT timing rows passed separate direct Dolmen replay. The timing
comparison checks declared sat/unsat statuses and inter-arm concrete-answer agreement;
trusted-reference fuzzing is a separate correctness gate. This development
selection does not supply full-corpus performance evidence.

The enabled repeat adds 11,483 fresh pairs in native and 15,316 in M124.
The initial `f526ed8e` diagnostic on `wisas/xs_37_57.smt2` explains why
promotion was rejected: native grows 274 fresh pairs in ten batches. Equality
scanning costs about 11 ms, while solve time exceeds 15 seconds, versus
1.60 seconds in the baseline. Before timeout, LRA pivots grow from 12,704
to 63,161 and SAT decisions from 24,231 to 75,843. A separate bounded GDB
sample finds arithmetic work in 17 of 30 candidate stacks, all within SAT
solving; it is wall-time stack sampling of one input, not CPU attribution.

The option preserves the requested integration for research. Default
enablement is rejected, and M124 also remains unpromoted. A follow-up lead
is to share relevant proved equalities without eagerly adding the full
arithmetic bridge for every fresh pair, and to examine explanation width.
Direct guarded facts or lazy bridge activation need their own design,
explanations, lifecycle and paper review; neither is implemented here.


## Larger comparison

The [659-input, 60-second follow-up](../evidence/qf-uflia-equality-sharing.md)
finds a mixed result: native solves 575 cases disabled and 567 enabled;
M124 solves 545 disabled and 566 enabled. All 3,373 successful Primo rows
match fresh Z3 answers, and all 2,331 SAT timing rows pass separate direct
Dolmen replay. The original development sample missed important MathSAT
gains. Wisas still regresses, while a selected Certora regression enters a
different, integer-completion bottleneck. The evidence page separates those
mechanisms, profiling limits and selected controls. No default is promoted.

The [delivery-cost ablation checklist](uflia-equality-ablation.md) documents
the implemented controls and their completed evaluation.


## Delivery ablation follow-up

The [five-policy investigation](../evidence/qf-uflia-equality-ablation.md) separates
scanning, naming/bridges and proof delivery on all 659 available inputs. It finds
a narrower fixed-value-only policy worth retaining experimentally, while broad
naming still causes regressions and integer-limit fallbacks. The original
all-rule integration and all delivery controls remain default off.
