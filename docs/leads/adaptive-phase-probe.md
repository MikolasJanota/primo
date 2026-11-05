# Bounded adaptive phase diagnostic

**Status: diagnostic completed; removal-count gate rejected, prototype retired.**
No solver policy or default change is retained. The qualified prototype is
preserved at `f0cf49b2`; current solver source is restored to `9b30e491`.
The experiment asked whether
repeated decisions agreeing with arithmetic phase advice identify a useful
point to release or reverse that advice on the 30 recorded lost `wisas` files.
At a 15-second cutoff, the tested adaptive gates recovered none, continuous
reversal recovered four, and broader native fixed-phase controls recovered 15.
The unchanged native profile solved all 30. These results justify investigating
phase coverage and native phase evolution, not promoting the regressing profile.
The [phase-control survey](../papers/arithmetic-phase-control.md) distinguishes
the paper-backed current-assignment rule from this original adaptive gate.
Biere–Fleury justify phase-only diversification as a search mechanism;
Shaw–Meel motivate measuring context dependence rather than trusting a blend.
Neither paper supplies this trigger or predicts a QF_UFLIA improvement.

## Candidate state and operations

In the frozen prototype, the {adaptivephasesource}`PhaseProbe controller <src/sat/phase_probe.hpp>`
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

For example, with k=2, a decision agreeing with a stable positive hint is
removed twice before an override begins. A propagated positive assignment
does not count. If the hint changes to negative between the two removals,
the count starts again. Release withdraws the installed preference; inversion
installs a negative preference. Neither action changes the arithmetic assignment.

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
negative reward: the experiment must measure whether the gate fires,
decisions occur under it and solves are recovered. If assumptions keep the slack pinned,
release of unrelated phase preferences may have no effect. That is a useful
negative diagnostic outcome, not permission to override assumptions.

The current {download}`runner <../../scripts/qualification/adaptive_phase/run.py>`
keeps detailed stdout/stderr and one durable JSON row per completed attempt.
The {download}`batch wrapper <../../scripts/qualification/adaptive_phase/run.sbatch>`
gets all deployment locations from its environment. Connection details are
kept outside tracked files.

## Results: which phase simulation helped?

The selection contains all 30 previously recorded M124 losses and 14 stable
controls from four families. Controls had solved in both historical profiles
within five seconds and were chosen by a deterministic hash rule, up to four
per family; only two Certora files qualified. The four development files are
`xs_24_44`, `xs_28_48`, `xs_33_53` and `xs_40_60`.
The {download}`selection record <../../scripts/qualification/adaptive_phase/selection.json>`
and manifests preserve that choice. All timing arms ran on SLURM, once per
file with a 15-second limit, rotated arm order, two CPUs and 8 GiB per task.
Model checks ran separately and do not enter timing.

| Profile | Lost files solved / 30 | Controls solved / 14 |
|---|---:|---:|
| Native: four integrality options off, arithmetic phases off | 30 | 14 |
| M124, arithmetic phases off | 0 | 14 |
| M124 with current β advice | 0 | 14 |
| Always release β advice (k=0) | 0 | 14 |
| Always reverse β advice (k=0) | 4 | 14 |
| Release or reverse after k=8 removals | 0 each | 14 each |
| Release or reverse after k=1 removal | 0 each | 14 each |
| M124, β off, native `forcephase=1, phase=0` | 15 | 14 |
| M124, β off, native `forcephase=1, phase=1` | 15 | 14 |

M124 means integer-bound rounding, primitive integer rows and integral-move
patching on, integer-slack obligations off. The explicit native baseline
disables all four. The exact flags and probe environment are defined in
`run.py`, so labels do not depend on future defaults. The β row is observe-only
instrumentation. The development grid also included uninstrumented β and
k=2/32: only native solved those four development losses. A k=1 confirmation
then repeated native and M124 baselines alongside the two early gates.

Continuous inversion recovered `xs_25_35` (12.609 s), `xs_26_36` (8.914 s),
`xs_28_38` (1.798 s), and `xs_32_42` (13.722 s). Both uniform-sign controls
recovered the same 15 losses:

```text
xs_25_35 xs_26_36 xs_27_47 xs_28_38 xs_29_39
xs_30_40 xs_30_50 xs_31_51 xs_33_43 xs_33_53
xs_34_44 xs_34_54 xs_35_55 xs_36_46 xs_39_59
```

