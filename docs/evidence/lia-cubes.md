(lia-cube-evidence)=
# QF_LIA cube-test evaluation

**Status: experimental implementation qualified; development comparison complete;
full 13,306-input timing/model campaign running. Cubes remain off by default.**
Source `84654ade`; implementation `65b6f48a`; workflows and frozen development
evidence `96355a2a`. The [implementation](../implementation/lia.md#optional-cube-model-attempt)
describes the sufficient model test from Bjørner–Nachmanson, CAV 2024, §4.2.
QF_UFLIA keeps its existing policy.

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
--lia-patch-integral-moves`. The QF_LIA default enables all four switches.
Each cube profile uses `--lia-cube=uniform` or `--lia-cube=difference`, with
`--lia-cube-steps=1000`. All other completion budgets retain their source defaults.

## What the counters show

M124 reports 344 cube calls: 11 skip after patching has made the model integral,
36 find and adopt a validated model, 294 find the strengthened LP infeasible,
and three exhaust the local budget. Uniform reports 10,503 scratch pivots;
difference reports 10,530. Difference mode exempts 16,454 selected inequalities
without adding a solved input on this subset.

These are **lower bounds over returned completions**. Counters are published
when the whole completion returns, so an unfinished cut/branch tail can hide an
earlier cube attempt. The recorded helper times, about 0.61 seconds total per
M124 cube arm, also exclude SMT snapshot construction. They do not bound the
feature's full cost. Full-corpus timings are needed before judging the overall
benefit or promoting a default.

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

## Full-corpus campaign

The full run compares M124 with cubes off, uniform cubes, difference-aware cubes,
and fresh Z3, using the same frozen 13,306-file corpus. Current-native arms all
use one `84654ade` binary. The previous-build and default-setting controls remain
development-only evidence. The full set includes the development subset; it is
not a disjoint holdout. One static profile is used for every input in an arm.

Timing job `363536` and dependent model job `363697` use 1,000 shards with up to
160 concurrent tasks. Each requests one CPU and 12 GiB. Every successful native
SAT row must pass a separate model replay. Results, memory peaks, paired gains/
losses, families and the reference comparison remain pending.

## Reproduction artifacts

- {download}`Development experiment and exact arguments <../../benchmarks/lia-cubes-20261108/experiment.json>`;
  {download}`selected paths/hashes and recipe <../../benchmarks/lia-cubes-20261108/selection.json>`.
- {download}`Development summary <../../benchmarks/lia-cubes-20261108/development/summary.json>`;
  {download}`audited per-input timing/counter rows <../../benchmarks/lia-cubes-20261108/development/rows.csv>`.
- {download}`Correctness qualification <../../benchmarks/lia-cubes-20261108/qualification.json>`;
  {download}`full-corpus experiment <../../benchmarks/lia-cubes-20261108/full-experiment.json>`.

The canonical ledger entry is `2026-11-08-lia-cube-implementation` in
`docs/run-log.json`. Raw timing and model artifacts remain under
`results/lia-cubes-20261108`; the public rows omit private environment paths.

After the full timing and model stages finish, audit them with:

```sh
python3 scripts/analyze_lia_cubes.py \
  --root results/lia-cubes-20261108/full60 \
  --experiment benchmarks/lia-cubes-20261108/full-experiment.json \
  --selection benchmarks/lia-cubes-20261108/inventory.tsv \
  --out benchmarks/lia-cubes-20261108/full60 --require-models --gzip-rows
```

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
