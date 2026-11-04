# QF_LIA progress against reference solvers

**Status: contextual comparison, 2026-11-03.** All series cover the same
13,306 QF_LIA files at 60 s per file, but they come from runs on different
dates and nodes, so this is not a paired experiment. A {ref}`paired rerun
<qf-lia-paired-120s>` of the Primo configurations, further down, corrects two
conclusions drawn from it: the size of the gap to October, and the `nec-smt`
part of the integrality options' gain.

Since commit `5dec036` (2026-11-04) the QF_LIA default turns on all four
integrality options ({ref}`promotion <integrality-promotion>`). On this page,
the "default" series and rows of 2026-11 runs are the options-off
configuration. The new binary
reproduces that configuration with the four `--no-lia-*` flags. The "all four
integrality options" series now describes the QF_LIA default; a paired check
found the `5dec036` default identical to it in answers and search counters
on every file both solved within 10 s.

![QF_LIA at 60 seconds: Primo's August baseline, the options-off default and the integrality options, against z3, cvc5 and OpenSMT](../_static/qf-lia-cactus.svg)

In the figure legend, M1-M4 means all four integrality options, milestones M1
to M4 of the {ref}`integrality evaluation <integrality-evaluation>`.

| Series | Run | Solved | PAR2 (s) |
|---|---|---|---|
| z3 5.1.0 | 2026-09-07 | 12,144 | 174,586 |
| OpenSMT 2.9.2 | job 181259, 2026-10-09 | 10,654 | 361,027 |
| Primo `2e120fa`, all four integrality options | job 335585, 2026-11-02 | 9,798 | 459,979 |
| Primo `5efbd02` | 2026-10-05 | 9,705 | 470,785 |
| Primo `2e120fa`, default | job 335585 | 9,662 | 474,705 |
| cvc5 1.3.4 | job 127709, 2026-08-21 | 9,239 | 507,295 |
| Primo August baseline (before conflict generalization) | job 127709 | 8,703 | 574,137 |

A file counts as solved when the run exits normally with `sat` or `unsat`
within 60 s; PAR2 counts each unsolved file as 120 s. A clean `unknown` exit
counts as unsolved. Primo gives one, for example, when branch-and-bound
reaches its node limit: 569 files in the August run, 751 for the current
default and 450 with the options. OpenSMT ends in an error on 21 files. Every
series reports zero wrong answers against the declared statuses. The run-log
entry `2026-08-21-qf-lia-conflict-generalization` records the August run as
8,704 solved, one more than this counting rule gives. The figure omits the
`5efbd02` series to keep each curve a distinct colour.

## What the comparison shows

- **The default has improved a lot since August.** Primo solves 1,023 files
  that the August baseline did not and loses 64, a net of +959. PAR2 fell
  from 574,137 to 474,705 s. Most of the gain is `nec-smt`, from 608 to 1,236
  solved. Bromberger (+105), `convert` (+60), `slacks` (+55) and CAV 2009
  (+47) follow. The gap to z3 went from 3,441 to 2,482 files, and to OpenSMT
  from 1,951 to 992. Primo went from 536 files behind cvc5 to 423 ahead.
- **The integrality options add to that on QF_LIA.** In the first run, all
  four options solve 9,798, 136 more than the default in the same job: they
  gain 411 files and lose 275. The family churn is large in both directions,
  for example CAV 2009 +117/−59 and Bromberger +66/−79. Net gains come
  mostly from `nec-smt` (+83), CAV 2009 (+58), `dillig` (+38) and `rings`
  (+31). Net losses come from `slacks` and `prime-cone` (−17 each),
  `cut_lemmas` (−14) and Bromberger (−13).
- **Part of that gain does not reproduce.** In the 2026-11-03 rerun (job
  339382) the default solves 9,662 again (+5 −5), but the options solve
  9,745. Of the 60 files the options arm lost relative to its first run, 57
  are `nec-smt`. Counting only files that behave the same in both runs, the
  options gain 348 and lose 274, an estimated stable net of +74. The paired
  rerun measures +53, with `nec-smt` contributing +3. On QF_UFLIA the same
  options lose 31 files ([QF_UFLIA progress](qf-uflia-progress.md)). For
  that reason the 2026-11-04 promotion makes them the default for QF_LIA
  only.
- **The 43-file gap to October is not a code regression.** In these runs
  the default gains 3 files against `5efbd02` and loses 46, all of which had
  solved in 52.5 to 59.9 s in the October run; 38 of the 41 that also stay
  unsolved in the rerun are `nec-smt`. When both binaries run on the same
  node, the gap shrinks to 2 files: 9,698 against 9,696 solved at 60 s. It
  came from run conditions, which were not identified. Both unpaired
  November runs of the default were slower on `nec-smt` than the paired run,
  by a median of about 2.5% and by more on many of the files near the cutoff.
- **Remaining gap.** Against z3 the default is furthest behind on `nec-smt`
  (994 files), Bromberger (398), `rings` (255), CAV 2009 (239),
  `rings_preprocessed` (185), `convert` (139) and `slacks` (116).

## Paired pilot on a quiet host (2026-11-03)

The run-log entry `2026-11-03-calm-host-timing-pilot` records a small paired
run of both binaries, `5efbd02` and `2e120fa`, on the research host dai-03.
This host is outside SLURM, an AMD EPYC 7513 with frequency boost on, and
was idle at the start. Each run was pinned to its own physical core. The
subset was 292 files:

- 100 easy files both cluster runs solved in under 50 ms;
- 100 files solved in 1 to 45 s;
- the 46 files lost since October;
- 46 files both runs solved in 45 to 60 s.

**Startup.** Serially, on one core, each binary ran `--version` 50 times and
each easy file 5 times. The medians were 3.28 ms (`5efbd02`) and 3.35 ms
(`2e120fa`) for `--version`, and 4.51 and 4.68 ms for the easy files.
Repeated runs of one file varied by a median 1.2%. So the current binary is
about 4% slower on easy files, about 0.17 ms. On the cluster, the medians on
easy files were 8.2 ms (October) and 12.6 ms (November). That 4 ms gap is not
a property of the binaries.

**Solving.** Each (file, binary) pair ran three times, in shuffled order, on
16 cores. Another user's jobs started about 13 minutes in and oversubscribed
the host. Runs that started after that took a median 1.53 times as long as
the same file and binary before it, and up to 2.27 times. Only the 915 runs
that finished before that point are used below.

- **Repeatable.** Of the files with at least two clean runs per binary (157
  for `5efbd02`, 156 for `2e120fa`), none was solved in some runs and not
  others. For files taking at least 0.5 s, repeated times differed by a
  median 3% and at most 20%.
- **About 1% slower, mostly on short runs.** On the 229 files both binaries
  solved in all their clean runs, `2e120fa` took a median 1.006 times as long
  as `5efbd02` on this host (quartiles 0.979 and 1.043). By group: easy 1.022
  (83 files), 1 to 45 s 1.010 (77), near-cutoff 1.005 (37), and the files
  lost since October 0.996 (32).
- **Cutoff effects cannot be tested here.** This host ran the 1 to 45 s files
  in a median 0.6 times their cluster time. Files near the cluster's 60 s
  cutoff therefore finish well inside it here, and every clean run of the
  lost and near-cutoff files was solved by both binaries. Across all runs,
  including contaminated ones, each binary solved 45 of the 46 lost files at
  least once. The evidence on those files is the paired time ratio above, not
  the solved count.

This is a pilot, not an evaluation. It uses one host that is not a cluster
node and a subset of 292 files, and 837 of its 1,752 runs were discarded
for load. Its runner script is kept with the results on the host and is not
committed. It finds no code slowdown that would explain the 43-file October
gap, which is consistent with a node or date cause. It does not test the
cluster nodes; the paired rerun below does.

(qf-lia-paired-120s)=
## Paired rerun on the cluster at 120 s (2026-11-03)

The run-log entry `2026-11-03-paired-120s-rerun` records a rerun of three
configurations on all 13,306 files at 120 s per file: `5efbd02`, the
`2e120fa` default, and `2e120fa` with all four integrality options, each with
`--quiet --stats`. {settingssource}`slurm_paired_solver_compare.sbatch
<scripts/slurm_paired_solver_compare.sbatch>` ran all three in the same array
task, on the same node, in an order rotated per sub-shard (job 345183, 400
tasks on nodes 02 to 05). Each task requested two CPUs, so that a run would
not share a physical core with another job. The earlier runs differ:

- The October run of `5efbd02` used the evaluation-refresh timing job, one
  CPU per task with `--hint=nomultithread`, and `--quiet` without `--stats`.
- The two November runs of `2e120fa` (jobs 335585 and 339382) used one CPU
  per task without that hint and a separate task per configuration.

The 60 s columns count files solved within 60 s, which makes them comparable
to the table above.

| Configuration | Solved, 60 s | PAR2, 60 s | Solved, 120 s | PAR2, 120 s |
|---|---|---|---|---|
| Primo `5efbd02` | 9,698 | 471,377 | 10,588 | 768,626 |
| Primo `2e120fa`, default | 9,696 | 471,738 | 10,581 | 769,926 |
| Primo `2e120fa`, all four integrality options | 9,749 | 464,971 | 10,634 | 757,186 |

PAR2 counts each unsolved file as twice the limit: 120 s in the 60 s
columns, 240 s in the 120 s columns. No configuration gives a wrong answer
against the declared statuses, and no two configurations give different
`sat`/`unsat` answers on any file.

- **`5efbd02` and the options-off default run within 1% of each other.** At
  60 s the default gains 10 files against `5efbd02` and loses 12, all
  `nec-smt` but one; at 120 s it gains 15 and loses 22. On the 3,140 files
  both solve in at least 0.5 s, the default takes a median 1.007 times the
  time of `5efbd02` (quartiles 0.987 and 1.030), between 1.006 and 1.011 on
  each of the four nodes. That consistent slowdown of about 0.7% matches the
  quiet-host pilot and is too small to explain the 43-file gap. Of the 46
  files lost since October in the unpaired runs, `5efbd02` solves 26 and the
  default 24 at 60 s. Both solve all 46 at 120 s, at a median time ratio of
  1.003.
- **Per-arm speed in the unpaired runs varied by several percent.** Cut at 60
  s, the paired default solves 37 files its first run (job 335585) did not and
  loses 3; against the rerun (job 339382) it is +36 −2. 36 of the 37 are
  `nec-smt` files that the paired run solved in 53 to 60 s, and 32 of those 36
  were lost in both unpaired runs. On `nec-smt` files solved in both runs (at
  least 0.5 s), the paired default's times are a median 0.974 of the first
  run's and 0.975 of the rerun's, so the unpaired default was about 2.5%
  slower. Only 13 of the 36 files would need no more than that median slowdown
  to cross 60 s; the other 23 must have run at least 3 to 13% slower. The
  all-options arm of the same jobs went the other way: its paired times are a
  median 1.061 of the first run's and 1.007 of the rerun's, so in job 335585
  it ran about 6% faster than in the paired run. A slow default arm and a fast
  options arm in that job are consistent with the first run's `nec-smt` gain.
  The times of `5efbd02` in the paired run are a median 1.000 of its October
  run. The two runs that asked for a whole core, the October run and the
  paired run, agree. Because both arms of a November job had the same
  allocation, the allocation alone does not explain the opposite deviations.
  The cause was not identified.
- **The integrality options gain a net 53 files.** At 60 s they gain 336
  and lose 283 against the default. By family the net is CAV 2009 +58,
  `dillig` +38, `rings` +31 and `nec-smt` +3, against `slacks` and
  `prime-cone` −17 each, `cut_lemmas` −14, Bromberger −13 and
  `rings_preprocessed` −7. At 120 s it is +341 −288. The `nec-smt` net of
  +83 in the first run does not reappear; the other families match the
  stable estimate. On the 3,018 files both solve in at least 0.5 s, the
  options take a median 1.003 times the default's time.
- **Doubling the limit mostly helps `nec-smt`.** Each configuration solves
  885 to 890 more files at 120 s than at 60 s, 827 to 831 of them `nec-smt`.
  The default goes from 1,271 to 2,098 of the 2,780 `nec-smt` files. The
  reference solvers were run only at 60 s, so this says nothing about the
  gap to them at 120 s.

The same job design on QF_UFLIA is on the [QF_UFLIA
progress](qf-uflia-progress.md) page.

## Reproduction

The cactus plot is drawn by {source}`plot_cactus.py <scripts/plot_cactus.py>`:

```bash
python3 scripts/plot_cactus.py --timeout 60 --sort-by-solved \
  --title "QF_LIA, 13,306 files, 60 s per file" \
  --highlight-series "Primo 2e120fa default" \
  --series "z3 5.1.0=<hist>/z3" --series "cvc5 1.3.4=<hist>/cvc5" \
  --series "OpenSMT 2.9.2=<hist>/opensmt" \
  --series "Primo Aug 21 baseline=<127709>/primo" \
  --series "Primo 2e120fa default=<ab>/QF_LIA/off" \
  --series "Primo 2e120fa, M1-M4=<ab>/QF_LIA/all" \
  --note "Primo Aug 21 baseline and cvc5: SLURM job 127709 (2026-08-21); z3: 2026-09-07; OpenSMT: job 181259 (2026-10-09)." \
  --note "Primo 2e120fa: job 335585 (2026-11-02). Different dates and nodes: a contextual comparison, not a paired experiment." \
  --out docs/_static/qf-lia-cactus.svg
```

In the cluster workspace `primo-cadical-profile`, `<hist>` is
`results/qf-lia-opensmt-20261009/historical` (its `primo` arm is `5efbd02`)
and `<127709>` is `results/qf-lia-full-compare-127709`. `<ab>` is
`results/m5-ab-2e120fa` in `primo-lia-integrality`. The run-log entries
`2026-10-09-recovered-qf-lia-comparison-5efbd02`,
`2026-10-09-qf-lia-opensmt-full-division` and
`2026-11-02-lia-integrality-ab-2e120fa` hold the provenance. The rerun is
`results/m5-noise-2e120fa` in `primo-lia-integrality`, run-log entry
`2026-11-03-lia-integrality-noise-rerun-2e120fa`.

The paired rerun used these settings, in the cluster workspace
`primo-lia-integrality`:

```bash
B="build-slurm-rel/primo --quiet --stats"
O="/home/janotmik/primo-evaluation-20261005/build-slurm-rel/primo --quiet --stats"
export PRIMO_WORKDIR=$PWD PRIMO_COMPARE_SOLVERS="oct=$O;off=$B;all=$B \
  --lia-round-integer-bounds --lia-primitive-int-rows --lia-integer-slacks \
  --lia-patch-integral-moves"
sbatch --array=0-399%56 --cpus-per-task=2 --time=05:00:00 \
  --export=ALL,PRIMO_COMPARE_ROOT=tmp/QF_LIA,PRIMO_COMPARE_TIMEOUT=120,PRIMO_PAIRED_SHARDS=400,PRIMO_PAIRED_CHUNKS=4,PRIMO_COMPARE_OUT_ROOT=$PWD/results/paired-120s-20261103/QF_LIA \
  scripts/slurm_paired_solver_compare.sbatch
```