Their union with inversion contains 16 losses because inversion also recovers
`xs_32_42`. This is an oracle union of independently run arms, not a measured
selector or portfolio. No selector is implemented. All controls completed
within the cutoff, which does not establish absence of runtime regressions.

The complete {download}`benchmark metadata <../../benchmarks/qf-uflia-adaptive-phase-20261105.json>`
contains input and binary hashes, per-profile PAR2 sums, family counts,
held-out counts, probe totals, successful loss rows and raw-row hashes.
Across the five candidate timing stages there are 628 attempts: 204 SAT,
78 UNSAT and 346 timeouts, with no errors or wrong answers against input
statuses. All 204 successful SAT rows passed direct Dolmen validation in
separate replays using the same binary, flags and environment. Successful
UNSAT rows are not model-validation claims.

## What the negative gate result tells us

The gate was active: across the full k=1 comparison, release activated
1,463,798 times and inversion 1,318,526 times. Respectively 460,479 and
379,923 observed decisions occurred while overrides were active. The summary
field `overridden` counts such decisions; it does **not** prove their signs
differed from the native or counterfactual choice. Similarly, agreement with
an installed hint does not establish that the hint caused a conflict.
The negative result applies to the tested removal-count policy and windows,
not to every adaptive phase policy.

There is also a coverage difference. The
{settingssource}`arithmetic phase producer <src/smt/solver_engines.cpp#L9242-L9301>`
skips arithmetic equality atoms. The raw `xs_28_48` input contains many
equalities selecting integer values, whereas this gate only overrides
variables receiving installed hints. Model-combination assumptions are
excluded as well. A source/input audit identifies that limitation; it is not
an executed trace proving that equality decisions caused the losses.
Uniform native controls cover a broader set of SAT variables, including
Boolean auxiliaries, so their recovery cannot be attributed to equalities
alone.

