(arithmetic-overhead-experiment)=
# Removing repeated arithmetic work

**Status: interval cache accepted; constant if-then-else (CITE) fast paths
rejected after a confirmed paired runtime regression. Correctness and separate
SAT-model qualification are complete for both experiments.**
The baseline is `75e10877`. This batch changes work reuse, preserving arithmetic
options, the Boolean flattening threshold, search heuristics, candidate order
and preprocessing results. It makes no claim of closing the reference-solver gap.
The [ordered follow-ups](../leads/arithmetic-efficiency-followups.md) retain the
larger regrouping, flattening and numerical-representation investigations.

## Interval-cache mechanism

The owning map in `patch_integer_variables` reuses an exact `FreedomInterval`
for each moved nonbasic during one selection attempt. Any accepted move clears
all entries, including intervals of other variables. The
[implementation contract](../implementation/lia.md#patching-interval-reuse)
explains ownership, strict/unbounded endpoints and the regression that detects
stale intervals after a coupled move. This is a Primo adaptation of the
Bromberger §7.1.4 maintenance principle, not the thesis's full `VarInfo` scheme.

On the frozen `convert-jpg2gif-query-1577` diagnostic, all three repetitions
record exactly 2,236,456 interval requests, 139,987 computations and 2,096,469
hits under defaults. The same request positions were uncached in the baseline;
the equivalent uncached computation count is inferred from that unchanged
callback placement, rather than measured by a baseline counter that did not
exist. Reuse avoids **93.7%** of those interval computations. All original
patching scoring/rejection counters and successful logical decisions agree.

| Diagnostic profile | Baseline median, s | Cached median, s | Repetitions |
| --- | ---: | ---: | ---: |
| Defaults | 4.308 | 1.737 | 3 |
| M124 + uniform cubes | 3.969 | 1.617 | 3 |

The M124-plus-uniform run records 1,924,872 requests and the same 139,987
computations. Candidate generation and fractional-candidate rejection remain
in their original order. No early rejection or coefficient-index change is
included in this experiment.

## Main paired measurements

The 12 frozen diagnostic inputs use three rotated repetitions. The existing
200-input LIA development set and 519-input LRA competition selection use one
paired timing pass. The user requested **60-second limits**, replacing the
plan's mistaken 1,200-input/1,200-second LRA interpretation. M124 enables bound
rounding, primitive integer rows and integral patch moves, while disabling
integer-slack obligations. Uniform cubes are optional pure-QF_LIA controls;
QF_UFLIA ignores that switch.

| Set / profile | Baseline solved | Cached solved | Baseline PAR2, s | Cached PAR2, s | Paired geometric ratio (pairs) |
| --- | ---: | ---: | ---: | ---: | ---: |
| LIA / Defaults | 92/200 | 96/200 | 13463.14 | 12966.98 | 0.861 (53) |
| LIA / M124 | 126/200 | 127/200 | 9440.45 | 9220.29 | 0.672 (66) |
| LIA / Uniform cubes | 126/200 | 130/200 | 9378.07 | 8876.73 | 0.773 (55) |
| LIA / M124 + uniform | 150/200 | 151/200 | 6561.25 | 6341.75 | 0.671 (63) |
| LRA / defaults | 452/519 | 452/519 | 10969.22 | 10939.99 | 0.992 (381) |

A solved row requires successful `sat`/`unsat` termination within the limit.
Timeouts, errors and `unknown` are unsolved; each costs 120 seconds in PAR2.
The paired ratio is cached/baseline over common solves where both runtimes
are at least 50 ms. It does not substitute for solved counts or PAR2. Family
summaries, all option profiles, individual paired runtimes and mechanism
counters are retained in the machine-readable artifacts below.

The main LRA pass has one gained and one lost solve at the cutoff. Seven
rotated paired repetitions investigate three runtime outliers exceeding 15%
and 0.1 seconds. Their paired median cached/baseline ratios are 0.990, 1.019
and 1.005; each repeat series contains both faster and slower pairs. The large
initial slowdowns do not reproduce. Descriptive paired log-ratio intervals also
include one; these short repeat series do not establish a universal runtime
bound.

Three further paired repetitions investigate each LRA cutoff case. The initial
lost solve is UNSAT in both arms in all three repeats. The initial gained solve
times out in both arms in all three repeats. No stable LRA gain is credited.
The original main timing rows remain immutable. All 42 SAT timing rows in the
runtime follow-up also pass separate direct Dolmen checks; the cutoff follow-up
has no successful SAT rows. The cache is **accepted**, with no confirmed runtime
regression in these measurements. The {download}`acceptance record
<../../benchmarks/arithmetic-overhead-20261109/cache/acceptance.json>` retains
all paired values, decisions and the scope of that conclusion.

## Correctness and source review

Cache snapshot `cab1ebc4` passes SLURM `make check` (47 C++ groups and 312
Python tests) and ASan. New patching fixtures also pass when linked against
pristine baseline libraries, with only assertions on newly introduced counters
removed. They freeze assignments, accepted-move prefixes and original work
counters while independently checking distinct-variable interval computations.

The fresh generated gate performs **6,000 profile/input checks using 4,000
formulas**: 1,000 QF_LIA cases under defaults and M124 plus uniform cubes,
1,000 QF_UFLIA cases under defaults and M124, and 1,000 each for QF_LRA and
QF_UFLRA. The mixed-integer M124 command also passes the uniform switch, which
QF_UFLIA ignores. These generated controls differ from the M124-only timing
row. The integer profiles reuse matching
generated inputs. There are 5,954 agreements, 43 solver timeouts, two reference
timeouts and one integer-node-limit `unknown`. The reference-timeout input
under its two LIA profiles and the UFLIA node-limit unknown have extra
baseline/reference replays. The 43 native timeouts remain inconclusive;
none of these rows is counted as an agreement. Every one of
4,385 primary successful SAT answers has direct Dolmen acceptance. Twelve
additional model passes follow initially inconclusive answer runs.

The real-seed mutation stage checks 620 retained inputs across the six
profiles, using five small hashed real seeds per logic and the same generated-
gate profile arguments (including uniform cubes for QF_LIA M124). All 620 answer checks
pass, and their successful SAT models pass direct Dolmen. The initial UFLRA
attempt retained only 60 mutations; it is auxiliary evidence. A fresh 120-input
retry supplies that profile's required coverage.

All **1,121 successful native SAT timing rows** in the main comparison pass
separate direct Dolmen replay with the original profile. Model time never
enters the speed measurements. There are no contradictory successful decisions,
invalid models, solver crashes or unsupported timing outcomes in this data.
Matching completed runs preserve logical counters; Rational-operation work,
new mechanism counters and duration fields intentionally may differ.

The whole C++ scan covers 131 source/test translation units and four
manual examples, including `clang-analyzer` and advisory `misc-include-cleaner`.
Its 2,289 warning occurrences match the prior exact-C++ baseline: 2,288 owned
advisories and one external CLI11 advisory. There are no new warning identities;
this is not a warning-free baseline. Paired source/header self-include reports
were inspected rather than removed automatically. Formatting/lint, synthetic
analysis-provenance guard tests and the independent documentation review are
separate checks, not substitutes for solver qualification.

## CITE constant comparisons

**Rejected candidate.** The implementation and counters below describe
experimental snapshot `98a5fd8b`. Final production retains the interval cache
and the previous full-scan CITE classifier; the candidate and probes remain in
history for reproduction.

{citesource}`Snapshot 98a5fd8b's csimp_against_constant
<src/smt/preprocessing.cpp#L3949-L4031>` uses sorted endpoints for `<`/`<=` and singleton handling
plus binary-search membership for equality. It retains complete leaf sets,
operand orientation, exact rationals and mixed reconstruction. Its comparison
counters exclude leaf construction and cache-key comparisons. Balanced
{citesource}`8/64/512/4096-leaf probes
<tests/unit/preprocessing_tests.cpp#L2070-L2111>` require two uniform inequality
comparisons and a logarithmic equality-absence bound. The first new test oracle incorrectly
expected an existing unimplemented hoist; the repaired oracle preserves the
mixed scaled-CITE-inside-sum fallback and separately exercises pure scales and
offsets. The repaired semantic fixtures pass against the pristine baseline.

The NEC `prp-23-46` diagnostic records 11,274,943 leaf comparisons against
284,989,646 equivalent full-scan leaf visits in each of three default-profile
repetitions: **96.0% fewer comparisons**. There are 1,355,778 uniformity checks.
These are mechanism counts rather than a claim that all preprocessing work
falls by that proportion; complete leaf sets, sorting and cache-key comparisons
remain. Median default runtimes are 24.982 seconds for `75e10877`, 24.462 seconds
for the cache-only control, and 23.692 seconds for CITE. This modest wall-time
improvement is distinct from the larger interval-cache gain on `convert`.

The second experiment measures three immutable binaries: the original baseline,
cache-only control and cache-plus-CITE candidate. The following cells list them
in that order. Each LIA row has 200 inputs; LRA has 519. Ratios compare CITE
with the cache-only control over common solves with both runtimes at least
50 ms. These are new timing rows, separate from the first experiment.

| Set / profile | Solved: baseline / cache / CITE | PAR2, s: baseline / cache / CITE | CITE/cache geometric ratio (pairs) |
| --- | ---: | ---: | ---: |
| LIA / Defaults | 92 / 96 / 96 | 13465.72 / 12967.01 / 12966.83 | 1.001 (57) |
| LIA / M124 | 126 / 127 / 127 | 9441.84 / 9222.57 / 9222.37 | 1.004 (67) |
| LIA / Uniform cubes | 126 / 130 / 130 | 9383.57 / 8879.73 / 8876.62 | 0.999 (59) |
| LIA / M124 + uniform | 150 / 151 / 151 | 6562.36 / 6345.20 / 6343.61 | 1.003 (64) |
| LRA / Defaults | 451 / 453 / 451 | 10986.13 / 10852.95 / 10993.68 | 1.002 (380) |

The four LIA profiles retain the cache-only solved sets. LRA has two fewer
solves than the cache-only control and the same solved set as the original
baseline in this pass. Four distinct runtime cases exceed the investigation
threshold against the original baseline or cache-only control. Seven rotated
repetitions give CITE/cache median ratios of 1.009, 0.985, 1.004 and 0.980;
the original large slowdowns do not reproduce on those four cases. The 84 timing rows contain 42 SAT and 42 UNSAT answers; all 42
successful SAT timing rows in that series pass direct Dolmen replay.

The two cutoff cases receive three rotated repetitions. `NoriSharma` times
out in all arms throughout; `hongyi1` solves in 2/3 original-baseline, 3/3
cache-only and 1/3 CITE runs. Seven additional rotated 60-second trials still
leave most runs censored: solved counts are 1/7, 2/7 and 1/7 respectively.
All six successful SAT rows in the first cutoff series and all four in the
additional trials pass separate direct Dolmen checks. Printed SAT answers from
processes that time out remain unsolved.

To resolve the remaining runtime question, seven final three-arm trials use a
**90-second ceiling on this one Hongyi diagnostic only**. All 21 timing runs
finish SAT. CITE is slower than cache-only in all seven pairs: median ratio
**1.0153**, geometric ratio 1.0131 and descriptive paired log-ratio interval
[1.0100, 1.0162]. Against the original baseline, the median ratio is 1.0057.
The interval uses the same formula as the cache follow-up and is descriptive,
not a universal bound. Rotation balances the serial arm order; trial configs
and allocation provenance are retained. Separate direct Dolmen validation accepts all
21 SAT rows. The primary LRA selection and earlier cutoff rows
retain their original 60-second limits and remain immutable.

This confirmed slowdown fails the stated runtime acceptance rule. The CITE
source and added C++ tests are reverted from production, while its snapshots,
work counters, correctness results and all timing evidence remain reproducible.
The regression case records zero CITE classifications, so the new helper does
not execute there; the experiment does not establish the slowdown's cause.
No binary-layout explanation is claimed as a measured result.
Across all CITE supplementary series, 144 timing rows contain 73 successful
native SAT rows; all 73 pass direct Dolmen, for 1,756 accepted SAT model rows
including the main comparison. The {download}`rejection record
<../../benchmarks/arithmetic-overhead-20261109/cite/acceptance.json>` retains all
paired values and the decision.

The strict main audit covers **5,000 timing rows** and **1,683 successful native
SAT timing rows**, all separately accepted by direct Dolmen replay. Matching
completed runs preserve logical counters and successful answers. The candidate
passes SLURM `make check` (47 C++ groups, 312 Python tests) and ASan (47 groups
plus 200 generated profile/input checks, including four inconclusive timeouts).
Its fresh generated gate checks 6,000 profile/input pairs using 4,000 formulas:
5,963 agreements, 37 native timeouts, no reference timeouts or budget unknowns,
and all 4,385 primary SAT answers directly validated. Thirteen additional model
passes follow initially inconclusive answer runs. The 1,120 retained real-seed
mutations all agree with the reference; every successful SAT model passes.
The generated profiles and model-stage separation are the same as for the
cache gate, with fresh seeds.

The whole 135-unit C++ scan initially reports one new advisory in the added
oracle test, for a three-value vector grown without reserving capacity. A
test-only reserve repair passes focused Release/ASan preprocessing regressions
and the same static scan of that translation unit. Its normalized advisories
then match the cache baseline; no new advisory remains in the experimental
snapshot after its test-only repair. The measured workspace
retains the `98a5fd8b` production sources and binary. Final production is restored
to the accepted cache source. Existing baseline
advisories remain documented rather than described as a clean static baseline.

- {download}`CITE main summary <../../benchmarks/arithmetic-overhead-20261109/cite/summary.json>` and
  {download}`individual rows <../../benchmarks/arithmetic-overhead-20261109/cite/rows.csv.gz>`.
- {download}`CITE generated/mutation coverage <../../benchmarks/arithmetic-overhead-20261109/cite-gates.json>` and
  {download}`static comparison and focused repair <../../benchmarks/arithmetic-overhead-20261109/cite-static.json>`.

## Reproduction and provenance

All solver builds, tests, generated/mutation gates, timings and model replays
run on SLURM compute allocations. Timing shards request one CPU and 12 GiB;
three-repetition diagnostics rotate arm order by shard and repetition. Requested
resources are separate from Slurm's whole-task accounting and are not per-input
peak-memory measurements. Raw inputs, exact solver/model outputs and binaries
remain in the dedicated remote workspaces; local `results/arithmetic-overhead-20261109`
contains collected metadata. SSH routes, executable paths and corpus roots stay
in private configuration.

- {download}`Experiment and frozen input identities <../../benchmarks/arithmetic-overhead-20261109/experiment.json>`.
- {download}`Main cache summary <../../benchmarks/arithmetic-overhead-20261109/cache/summary.json>` and
  {download}`individual rows <../../benchmarks/arithmetic-overhead-20261109/cache/rows.csv.gz>`.
- {download}`Generated/mutation coverage <../../benchmarks/arithmetic-overhead-20261109/cache-gates.json>`;
  {download}`static comparison <../../benchmarks/arithmetic-overhead-20261109/cache-static.json>`.
- {download}`Timing/replay batch <../../scripts/qualification/arithmetic_overhead/measure.sbatch>`,
  {download}`generated gate <../../scripts/qualification/arithmetic_overhead/fuzz.sbatch>` and
  {download}`real-seed mutation gate <../../scripts/qualification/arithmetic_overhead/mutations.sbatch>`.
- {download}`Metadata preparation <../../scripts/qualification/arithmetic_overhead/prepare.py>` and
  {download}`strict result audit <../../scripts/qualification/arithmetic_overhead/analyze.py>`.

The canonical run-log ID is `2026-11-09-arithmetic-overhead`. It records job IDs,
commands, resource limits, source snapshots and failed setup attempts. A first
LRA generator stage lacked a seed directory; a subsequent FuzzSMT LRA model
stage rejected negative denominators at Dolmen's input probe. Neither supplies
accepted LRA model coverage; the fresh conforming generator replaces them.
The first timing layout used symlinks escaping its declared corpus root and
failed before solver execution. The accepted timing layout uses canonical
paths beneath their common ancestor and a fresh `timing-attempt2` artifact root.

Use the preparation helper with `--build-via-slurm --reuse-dependencies`, then
its `--check-only --no-sync` preflight immediately before every submission.
Validate each batch with `scripts/slurm_validate_sbatch.py`. Private preparation
JSON supplies workspace/binary paths, per-logic corpus roots, the frozen case
lists and mutation-seed manifests. `prepare.py` emits hashed per-suite configs;
`measure.sbatch` consumes them via `PRIMO_COMPARE_CONFIG`, `PRIMO_COMPARE_OUT`,
`PRIMO_CORPUS_ROOT` and `PRIMO_SHARDS`. Submit `PRIMO_OVERHEAD_PHASE=benchmark`
first and `replay` as a separate dependent stage after every timing array ends.
The audit rejects missing/changed identities, process failures, incomplete SAT
coverage, wrong direct-model exits and unexpected logical-counter changes.

For example, run metadata preparation from the qualified workspace, supplying
private paths and the frozen lists through `PRIMO_PRIVATE_CONFIG`:

```sh
python3 scripts/qualification/arithmetic_overhead/prepare.py \
  --config "$PRIMO_PRIVATE_CONFIG" --stage cache
python3 scripts/qualification/arithmetic_overhead/analyze.py \
  --root "$PRIMO_COLLECTED_TIMINGS" --suites "$PRIMO_SUITE_INDEX" \
  --configs "$PRIMO_COLLECTED_CONFIGS" \
  --out benchmarks/arithmetic-overhead-20261109/cache
python3 scripts/qualification/arithmetic_overhead/summarize_gates.py \
  --root "$PRIMO_COLLECTED_CACHE_GATES" --stage cache \
  --out benchmarks/arithmetic-overhead-20261109/cache-gates.json
```

The preparation JSON requires `workspace`, `baseline`, `candidate`, `z3`,
`roots`, `diagnostics`, `lia_selection`, `lra_manifest`, `uflia_manifest` and
`uflra_manifest`; optional `control` adds the interval-cache arm for the CITE
experiment. Copy `diagnostics` from the frozen diagnostic experiment's `cases`
and `lia_selection` from the LIA selection's `inputs`; text manifests become
lists of lines. The remaining path entries are private absolute paths; `roots` maps each
logic name to its private corpus directory. Collect the emitted
`tmp/overhead-cache-suites.json` byte-for-byte as `PRIMO_SUITE_INDEX`. Copy each
`tmp/overhead-cache-<suite>.json` byte-for-byte into `PRIMO_COLLECTED_CONFIGS`
under the required name `<suite>-private.json`; preserving the bytes preserves
the audited parent-configuration digest. The audit uses these collected names,
not the preparation filenames. For CITE, collect the corresponding
`overhead-cite-*` files and use `--stage cite` and the CITE root for gate summaries.
