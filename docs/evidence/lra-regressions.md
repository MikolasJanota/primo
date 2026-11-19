# QF_LRA option regressions, 11 October 2026

The [completed full-corpus ablation](arithmetic.md#migration-branch-option-ablation)
confirmed aggregate gains, but its paired repetitions also confirmed substantial
losses. This investigation locates substantial costs in those slower runs:
model phase can lead into expensive arithmetic pivoting; elimination can change
the search enough to outweigh a smaller tableau; and additional conflicts expose
a full-tableau scan on backtracking. These observations do not establish
pathological simplex behavior or an implementation defect. This diagnostic
campaign did not change either option. The later
{ref}`automatic LRA policy <automatic-lra-heuristics>` is
qualified separately.

## Scope and qualification

The input manifest is the union of all **33 distinct lost solves** from the
1,753-input, 30-second full-corpus experiment. It includes losses that did not
repeat, so the diagnostic set is not a new representative benchmark corpus.
The solver is the same frozen `4200525` Release binary, SHA256
`4ff4f6c58c7ee93f75898639df8e49a6dcbd53d40a6d8565d12ef08ab5e1a90b`,
with CaDiCaL `3.0.0-7b99c07` and default-off options.

All work ran on SLURM `compute` nodes with an E5-2690 v4, one physical CPU
and 7 GiB per task. Job `188174` collected all four profiles on each input:
132 runs with `--stats`, a 45-second limit, SIGTERM, and an eight-second
cooperative-cancellation grace. Twenty-four runs timed out; nine could not
flush counters before the cancellation alarm. Every run exited with code zero,
and every decisive answer agreed with the original default answer.

Jobs `188199` and `188238` sampled all four profiles on eight representative
inputs with GDB, a 0.2-second jittered interval, seed 1 and a 60-second deadline.
Of 32 profiles, 26 exited normally and six reached the deadline; the latter
contain explicitly partial samples. The total is 4,501 sampled stacks.
Job `188225` made 12 bounded probes of existing alternative pivot rules.
All 12 timed out; six lacked final counters. All 77 SLURM array tasks completed
with exit code zero, including wrappers that retained partial deadline profiles.

These are **diagnostic runs**. Statistics, cancellation and GDB can perturb
timing. They do not replace the original speed measurements or their separate
Dolmen model qualification. No new solver implementation was introduced.
The ledger entry is `2026-10-11-migration-lra-regression-diagnosis`.

The retained tables contain [all counters](data/lra-regressions-20261011/counters.csv),
[stack categories](data/lra-regressions-20261011/profiles.csv), and
[pivot-rule probes](data/lra-regressions-20261011/sensitivity.csv).
Blank counters mean unavailable, not zero. Stack categories are inclusive and
can overlap; their percentages must not be added.

## Model phase can enter an expensive arithmetic path

For `latendresse/ecoliMILPglycerolYices3-5000.smt2`, the original paired
measurements gave about 8.5 seconds for default, while phase and both timed out
even in the supplementary 1,200-second reruns. In the new diagnostic run,
monotone elimination found **zero candidates and folded zero atoms**. Default
and monotone had identical search counters: 968 SAT decisions, 3,704 pivots,
71,898,345 pivot term operations and 5,188,060 big-rational operations.
Elimination is therefore not the transformation responsible for this cliff.

The phase profile had pivoting in **287/290** sampled stacks; both had
**283/290**. GMP appeared in 182/290 stacks for each profile. Neither had a
sample in phase refresh. This locates the observed cost in arithmetic pivoting
and rational operations, rather than phase-refresh overhead. It does not prove
cycling or determine coefficient growth: neither phase run flushed final
counters, and these stack samples do not measure coefficient sizes.

Changing the existing pivot rule to Bland, Dantzig or SOI did not rescue
either default or phase within the diagnostic limit. All six probes timed out
without final counters; their stalled location was not sampled. A global pivot-rule switch
would therefore sacrifice this input's working sparse-rule baseline.

There is also a Boolean-search variant of phase regression. For
`2019-ezsmt/Labyrinth/SCC_Strong/0028-labyrinth-12-0.smt2`, the phase profile
sampled CaDiCaL conflict analysis in 87/292 stacks and Boolean propagation in
77/292, with no sampled tableau pivots. Phase refresh appeared in 19/292.
The same option can thus regress through different search paths.

### What the current phase interface actually does

{ablationsource}`OnlineLraPropagator <src/smt/solver_engines.cpp>` owns a
tableau, a cached phase per SAT variable, and dirty assignment indices.
It refreshes arithmetic polarities at conflict-free points and sends changes
through {ablationsource}`CadicalBackend::phase <src/sat/cadical_backend.cpp>`.
The pinned [CaDiCaL API](https://github.com/arminbiere/cadical/blob/7b99c07/src/cadical.hpp#L844-L848)
describes `phase` as forcing a variable's default decision phase and provides
`unphase`. Primo's adapter exposes no corresponding release operation.

SPASS-SATT §3, Figure 2 and footnote 3 instead obtain the polarity of the
selected decision literal through an immediate `GetPhase` query
{cite:p}`bromberger2019spass`. Primo implements model-satisfaction polarity
with refresh-and-cache delivery, not that exact callback timing. The
[implementation page](../implementation/arithmetic/lra/search.md#model-phase-hints) describes
that limitation. Persistent cached preferences and their freshness are
reasonable targets for a controlled policy experiment; these measurements
do **not** isolate stale preferences as the cause of the cliff.

## A smaller tableau can still require more search

For the CooperatingT2 input
`elmhes.c.i.elmhes.pl.t2.nor.t2.rlgfixed.t2.c_Iteration5_Loop_7-phaseTemplate.smt2`,
elimination removes 39 variables and folds 2,968 atoms. Prepared rows fall from
15,796 to 14,328 and nonzeros from 79,375 to 64,259. The pass itself takes
about 0.20 seconds, but the later search becomes substantially more expensive:

| Profile | Diagnostic outcome | Reported SAT conflicts | Theory conflicts | Pivots | Pivot term operations |
| --- | --- | ---: | ---: | ---: | ---: |
| Default | SAT, 4.33 s | 20 | 3,357 | 610 | 8,134,978 |
| Monotone | SAT, 43.48 s | 494 | 20,549 | 7,982 | 429,490,347 |
| Phase | Timeout, counters at cancellation | 1,718 | 10,607 | 7,618 | 430,141,873 |
| Both | SAT, 6.65 s | 272 | 2,431 | 1,861 | 31,746,259 |

Elimination alone performs about **53 times** as much pivot term work. The
monotone profile samples pivoting in 133/198 stacks, compared with 2/20 for
default. Combining the flags substantially mitigates this particular loss,
even though each flag alone loses it at the original 30-second cutoff.
The effects are not additive.

The opposite interaction appears on
`sumit.t2.c_Iteration2_Loop_7-phaseTemplate.smt2`: default and monotone finish
with roughly 5.3 million and 3.1 million pivot term operations, while both
needs about 270 million and phase exceeds 369 million before cancellation.
Removing constraints preserves satisfiability, but the resulting SAT decisions,
theory conflicts and tableau bases need not follow a cheaper trajectory.

## Backtracking is costly even in runs with fewer pivots

For `aviad_true-termination.c_Iteration1_Loop_3-lexTemplate.smt2`, monotone
elimination reduces pivot term operations from 12,049,293 to 1,771,528 and
SAT decisions from 3,251,684 to 1,025,470. Nevertheless, theory conflicts
increase from 2,369 to 3,858; diagnostic runtime rises from 29.14 to 35.11 seconds.

The sampled monotone execution spends **78/137 stacks in `LraTableau::pop`**
and none in pivoting. Default has 68/146 stacks in `pop`. Conflict analysis
often calls backtracking, so its inclusive samples overlap with `pop`.
The monotone pass accounts for 8/137 stacks and 1.91 seconds in the separate
counter run; it is not the only extra cost.

The concrete mechanism is
{ablationsource}`LraTableau::pop and rebuild_violated_vars <src/theory/arithmetic/lra_tableau.cpp>`.
After undoing a nonempty trail segment, `pop` clears the violated-variable set
and scans **every tableau variable** to reconstruct it, even on the default
bounds-only backtracking path. The monotone `aviad` tableau still contains
168,683 variables. Each such reconstruction costs a full traversal and bound
comparisons, regardless of the small number of bounds actually undone.

This cost also appears on
`brp_withassume.t2.c_Iteration1_Loop_6-phaseTemplate.smt2`: monotone increases
theory conflicts from 1,849 to 6,715 before cancellation, and `pop` appears
in 161/291 sampled stacks. On
`sas2.t2.c_Iteration6_Lasso_5-phaseTemplate.smt2`, the monotone profile instead
has pivoting in 253/291 stacks. The elimination losses therefore do not all
have one arithmetic bottleneck.

## Next experiments and decision

One concrete optimization candidate is **incremental violation-set repair
for bounds-only backtracking**. A design and regression tests must establish
that only restored bounds can change membership on that path, while assignment,
basis and SOI restoration retain the necessary broader repair. That optimization
has not been implemented or qualified here. Its hot-path cost does not prove
that it causes the option regressions; a controlled implementation comparison
would need to measure whether reducing that cost changes their outcomes.

Separately, compare phase delivery policies with explicit measurements of
freshness, arithmetic work and SAT search. The severe phase cliff requires
visibility into unfinished tableau checks: ordinary final counters disappear
when the current cancellation alarm expires inside `check`. Refresh throttling
alone is not justified by the severe-case samples. A policy alternative must
be evaluated on the full corpus, with fresh fuzzing and separate model checks,
before any default decision.

Both flags were opt-in in the frozen binary. Their confirmed aggregate gains
remain valid for that binary, and the repeatable regressions remain part of that
assessment. More pivots or more pivot term work alone is not evidence of a
pathology: changing the Boolean search, asserted bounds or tableau basis can
change both pivot count and cost. A default-policy assessment should weigh the full-corpus gains
against the severity and persistence of losses; these diagnostics do not
establish a requirement to eliminate every regression first. No family-specific
routing or competition portfolio is proposed.

## Reproduce the diagnosis

The tracked driver and manifests are under `scripts/qualification/lra_ablation`.
The {regressionworkflow}`diagnostic driver <scripts/qualification/lra_ablation/diagnose.py>`,
{regressionworkflow}`batch wrapper <scripts/qualification/lra_ablation/diagnose.sbatch>` and
{regressionworkflow}`auditor <scripts/qualification/lra_ablation/summarize_diagnostics.py>`
are pinned together with the manifests; the
{regressionworkflow}`GDB sampler <scripts/profile_gdb_samples.py>` supplies stack samples.
In a preflight-checked frozen SLURM workspace, set `PRIMO_WORKDIR`,
`PRIMO_WORKFLOW`, `PRIMO_DIAG_PLAN` and `PRIMO_DIAG_OUT`, then submit
`diagnose.sbatch` with `PRIMO_DIAG_MODE=counters` and array `0-32`.
Use `profile` with array `0-15` for each of `diagnostic-plan.json` and
`followup-plan.json`, storing the latter under `followup`; use `sensitivity`
with array `0-11` and `sensitivity-plan.json`. Keep each output directory fresh.
The workflow directory needs `diagnose.py`, `run.py`, and the maintained
`profile_gdb_samples.py`. Run the workspace preparation helper immediately
before every submission, as required by the benchmarking procedure.

After collecting the artifacts, audit and regenerate the compact tables:

```bash
python3 scripts/qualification/lra_ablation/summarize_diagnostics.py \
  results/lra-migration-ablation-20261010/diagnostics-20261011 \
  --plan scripts/qualification/lra_ablation/diagnostic-plan.json \
  --followup-plan scripts/qualification/lra_ablation/followup-plan.json \
  --sensitivity-plan scripts/qualification/lra_ablation/sensitivity-plan.json \
  --out results/lra-migration-ablation-20261010/diagnostic-audit.json \
  --tables docs/evidence/data/lra-regressions-20261011
```