The uniform controls use existing CaDiCaL options with β disabled. In
[CaDiCaL 3.0.0 option declarations](https://raw.githubusercontent.com/arminbiere/cadical/rel-3.0.0/src/options.hpp),
`forcephase` always selects the initial phase; `phase` supplies its sign.
The [release notes](https://raw.githubusercontent.com/arminbiere/cadical/rel-3.0.0/NEWS.md)
document that external API phase advice has higher precedence, hence the
explicit β-off control. In Primo, custom options are passed to backend `set`;
their presence does not select a different QF_UFLIA solver route. Assumptions
mode rejects custom options, and was not used here.

Both uniform signs solving the same set suggests that overriding native
phase evolution/history may matter more here than the particular uniform
sign. This is an inference, not an isolation of phase saving from target,
random or other native phase mechanisms. It motivates a separate coverage
and decision trace before designing another adaptive rule.

Four development files all have B=A+20; all four inversion recoveries have
B=A+10 and lie outside that development set. Thus the initial four-file grid
would have missed this positive control. The final model on `xs_32_42`
corresponds to t=12 rather than the minimum 11, so recovery is not evidence
that the heuristic always chooses the smallest slack value.

## Qualification, provenance and retirement

The qualified Release snapshot is `f0cf49b252b4f9b9ecca22166d4c49d54d60f4b2`,
GNU C++ 11.4.1, CaDiCaL `cadical-3.0.0-7b99c07`.
Its Primo SHA-256 is
`9bb8cc30f957037306911f02bac71f4b63d4c1b5be0d48da3a3e7f4a7bbeb85d`.
The preserved `8357ff8b` baseline recheck found native solving all four
development files in 0.41–1.79 seconds and both M124 arms timing out at
12 seconds. GDB sampling of `xs_28_48` showed distributed SAT/arithmetic work;
debugger time is excluded from timing evidence.

| SLURM jobs | Evidence |
|---|---|
| 354170 | Candidate build through `--build-via-slurm` |
| 354174 | 46 CTest and 311 Python tests; 2,000 fresh mixed QF_UFLIA comparisons against Z3; 1,002 generated SAT models pass Dolmen |
| 354175 | ASan: 46 CTest targets and 40 generated reference comparisons pass |
| 354176 | Whole-tree clang-tidy: 130 source/test translation units plus four documentation examples; zero errors, 755 unique warnings remain |
| 354177 / 354178 | Four-file threshold grid / model validation |
| 354185 / 354186 | 44-file β/static/k=8 comparison / model validation |
| 354200 | 2,000 fresh uniform-phase comparisons against Z3 |
| 354201 / 354202 | Uniform controls on development files / model validation |
| 357025 / 357028 | Uniform controls on all 44 selected files / model validation |
| 357026 / 357027 | k=1 confirmation on all 44 files / model validation |

The generated gates have zero wrong answers, crashes, parser/type errors,
unsupported results, reference disagreements or timeouts. The uniform gate
checks answers only; its generated SAT models were not separately validated.
Per-stage seeds, commands, resource limits, binary identities and failure
artifact locations are recorded in `docs/run-log.json` and the benchmark
metadata. The static tree is not warning-free: existing advisories remain,
and the diagnostic added enum-size and test-entry exception advisories.

Earlier attempts are retained rather than counted as successful evidence:
354165 lacked `/usr/bin/time`; 354166 supplied the corrected baseline timing.
354171 mistakenly sent UNSAT inputs through model replay, producing a
probe-stderr/get-model-refusal harness interaction. SAT-only expected-SAT
replay fixed the harness without weakening Dolmen acceptance. ASan job
354172 could not link the missing runtime; an official matching Rocky RPM
was extracted only inside the isolated workspace for 354175. Pending static
job 354173 was cancelled and replaced. All jobs are terminal.

This remains a single-attempt, 15-second diagnostic on a retrospective
selection. Some recoveries are close to the cutoff. The probe computes β
and scans active overrides even for static controls; always-release is not
timing-equivalent to disabling β. No precise wall-time speedup, full-corpus
gain, default promotion or new termination guarantee follows.

The gate is rejected for adoption on this evidence. `CMakeLists.txt`, `src/`
and unit tests are restored to `9b30e491`; frozen history and
{download}`source.patch <../../scripts/qualification/adaptive_phase/source.patch>`
preserve the experimental controller. The patch applies to that baseline
with `git apply --unidiff-zero`. Runner scripts remain reproduction tools;
probe profiles require the frozen experimental binary and are not production
CLI options.

To reproduce, create a separate worktree at `f0cf49b2`, use
`scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm` with the
session's external connection configuration, and stage the **current**
qualification scripts under that workspace's `tmp/`. The current scripts
include corrected SAT-only replay and later uniform arms absent at the frozen
revision. Run the helper's `--check-only --no-sync` preflight before submission.
For example, copy the current `scripts/qualification/adaptive_phase/` directory
to `<isolated-workspace>/tmp/phase-methods/`, including its manifests. With
`PRIMO_WORKDIR` and `PRIMO_CORPUS_ROOT` exported outside tracked files, the following
submits the seven-arm selected comparison and its model replay. Both script
overrides are required: the frozen solver checkout lacks the replay scripts
and its original runner lacks the uniform controls.

```bash
cd "$PRIMO_WORKDIR"
mkdir -p logs
phase_methods="$PRIMO_WORKDIR/tmp/phase-methods"
export PRIMO_EXPECTED_GIT=f0cf49b2
export PRIMO_PROBE_RUNNER="$phase_methods/run.py"
export PRIMO_PROBE_REPLAYER="$phase_methods/replay.py"
export PRIMO_PROBE_MANIFEST="$phase_methods/evaluation.txt"
export PRIMO_PROBE_ARMS="native m124 beta release-0 invert-0"
PRIMO_PROBE_ARMS+=" release-8 invert-8"
export PRIMO_PROBE_SHARDS=8 PRIMO_PROBE_TIMEOUT=15
export PRIMO_PROBE_OUT="$PRIMO_WORKDIR/results/selected-fresh"
export PRIMO_PROBE_PARENT="$PRIMO_PROBE_OUT"
export PRIMO_PROBE_REPLAY_OUT="$PRIMO_WORKDIR/results/models-fresh"
phase_job=$(sbatch --parsable --array=0-7 --export=ALL \
  "$phase_methods/run.sbatch")
sbatch --array=0-7 --dependency="afterok:$phase_job" --export=ALL \
  "$phase_methods/replay.sbatch"
```

Use fresh output paths for every submission and a matching array size for
replay. For the uniform comparison set `PRIMO_PROBE_ARMS="force-positive
force-negative"`; for the early comparison use `"native m124 release-1
invert-1"`. Analyze completed timings with the staged files:

```bash
python3 "$phase_methods/analyze.py" --root "$PRIMO_PROBE_OUT" \
  --selection "$phase_methods/selection.json" --timeout 15 \
  --out "$PRIMO_WORKDIR/results/selected-summary.json"
```

Local artifacts are under
`results/adaptive-phase-20261105/`; complete generated inputs remain in the
isolated remote result workspace, with hashes and summaries collected locally.
