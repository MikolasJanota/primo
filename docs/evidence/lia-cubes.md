(lia-cube-evidence)=
# QF_LIA cube-test evaluation

**Status: experimental implementation qualified; development and full 13,306-input
comparisons complete, including every native SAT model. Cubes remain off by default.**
Source `84654ade`; implementation `65b6f48a`; workflows and frozen development
evidence `96355a2a`. The [implementation](../implementation/lia.md#optional-cube-model-attempt)
describes the sufficient model test from Bjørner–Nachmanson, CAV 2024, §4.2.
QF_UFLIA keeps its existing policy.

## Full-corpus result

The full run compares M124 with cubes off, uniform cubes, difference-aware cubes,
and fresh Z3 on the same frozen 13,306-file corpus. M124 enables integer-bound
rounding, primitive integer rows and integral patch moves, with integer-slack
obligations disabled. Current-native arms all use
one `84654ade` binary. Previous-build and default-setting controls remain
development-only evidence. The full set includes the development subset.

Uniform cubes add **578 net solves** over M124 with cubes off and reduce PAR2
by **15.47%**.

All **20,942 successful native SAT timing rows** passed separate direct Dolmen
validation: 6,595 cube-off, 7,174 uniform and 7,173 difference. All 3,000 native
profile/shard coverage records are complete, with no missing row, invalid model,
changed answer or validation timeout. Every native-profile/family SAT count in the
summary therefore also gives its validated coverage. No wrong flag, rejected
candidate or cross-profile/reference disagreement was found.

| Profile | Solved | SAT | UNSAT | Clean unknown | Timeout | PAR2 (s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| M124, cubes off | 9888 | 6595 | 3293 | 401 | 3017 | 449341.76 |
| M124 + uniform | 10466 | 7174 | 3292 | 260 | 2580 | 379842.28 |
| M124 + difference | 10469 | 7173 | 3296 | 261 | 2576 | 379668.80 |
| Z3 5.1.0 | 12158 | 7836 | 4322 | 0 | 1148 | 172991.40 |

```{figure} ../_static/qf-lia-cubes-full60.svg
:alt: Full QF LIA cactus comparing M124 off, uniform, difference and Z3

All 13,306 inputs, without easy-input filtering. Times below 0.01 seconds share
the left edge. The two cube curves almost coincide. Model checking runs outside
these timing measurements.
```

Difference adds 581 net solves and reduces PAR2 by 15.51%. On common solves taking at least 50 ms in both
arms, runtime geometric means are 0.9617 uniform/off (4,089 pairs) and 0.9613
difference/off (4,087 pairs).

Both modes find cube models on **exactly the same 842 inputs**, all successful
SAT timing answers. Of these, **581 were unsolved with cubes off**. Those direct
model gains concentrate in four families:

| Family | Inputs | Cubes off | Uniform | Difference | Z3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `20180326-Bromberger` | 806 | 348 | 595 | 595 | 728 |
| `CAV_2009_benchmarks` | 591 | 424 | 591 | 591 | 590 |
| `slacks` | 233 | 122 | 221 | 221 | 232 |
| `dillig` | 233 | 165 | 233 | 233 | 232 |

The Bromberger family includes transformed CAV 2009 inputs; these counts cover
multiple encodings as well as different instances. Other families show little
solved-count change. In particular, solving the `dillig` benchmark family does
not mean implementing Dillig's proof-derived splitting algorithm.

### Gains, losses and the two modes

Uniform has 590 paired gains and 12 losses; difference has 595 gains and 14
losses. Exactly 581 gains in either arm carry a cube-model counter. Uniform
recovers 140 clean node-limit unknowns and 445 timed-out SAT cases in total;
difference recovers 140 and 447. These totals also include ordinary search
crossing the timeout boundary. The remaining five/eight gains respectively
are UNSAT timing changes; a failed cube attempt contributes no new refutation.

Every lost solve becomes a timeout. Cube-off runtimes on these inputs range
from 56.68–59.97 seconds for uniform losses and 55.60–59.97 seconds for difference
losses. This single run does not separate scheduling variation from feature
overhead near the deadline. All losses remain in PAR2 and the public paired lists.

Difference versus uniform has 15 gains and 12 losses, but **no extra cube-model
input**. Difference mode exempts 391,796 inequalities in the reported counters.
Its three-solve advantage therefore gives no evidence that the refinement
improves model coverage on this corpus. Uniform obtains all observed direct
cube gains.

### Full-run mechanism counters

| Reported counter | Uniform | Difference |
| --- | ---: | ---: |
| Calls | 12659 | 12402 |
| Skips | 489 | 489 |
| Models | 842 | 842 |
| Strengthened LP infeasible | 11321 | 11064 |
| Local budget exhausted | 7 | 7 |
| Scratch pivots | 257042 | 256744 |
| Helper time (s) | 41.61 | 41.44 |

No rejected candidate is reported. Branch-node totals are 49.09 million with
cubes off, 34.81 million with uniform and 34.65 million with difference. All
these totals are lower bounds: counters are published when completion returns,
so timeouts can omit earlier cube work. Helper time excludes snapshot construction and does not measure the full feature cost.

### Remaining reference gap

Uniform solves 1,692 fewer inputs than Z3 in net count. Z3 alone solves 1,884
inputs: 1,158 UNSAT and 726 SAT. Uniform alone solves 192: 128 UNSAT and 64 SAT.
The largest family gaps by net solved count are:

| Family | Uniform | Z3 | Net Z3 advantage |
| --- | ---: | ---: | ---: |
| `nec-smt` | 1286 | 2241 | 955 |
| `rings` | 62 | 282 | 220 |
| `rings_preprocessed` | 69 | 258 | 189 |
| `convert` | 180 | 318 | 138 |
| `20180326-Bromberger` | 595 | 728 | 133 |

Cubes supply a useful model-finding step. Most remaining Z3-only decisions are
UNSAT, which this sufficient feasibility test cannot establish. Stronger
integer inference and the existing deferred equality-basis/splitting leads
remain separate work.

### Execution and decision

Timing job `363536` and dependent model job `363697` use 1,000 shards with up to
160 concurrent tasks. Each requests one CPU and 12 GiB. Timing uses a 60-second
per-input timeout and a 75-minute task wall limit. Model replay runs afterwards
with separate 90-second solver and 30-second Dolmen budgets, a two-hour task
wall limit and an 8 GiB Dolmen size setting. Validation time is excluded from
PAR2. Each arm uses one static profile for every input.

All paired tasks report Intel Xeon E5-2690 v4 at 2.60 GHz. Slurm reports maximum
batch-step RSS of `9717264K` (about 9.27 GiB) for timing and `4861608K` (about
4.64 GiB) for model validation. These are whole-task accounting peaks across
profiles, not per-instance or cgroup-total measurements. All 1,000 timing and
1,000 model batch steps completed with exit status zero.

The implementation remains experimental and off by default. The full comparison
covers the M124 configurations; default-setting cubes have the separate
200-input development evidence below. General octagons, mixed cubes and integer
equality-basis elimination remain unimplemented.

## The development result

On the targeted 200-input set, either cube mode adds 24 SAT answers to M124,
with no lost answer. All 544 successful native SAT timing rows passed separate,
direct Dolmen validation: 437 current-build rows and 107 previous-build control
rows. This is development evidence, not the full-corpus result.

| Profile | Solved | SAT | UNSAT | Clean unknown | Timeout | PAR2 (s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Default, cubes off | 92 | 46 | 46 | 62 | 46 | 13449.36 |
| Default + uniform | 126 | 80 | 46 | 38 | 36 | 9365.63 |
| Default + difference | 126 | 80 | 46 | 38 | 36 | 9366.99 |
| M124, cubes off | 126 | 61 | 65 | 33 | 41 | 9426.27 |
| M124 + uniform | 150 | 85 | 65 | 19 | 31 | 6546.84 |
| M124 + difference | 150 | 85 | 65 | 19 | 31 | 6546.05 |
| Z3 5.1.0 | 199 | 102 | 97 | 0 | 1 | 212.55 |

Uniform and difference-aware cubes solve identical sets in both configurations.
The measured M124 PAR2 reduction is about 30.5%; the small runtime difference
between the cube modes does not establish a winner. Default-setting cubes add
34 SAT answers, also with no loss. Cube M124 solves a superset of the cube-default
set, adding 24 answers, so the full campaign focuses on M124.

Of M124's 24 gains, 14 were clean integer branch-node-limit unknowns and 10 were
timeouts with cubes off. Under default settings, 24 gains were clean unknowns
and 10 were timeouts. These node-limit gains show an improvement in finding
integer models within the existing search budget. A failed cube attempt leaves
the original search state untouched; it supplies no integer refutation.
The native UNSAT solved counts are unchanged in this development comparison.

The previous build, `aeaf2823`, and current cube-off control solve the same sets:
92 under default settings and 126 under M124. Every preexisting non-time counter matches
on their clean, matching-answer rows (new `lia.cube-*` and `lia.branch-nodes`
instrumentation is excluded): 154 default and 159 M124. Their common
runtime geometric means, restricted to pairs taking at least 50 ms each, are
1.0034 and 1.0046 current/previous respectively. This checks preservation on
this subset; it is not a full-corpus equivalence claim.

## Input and measurement scope

The manifest has 50 historically reference-fast/native-slow SAT inputs, 50
analogous UNSAT inputs, 50 clean-unknown node-limit candidates and 50 easy
controls. Buckets are disjoint, selection is deterministic by family round-robin,
no refill was needed, and 30 families occur. Selection uses historical rows;
every reported timing arm was rerun. The full corpus contains 13,306 inputs
across 32 families. SLURM job `363278` verified every relative path and content
hash against the frozen inventory: 8,192,190,039 bytes in total.

The timeout is 60 seconds. PAR2 charges 120 seconds to every unsolved input,
including clean unknowns. Timing job `363336` used 100 paired shards, one CPU
and 12 GiB requested per task, rotating profile order on the same node.
Model job `363436` ran afterwards with independent 90-second solver and
30-second Dolmen budgets. Validation time is excluded from the timing table.
References and binary hashes are frozen in the experiment metadata. Failed
processes and post-answer errors cannot count as solved.

M124 enables integer-bound rounding, primitive integer rows and integral patch
moves, with integer-slack obligations disabled. Its flags are
`--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks
--lia-patch-integral-moves`. The QF_LIA default enables rounding, primitive rows,
integral patch moves and integer-slack obligations.
Each cube profile uses `--lia-cube=uniform` or `--lia-cube=difference`, with
`--lia-cube-steps=1000`. All other completion budgets retain their source defaults.

## What the development counters show

M124 reports 344 cube calls: 11 skip after patching has made the model integral,
36 find and adopt a validated model, 294 find the strengthened LP infeasible,
and three exhaust the local budget. Uniform reports 10,503 scratch pivots;
difference reports 10,530. Difference mode exempts 16,454 selected inequalities
without adding a solved input on this subset.

These are **lower bounds over returned completions**. Counters are published
when the whole completion returns, so an unfinished cut/branch tail can hide an
earlier cube attempt. The recorded helper times, about 0.61 seconds total per
M124 cube arm, also exclude SMT snapshot construction. They do not bound the
feature's full cost. The full-corpus timings above measure the net benefit;
the feature remains experimental and off by default.

## Correctness qualification

The current source passed 47 CTest groups and 311 Python tests through SLURM
`make check`. ASan passed all 47 groups; its 200 generated answer checks have
196 agreements and four timeouts. The FuzzSMT campaign reuses the same three seeds across four profiles:
3,000 generated inputs and 12,000 profile/input answer checks. Those checks
have 11,840 agreements and 160 timeouts, with no wrong answer or crash. Every one of the
10,544 successful primary SAT answers has direct Dolmen acceptance; 36 initial
timeouts additionally yielded validated SAT models on replay. Remaining replay
timeouts are not model passes.

Four profiles—uniform/difference under default/M124—each ran 200 real-seed
OpFuzz mutations from ten families. All 800 mutations and 56 original seed/
regression checks agree with Z3; all 666 selected SAT rows pass Dolmen.
Generated inputs, seeds, reference identities, budgets and job IDs are retained
in the qualification metadata and run ledger.

Whole-tree analyzer/include review covered 135 non-generated C++ translation
units. After direct-header cleanup, it reports 2,289 warnings of 310 unique
message/check kinds, versus the previous qualified baseline's 2,215/308.
The two new unique kinds are enum-width advisories. Existing optional-access
warning kinds in the new tests refer to installed bounds and a valid literal;
exceptions intentionally fail those fixtures. No new algorithmic analyzer
finding was found. This is not a warning-free baseline.

## Reproduction artifacts

- {download}`Development experiment and exact arguments <../../benchmarks/lia-cubes-20261108/experiment.json>`;
  {download}`selected paths/hashes and recipe <../../benchmarks/lia-cubes-20261108/selection.json>`.
- {download}`Development summary <../../benchmarks/lia-cubes-20261108/development/summary.json>`;
  {download}`audited per-input timing/counter rows <../../benchmarks/lia-cubes-20261108/development/rows.csv>`.
- {download}`Full summary <../../benchmarks/lia-cubes-20261108/full60/summary.json>`;
  {download}`full per-input rows (gzip) <../../benchmarks/lia-cubes-20261108/full60/rows.csv.gz>`.
- {download}`Correctness qualification <../../benchmarks/lia-cubes-20261108/qualification.json>`;
  {download}`full-corpus experiment <../../benchmarks/lia-cubes-20261108/full-experiment.json>`.

The canonical ledger entry is `2026-11-08-lia-cube-implementation` in
`docs/run-log.json`. Raw timing and model artifacts remain under
`results/lia-cubes-20261108`; the public rows omit private environment paths.

Audit the completed full timing and model stages with:

```sh
python3 scripts/analyze_lia_cubes.py \
  --root results/lia-cubes-20261108/full60 \
  --experiment benchmarks/lia-cubes-20261108/full-experiment.json \
  --selection benchmarks/lia-cubes-20261108/inventory.tsv \
  --out benchmarks/lia-cubes-20261108/full60 --require-models --gzip-rows
```

For resource reporting, collect the following metadata on the cluster and copy
`accounting.psv` beside the local full-run artifacts:

```sh
sacct -j 363536,363697 -P \
  --format=JobID,State,ExitCode,ElapsedRaw,MaxRSS > accounting.psv
```

Add `--accounting results/lia-cubes-20261108/full60/accounting.psv` to the analyzer
command. It joins the paired-task CPU/request metadata and reports Slurm's
batch-step elapsed times and maximum reported RSS. These cover whole tasks
across profiles; they are not per-instance memory or cgroup-total measurements.
The analyzer checks the replay's Dolmen identity against the frozen experiment
and publishes validator configurations separately from speed metrics.

The analyzer also compares uniform with difference mode and each native arm
with Z3. Gains and losses are relative to the left-hand profile named in each
comparison key; common-runtime ratios are right over left. The cactus reader
accepts each profile's `shard-*/answers.csv` layout directly, in addition to its
older flat CSV layouts. For example, after qualification:

```sh
python3 scripts/plot_cactus.py \
  --series 'M124 off=results/lia-cubes-20261108/full60/off-m124' \
  --series 'M124 uniform=results/lia-cubes-20261108/full60/uniform-m124' \
  --series 'M124 difference=results/lia-cubes-20261108/full60/difference-m124' \
  --series 'Z3=results/lia-cubes-20261108/full60/z3' \
  --timeout 60 --title 'QF_LIA: optional cube tests' --sort-by-solved \
  --highlight-series 'M124 uniform' --public \
  --out docs/_static/qf-lia-cubes-full60.svg
```
