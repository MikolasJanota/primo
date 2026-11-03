# QF_UFLIA progress against reference solvers

**Status: contextual comparison, 2026-11-03.** All series cover the same 659
QF_UFLIA files at 60 s per file, but they come from runs on different dates
and nodes, so this is not a paired experiment. A {ref}`paired rerun
<qf-uflia-paired-120s>` below reproduces the solved sets of both `2e120fa`
arms.
The QF_LIA counterpart is [QF_LIA progress](qf-lia-progress.md).

![QF_UFLIA at 60 seconds: Primo's September baseline, the current default and the integrality options, against z3, cvc5 and OpenSMT](../_static/qf-uflia-cactus.svg)

| Series | Run | Solved | Certora (76) | TwoSquares (21) | mathsat (454) | wisas (108) | PAR2 (s) |
|---|---|---|---|---|---|---|---|
| z3 5.1.0 | job 131212, 2026-09-03 | 618 | 35 | 21 | 454 | 108 | 5,490 |
| cvc5 1.3.4 | job 131212 | 605 | 25 | 21 | 454 | 105 | 7,379 |
| OpenSMT 2.9.2 | job 131212 | 601 | 18 | 21 | 454 | 108 | 8,049 |
| Primo `2e120fa`, default | job 335586, 2026-11-02 | 575 | 6 | 21 | 441 | 107 | 11,375 |
| Primo `2e120fa`, all four integrality options | job 335586 | 544 | 5 | 21 | 441 | 77 | 14,934 |
| Primo `61cd9ee` | job 131212 | 543 | 3 | 21 | 437 | 82 | 15,758 |

In the figure legend, M1-M4 means all four integrality options, milestones
M1 to M4 of the {ref}`integrality evaluation <integrality-evaluation>`.
PAR2 counts each unsolved file as 120 s. Every series reports zero wrong
answers against the declared statuses.

## What the comparison shows

- **The default has improved.** Since the first full baseline at `61cd9ee`,
  Primo solves 32 more files and loses none. The gains are 25 `wisas`, 4
  `mathsat` and 3 Certora files. PAR2 fell from 15,758 to 11,375 s. The gap
  to OpenSMT shrank from 58 to 26 files, and the gap to z3 from 75 to 43.
- **The integrality options lose many of those files.** With
  `--lia-round-integer-bounds --lia-primitive-int-rows --lia-integer-slacks
  --lia-patch-integral-moves`, Primo solves 31 fewer files than the default:
  30 `wisas` and 1 Certora. That leaves it only one file above the September
  baseline. The `wisas` loss is a search-order effect of the primitive-row
  option, traced in the {ref}`integrality evaluation
  <integrality-evaluation>`. This is why the options are not the default.
- **The results are reproducible.** A rerun of both Primo `2e120fa` arms on
  2026-11-03 (job 339383) produced exactly the same solved sets, and so did
  the paired rerun below, cut at 60 s.
- **Remaining gap.** Certora is where Primo trails most: 6 of 76 against
  z3's 35. On `mathsat`, every reference solves all 454 files; Primo solves
  441.

(qf-uflia-paired-120s)=
## Paired rerun at 120 s (2026-11-03)

The run-log entry `2026-11-03-paired-120s-rerun` records a rerun of
`5efbd02`, the `2e120fa` default and `2e120fa` with all four integrality
options, at 120 s per file. `5efbd02` is the October binary compared on the
{ref}`QF_LIA page <qf-lia-paired-120s>`. Its October run on QF_UFLIA (job
167599, not in the table above) solved 574 files at 60 s, one fewer than
here.
All three configurations ran in the same array
task, on the same node, with two CPUs per task (job 345182, 40 tasks, 30 of
them on node 03). The design is described on the {ref}`QF_LIA page
<qf-lia-paired-120s>`.

| Configuration | Solved, 60 s | PAR2, 60 s | Solved, 120 s | PAR2, 120 s |
|---|---|---|---|---|
| Primo `5efbd02` | 575 | 11,343 | 577 | 21,118 |
| Primo `2e120fa`, default | 575 | 11,347 | 577 | 21,122 |
| Primo `2e120fa`, all four integrality options | 544 | 14,938 | 548 | 28,150 |

PAR2 counts each unsolved file as twice the limit. There are no wrong
answers and no disagreements between configurations; every unsolved run is a
timeout.

- **`5efbd02` and the current default solve the same files** at both limits.
  On the 194 files both solve in at least 0.5 s, the default takes a median
  1.002 times the time of `5efbd02`.
- **The options lose 31 files at 60 s and 29 at 120 s**: the same 30
  `wisas` and 1 Certora file at 60 s. At 120 s, 2 of those `wisas` files
  solve in 74 and 84 s, and the options also solve the two Certora files the
  default gains, in 104 and 111 s. On the 145 files both solve in at least
  0.5 s, the options take a median 1.045 times the default's time.
- **Doubling the limit adds little.** The default and `5efbd02` each gain
  two Certora files, solved in 74 to 101 s.

## Reproduction

The cactus plot is drawn by {source}`plot_cactus.py <scripts/plot_cactus.py>`:

```bash
python3 scripts/plot_cactus.py --timeout 60 --sort-by-solved \
  --title "QF_UFLIA, 659 files, 60 s per file" \
  --highlight-series "Primo 2e120fa default" \
  --series "z3 5.1.0=<131212>/z3" --series "cvc5 1.3.4=<131212>/cvc5" \
  --series "OpenSMT 2.9.2=<131212>/opensmt" \
  --series "Primo 61cd9ee (Sep 3)=<131212>/primo" \
  --series "Primo 2e120fa default=<ab>/QF_UFLIA/off" \
  --series "Primo 2e120fa, M1-M4=<ab>/QF_UFLIA/all" \
  --note "References and Primo 61cd9ee: SLURM job 131212 (2026-09-03). Primo 2e120fa: job 335586 (2026-11-02); rerun 339383 reproduced both counts." \
  --note "Different dates and nodes, same 659 files and 60 s limit: a contextual comparison, not a paired experiment." \
  --out docs/_static/qf-uflia-cactus.svg
```

Here `<131212>` is `results/qf-uflia-full-131212` in the cluster workspace
`primo-qflia-fuzz-gate`, and `<ab>` is `results/m5-ab-2e120fa` in
`primo-lia-integrality`. The run-log entries
`2026-09-03-qf-uflia-full-smtlib-61cd9ee` and
`2026-11-02-lia-integrality-ab-2e120fa` hold the provenance. The paired
rerun used the QF_LIA command on the {ref}`QF_LIA page <qf-lia-paired-120s>`
with `--array=0-39%12 --time=04:00:00`, `PRIMO_COMPARE_ROOT=tmp/QF_UFLIA`,
`PRIMO_PAIRED_SHARDS=40` and the output directory
`results/paired-120s-20261103/QF_UFLIA`.
