# Bounded adaptive phase diagnostic

**Status: isolated implementation candidate; qualification and results pending.**
This is not a production policy or default proposal.
The experiment is isolated from `lia-integrality`. Its question is whether
repeated decisions agreeing with arithmetic phase advice identify a useful
point to release or reverse that advice on the 30 recorded lost `wisas` files.
The [phase-control survey](../papers/arithmetic-phase-control.md) distinguishes
the paper-backed current-assignment rule from this original adaptive gate.
Biere–Fleury justify phase-only diversification as a search mechanism;
Shaw–Meel motivate measuring context dependence rather than trusting a blend.
Neither paper supplies this trigger or predicts a QF_UFLIA improvement.

## Candidate state and operations

The {adaptivephasesource}`PhaseProbe controller <src/sat/phase_probe.hpp>`
owns per-variable hint records and observed SAT-decision records.
{adaptivephasesource}`CadicalBackend <src/sat/cadical_backend.cpp>` owns the
controller, and its external propagator adapter borrows it for the lifetime
of that connection. It sees
only SAT literals, level boundaries and explicit assumptions, never terms.
The arithmetic producer and theory checks are unchanged.

- Record the latest installed sign for each variable. In these QF_UFLIA
  profiles initial theory-phase installation is absent, so arithmetic phase
  output is the relevant installed advice. This attribution is not universal
  for other logics/routes.
- Query the backend's decision predicate before forwarding an observed
  assignment. Record actual decisions, their level, the current hint sign and
  whether an override was active. Do not infer decisions from notification
  ordering or the first observed literal at a level.
- Exclude explicitly supplied assumption variables from the trigger. A
  model-combination equality may be an assumption, which phase control cannot
  reverse. Propagations also do not count as decisions.
- Count removal of a decision only when it matched the latest hint sign at
  assignment time and that hint context is still unchanged. This is agreement
  with advice, not proof that the advice caused the choice or the conflict.
  Decisions recorded under an override, and removals while an override is
  active, do not contribute to the trigger.
- After k such removals, either release the installed preference through the
  backend's unphase operation, or publish its opposite sign. Reset the removal
  counter. Keep the override for a bounded number of subsequent observed
  decisions, then restore the most recent hint. A change of hint sign also
  resets the context. Per-hint removal counters survive ordinary backtracking, but reset at each
  SAT solve-call boundary. Several such calls can occur inside one SMT query.
  Summary counts instead accumulate over the backend lifetime.
- An always-release control and an always-opposite control distinguish the
  gate from a static policy. An observe-only arm retains instrumentation while
  leaving the installed sign unchanged.

The controller returns actions; the adapter applies them after controller
state changes, avoiding borrowed map entries across backend calls. SAT solve-call boundaries and
connection establishment clear decision records; disconnection itself does not. Hint signs remain recorded
because their backend preferences can persist between calls; static controls
reapply their policy at the next solve call. On model cancellation, preserve
partial logs and classify the row as timeout regardless of a printed answer.
No action adds a clause, numeric equality, assumption, decision variable or
new atom. SAT variable selection, propagation, explanations and theory conflict
checks retain their existing owners and proof rules.

Per-hint and per-removed-decision updates use expected constant-time maps.
Expiration work is bounded by the active overrides. Storage is linear in
recorded hinted variables, peak observed live decisions and explicit assumption
variables. Container capacity can persist after records are cleared. Detailed tracing is capped and
kept separate from timing. The {adaptivephasesource}`controller regressions
<tests/unit/phase_probe_tests.cpp>` cover the counter-specific invariants. This is not an implementation of a new complete
integer procedure; existing resource limits still apply.

## Diagnostic sequence and acceptance

1. Recheck four historically traced losses with the preserved source-equivalent
   binary, both with and without arithmetic phases, and profile a representative
   slow run using the existing GDB stack sampler on SLURM.
2. Build the isolated candidate through the SLURM preparation helper. Verify
   controller-specific invariants with deterministic tests: propagation and
   assumption exclusion, same-sign context, threshold, expiration, changed hints,
   release versus opposite actions and SAT solve-call reset.
3. Run the relevant solver checks, memory-sensitive ASan checks, static review,
   and a fresh mixed QF_UFLIA reference gate. Keep all answer/model failures
   blocking. Do not promote this diagnostic to a default profile.
4. On the four development losses, compare native profiles, observe-only,
   static controls and a small predeclared threshold grid. Preserve exact input
   hashes, binary identity, environment, commands, limits, decisions, removals,
   activations and release/opposite actions. Rotate arm order within allocation.
5. If an override is promising, test it on all 30 recorded losses and stable
   family controls, keeping development cases identified. This is a targeted
   diagnostic, not a full-corpus generalization claim. Independently validate
   every reported SAT recovery in a separate replay with the same profile.
6. Record accepted or rejected results, scripts, manifests and provenance in
   the run log. Keep production source unchanged unless a later qualification
   establishes a reason to adopt the policy.

The initial k grid is 2, 8 and 32; an override lasts 64 observed decisions.
Observed decisions are the backend notifications for observed variables, not
necessarily all propositional decisions. Repeated conflicts alone are not a
negative reward: the experiment must show that the gate actually fires,
changes decisions and recovers solves. If assumptions keep the slack pinned,
release of unrelated phase preferences may have no effect. That is a useful
negative diagnostic outcome, not permission to override assumptions.

The {adaptivephasesource}`runner <scripts/qualification/adaptive_phase/run.py>`
keeps detailed stdout/stderr and one durable JSON row per completed attempt.
The {adaptivephasesource}`batch wrapper <scripts/qualification/adaptive_phase/run.sbatch>`
gets all deployment locations from its environment. Connection details are
kept outside tracked files.
