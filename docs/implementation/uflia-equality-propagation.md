# QF_UFLIA equality propagation before integer completion

**Implemented candidate; SLURM qualification pending.**

This implements the integration proposed by the
[active-equality investigation](../leads/arithmetic-equality-exploitation.md).
It reuses Primo's four existing explained tableau rules, rather than adding a
new equality-discovery algorithm or integer equation solver.

## State and ownership

The {ufliaeqsource}`LiaComponentAdapter <src/smt/solver_engines.cpp>` owns prepared arithmetic state and receives the current
assignment and shared arrangement. Its rational check builds or receives a
feasible tableau whose bounds have SAT antecedents. Before calling integer
completion, it queries `LraTableau::entailed_equalities` for the registered
shared Int variables. The input-to-tableau map translates variable IDs back
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
2. With a nonempty shared interface, run the existing equality scan. Row
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


## Counters and current qualification

`:lia.equality.scans`, `candidates`, `already-true`, `proposed`, and `reason-lits`
measure the adapter work; `fixed-eq`, `eq-row`, `eq-offset-rows`, and `eq-rows`
count emitted consequences by discovery rule. `:time.lia.equality-scan-us`
measures scanning and translation. The SMT driver records
`:combination.equality.batches`, `fresh-pairs`, `clauses`, and `conflicts`.
An existing-false equality counts as a theory conflict; adding a fresh
consequence does not manufacture a conflict count.

No performance or correctness qualification result is claimed yet. This
integration adds no CLI option and does not replace the model-guided or
complete-arrangement correctness boundaries.
