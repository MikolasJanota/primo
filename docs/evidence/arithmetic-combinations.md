# Arithmetic technique combinations

## Measured results

The main matrix is complete: 14,364 fresh timings and all **7,931 selected SAT
rows directly accepted by Dolmen**. Hybrid branching accounts for the large
integer coverage gain. Enabling every technique is not the best integer
configuration in this selection. **342 pairs flagged by the predeclared regression criteria** received
three additional counterbalanced repetitions and separate model checks.
All 2,052 extra timings and 1,516 selected SAT model checks are complete. Defaults remain unchanged.

P denotes preprocessing bounds, R forward/reverse row propagation, and H
hybrid integer branching. B disables these three switches while retaining
existing same-variable propagation. Tables give solved-count ranges over
three repetitions and the median of their **mean PAR2 seconds**. Each
unsolved row costs 240 seconds. The integer pool has 500 inputs; the real pool
has 197. H is inactive for reals, so no duplicate real/H measurements exist.

| Profile | QF_LIA solved / 200 | PAR2 s | QF_UFLIA solved / 300 | PAR2 s | integers solved / 500 | PAR2 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| B | 104 | 120.635 | 259 | 35.373 | 363 | 69.478 |
| P | 104 | 119.965 | 257 | 36.754 | 361 | 70.040 |
| R | 102–103 | 121.151 | 260 | 34.908 | 362–363 | 69.410 |
| PR | 102–103 | 121.510 | 260 | 34.838 | 362–363 | 69.507 |
| H | 134 | 85.351 | 260 | 34.463 | 394 | 54.813 |
| PH | 134 | 84.610 | 258 | 35.920 | 392 | 55.396 |
| RH | 134–135 | 83.082 | 261 | 34.067 | 395–396 | 53.680 |
| PRH | 133–134 | 85.109 | 260 | 34.665 | 393–394 | 54.842 |

| Profile | QF_LRA solved / 100 | PAR2 s | QF_UFLRA solved / 97 | PAR2 s | reals solved / 197 | PAR2 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| B | 85 | 41.120 | 91 | 18.970 | 176 | 30.213 |
| P | 87 | 36.791 | 91 | 18.973 | 178 | 28.017 |
| R | 85 | 41.468 | 91 | 18.634 | 176 | 30.225 |
| PR | 88 | 35.924 | 91 | 18.626 | 179 | 27.415 |

H alone consistently solves 394 integer inputs versus B's 363; its median
mean PAR2 improves from 69.478 to 54.813 seconds (21.1%). RH has the lowest
integer median mean PAR2, 53.680 seconds (22.7% below B), but its solved count
varies between 395 and 396. PRH solves 393–394 and scores 54.842 seconds.
For real arithmetic, PR solves 179 versus B's 176 and reduces median mean
PAR2 from 30.213 to 27.415 seconds (9.3%). These are results on the frozen
selection, not a default-promotion decision or a competition-wide ranking.

```{figure} ../../benchmarks/arithmetic-combinations-20261115/full/cactus.png
:name: arithmetic-combinations-cactus

Cases solved in every repetition, sorted by median time. Curve counts can be
lower than a table's per-repetition counts. Solid lines use private branching;
dashed lines use H. SAT rows receive separate model validation.
```

The {download}`complete analysis <../../benchmarks/arithmetic-combinations-20261115/full/analysis.json>`,
{download}`measurement CSV <../../benchmarks/arithmetic-combinations-20261115/full/measurements.csv>`
and {download}`selected-work diagnostic <../../benchmarks/arithmetic-combinations-20261115/full/mechanisms.json>`
retain per-family outcomes, gains/losses, memory, counters and receipt hashes.
The main report is frozen as the parent of the
{download}`regression manifest <../../benchmarks/arithmetic-combinations-20261115/followup-manifest.json>`.

The descriptive oracle union solves **401 integer inputs in each repetition**,
compared with RH's 396/396/395. Persistent residual slots are 2, 85, 95, 229
and 460; slot 17 is an additional RH residual in the third repetition.
The real union solves **180**, compared with PR's 179: slot 597 is solved by
B/P. The {download}`compact coverage inventory <../../benchmarks/arithmetic-combinations-20261115/full/coverage-union.json>`
identifies files and solving profiles for every residual. This is an upper
bound from choosing a successful profile per case; it does not price combined
execution, implement a selector, or change any fixed-profile score.

## What the combinations change

On QF_LIA, B→H has 32 stable gains and two stable losses: net +30 in every
repetition. Of those gains, 29 had returned the private-node-limit unknown
under B and three had timed out. H answers 28 SAT and four UNSAT on them.
The SAT witnesses passed Dolmen; UNSAT is checked against available expected
answers and differential gates, not certified by model checking. QF_UFLIA
adds one stable B→H gain with no stable loss.

Preprocessing does useful numerical work even when its overall PAR2 change
looks small. On the 104 QF_LIA cases solved by both B and P in all repetitions,
private nodes are exactly unchanged: 863,574 in each arm across the 312 paired
measurements. Pivots fall from 3,730,785 to 2,299,911 (38.4%). The conditional
time ratio is 0.798 over the 49 cases whose B median is at least one second.
The global solved count remains 104; unsolved penalties dominate whole-suite
PAR2. This is an observed reduction in arithmetic work, without a claim that
pivot counts alone explain all CPU time.

That benefit largely overlaps R on this LIA sample: adding P to R gives a
conditional ratio of 0.994 over 44 cases, and does not change private-node
counts. The matched four-corner P×R interaction ratios are 1.285 with H off
(47 eligible cases) and 1.298 with H on (45). Values above one mean the
relative benefit is weaker in combination; they do not imply that the
combined configuration is slower than B.

QF_UFLIA shows a different interaction. P alone cuts matched private nodes
from 2,101,161 to 1,200,807 across the common completed triples. Adding P to H
instead increases them from 15,030 to 25,449, with private-node and split-request
changes on eight cases. Its conditional H→PH time ratio is 1.000, while P
loses two solved inputs. The prepared problem and completion path change;
these counters do not identify a single cause such as the effective N budget.
Adding R to H has ratio 1.157 on 65 common slow cases, despite a net coverage
gain. Full gains and losses therefore matter alongside runtime ratios.

In QF_LRA, P adds two stable solutions. PR gains four over B and loses one,
net +3. R's conditional B→R ratio is 1.415 on 33 common slow cases; its gain
and loss occur outside that conditional cohort. QF_UFLRA keeps the same 91
solutions in every profile. Small aggregate differences there warrant caution.

Every eligible within-profile triple has identical values for the five
selected search counters across repetitions. This rules out changes in those
recorded counts within those triples; it does not establish identical traces
or attribute timing variation to a particular machine effect.

```{figure} ../../benchmarks/arithmetic-combinations-20261115/full/interactions.png
:name: arithmetic-combinations-interactions

Four-corner interaction ratios, using a shared horizontal scale. Each square
forms its ratio within a repetition before aggregation and uses its own joint
eligible cohort. Do not reconstruct these values by dividing aggregate edge
ratios with different cohorts. Real-arithmetic values near one are shown on
the same scale as the integer results.
```

The 14,364 timing outcomes are 7,931 SAT, 3,267 UNSAT, 762 normal private-node
limit unknowns, 2,333 other timeouts, 67 handled memory-exhaustion errors and
four timeouts coincident with memory exhaustion. All unsolved classes remain
in PAR2. There are no observed wrong answers, crashes or unexplained handled
errors. The largest reported generated-split-atom counter on a successful run is
74. The largest printed interrupted snapshot is 2,226, from a timeout;
interrupted or unprinted statistics do not establish a global peak. Maximum observed RSS reaches about
7.4 GiB in LIA and 7.36 GiB in UFLRA, against an 8 GiB address-space cap.

The primary private/H pairs are adjacent. P/R comparisons can cross groups,
and initial circular group rotations do not balance every pair's first
position. The additional flagged-pair runs counterbalance the original three;
small timing effects and descriptive interaction ratios do not establish
statistical significance. The observed five compute nodes share the reported
x86-64 topology: two sockets, 14 cores per socket, two hardware threads per
core, and 257,525 MB SLURM-reported memory. Allocation affinity masks have two
logical CPUs; physical-core isolation has not been established.

## Regression repeats and decision

The {download}`qualified repeat report <../../benchmarks/arithmetic-combinations-20261115/followups.json>`
covers 342 technique-addition/input pairs on **112 distinct inputs**: 315
runtime flags and 27 stable-loss flags. These are pair/context counts, not
342 distinct problems. All six main arrays and six follow-up arrays have finished; every selected SAT row in both studies passed Dolmen
(7,931 main + 1,516 repeats). No wrong answer, crash or invalid model was
observed.

| Logic | Runtime pairs flagged | Repeated ≥10% and ≥0.5s | Stable-loss pairs flagged | Repeated loss in all extra runs |
| --- | ---: | ---: | ---: | ---: |
| QF_LIA | 41 | 40 | 13 | 10 |
| QF_UFLIA | 208 | 206 | 12 | 12 |
| QF_LRA | 54 | 54 | 2 | 2 |
| QF_UFLRA | 12 | 12 | 0 | 0 |
| Total | 315 | 312 | 27 | 24 |

All 315 runtime pairs remain at least 10% slower in every extra repetition.
The original one-second median rule selected eligible main-run pairs; it is
not reapplied to this repeat-effect column. Reapplying it would give 311,
because UFLIA slot 420 B→P has repeat medians 0.194525/0.998366 seconds.
The three that miss the two effect thresholds still regress relatively; an
absolute delta in one or more runs falls just below 0.5 seconds. They are
LIA slot 72 PH→PRH and UFLIA slots 240/489 P→PR. The three stable-loss flags
that do not repeat in all extra runs concern LIA slot 2: B→R loses in two
of three, P→PR in one, and PH→PRH in two. H→RH on that input still loses in
all three. This is cutoff sensitivity, not disappearance of every loss.
Original main scores remain unchanged by the diagnostic repeats.

The coverage gain from H is substantial and repeated. R adds selected
solutions but also many repeated slowdowns; preprocessing has useful
numerical and real-arithmetic effects while often overlapping other work.
The evidence supports retaining the existing opt-in techniques and their
explicit tradeoffs. It does not support enabling every option by default,
promoting a universally best stack, or claiming that small differences between
nearby aggregate scores are statistically resolved. No solver default or
automatic profile-selection policy changes in this study.

## Question and fixed scope

Do preprocessing bounds (P), tableau-row propagation (R), and hybrid integer
branching (H) complement or obstruct each other? This study evaluates existing
options; it introduces no inference rule or default change. It follows the
[qualified row-work filters](propagation-redundancy.md). Earlier four-profile
measurements and the [hybrid study](lia-hybrid-20261111.md) motivate the matrix,
but their timings are not combined into a new speed claim.

The {download}`frozen protocol <../../benchmarks/arithmetic-combinations-20261115/protocol.json>`
and {arithmeticcombsource}`profile builder <scripts/qualification/arithmetic_combinations/profiles.py>`
define eight configurations for QF_LIA/QF_UFLIA:
`B`, `P`, `R`, `PR`, `H`, `PH`, `RH`, `PRH`. `B` means all three experimental
switches are off; existing same-variable propagation remains enabled.
QF_LRA/QF_UFLRA use only `B`, `P`, `R`, `PR`; duplicating them under an inert
integer policy would not be a distinct configuration.

| Factor | Off | On |
| --- | --- | --- |
| P | No preprocessing bound worklist | `--preprocess-bound-tightening` |
| R | Row propagation off | Both forward and reverse row propagation, including current scan/queue optimizations |
| H | Private integer branching | `--lia-branching=hybrid`, factor one, unlimited fresh split atoms |

The {arithmeticcombsource}`CLI owners <src/main.cpp>` and
{arithmeticcombsource}`solver option defaults <src/smt/check_sat.hpp>`
define these switches. Integer private-node limits remain 100,000 per completion. In hybrid mode the
{arithmeticcombsource}`proportional rule <src/theory/arithmetic/lia_completion.cpp>` still bounds each private attempt by the smaller of that
limit and factor × original-integer-term count. Preprocessing can change the
prepared problem and thus policy inputs: keeping the factor fixed does not
assert identical effective private budgets across configurations.
Integer rounding, patching, GCD/cuts/cube settings, row caps, initial/model
phase selection, UF equality producers/delivery and fallback policies retain
the same logic-specific defaults in every configuration. No cap tuning,
private-factor tuning or equality-sharing factor is part of this matrix.

## Execution and correctness gate

The measured source and profile/harness revision are frozen at `4f02a8b7`,
with production C++ unchanged from the qualified `c815a0b4`. One fresh Release
build supplies every configuration; its binary SHA256 is
`08948789d787e2a81e76ad671738f29ac192dde34b4c9276789dff55b03a44ff`. Builds and all native
solver work run in SLURM through the preparation helper. Before broad timings,
each of the 24 logic/configuration pairs receives 1,000 fresh generated inputs
against Z3 with model checks, exact-option tracked regression sessions, and
real-seed mutation coverage. Every successful native SAT result from these gates receives a direct Dolmen
check, including original mutation seeds and reference-timeout cases. A missing
check leaves qualification incomplete. Wrong answers, invalid models, crashes and
untriaged disagreements stop the campaign. Resource-only incomplete results
are preserved and classified separately.

The fixed selection is the same 697 inputs: 200 QF_LIA, 300 QF_UFLIA,
100 QF_LRA and 97 QF_UFLRA. Its three predeclared abort exclusions remain
excluded, not passes; there are no new removals. Three fresh repetitions
produce 14,364 native measurements: 500 integer inputs × 8 profiles × 3,
plus 197 real inputs × 4 profiles × 3. No earlier timing row is reused.
Each has a 120-second deadline and an 8 GiB address-space cap, within one CPU
and a 10 GiB SLURM allocation. Repetition arrays run sequentially. Integer
profiles are grouped into adjacent private/hybrid pairs for each P/R setting;
group order and pair direction rotate by case and repetition. Real-profile
order rotates too. Physical-core isolation is not assumed.

Every successful SAT timing row receives a separate exact-option model replay
and direct Dolmen check after all timing arrays finish. Native replay and
checker limits are 180 and 60 seconds. Validation time never enters timing or
PAR2. Late answers followed by timeout/error remain unsolved.

## Analysis and interpretation

Report all profiles by logic and family: solved counts, per-repetition
variation, PAR2, all unsolved outcome classes, memory, and gained/lost cases.
PAR2 charges each unsolved measurement 240 seconds, including normal resource
unknowns. Report the mean PAR2 for each repetition and its median across the
three repetitions; these are means, not summed scores. Inspect the best fixed profile and which additional cases are solved
by other profiles; an oracle union is only a descriptive upper bound, not an
implemented selector or a default policy.

Compare adding each technique with the corresponding configuration that lacks
only that technique. Examine P×R with H off and on, P×H with R off and on,
and R×H with P off and on. For common cases successfully solved by all four
corners in every repetition, compute the paired interaction ratio, for example
`(T(PR)/T(R)) / (T(P)/T(B))`. Compute this ratio within each paired repetition,
take the median ratio over three repetitions for each case, then the geometric
mean over eligible cases. Require the lower corner's median time to be at
least one second: the lower corner is `B` when the third factor is off, or that
third factor alone when it is on (for example `H` for P×R with H on).
Values below one mean preprocessing has a larger relative runtime benefit
when row propagation is present in this square.
This descriptive ratio is not a significance test and excludes the unsolved
cases where H may provide its largest benefit. Always present coverage/PAR2
alongside conditional runtime interactions.

Mechanism evidence includes emitted preprocessing endpoints, row deliveries
and computations, suppressed queue duplicates and repeated-state skips,
pivots, SAT conflicts/decisions, private integer nodes, split requests,
unique/reused split atoms, SAT resumes, phase times and peak generated atoms.
Counter availability and interrupted statistics must be explicit. Counts from
overlapping mechanisms are not summed as independent events.

Consistent paired regressions (at least 10% and half a second in all three
repetitions, with either median at least one second) and stable solve losses
receive additional paired runs. Keep smaller differences visible. Retention
or further policy recommendations require full model coverage and an explicit
account of regressions; no default promotion is implied by a winning sample.

## Current status

The corrected-adapter build passed 49 native test groups and 357 Python tests.
The {download}`qualification audit <../../benchmarks/arithmetic-combinations-20261115/qualification.json>`
covers all 24 logic/profile pairs: 23,877 generated agreement passes and 123
timeouts from 24,000 cases, plus 7,795 mutation agreement passes and 125
resource-incomplete results from 7,920 mutants: 120 native timeouts, four
private-node-limit unknowns (confirmed in separate SLURM replays), and one
reference timeout whose native SAT model passed Dolmen. All 292 tracked-regression
measurements passed. Direct Dolmen checks accepted all 22,961 native SAT gate
results, including the original mutation seeds. Resource-incomplete results
remain separate from agreement passes. The full timing/model audit is complete; the main results are above. All flagged-pair follow-ups and their model checks are complete. An initial attempt at `ebe8d4b2` was stopped before any
timing: fuzz/seed adapters classified `--stats` keys containing “unsupported” as
an unsupported result. A separate SLURM probe instead observed `unsat`, exit zero,
no diagnostic and only `:combination.model.fallback-unsupported 0`. The
{arithmeticcombsource}`adapter correction <scripts/fuzz_external.py>` excludes
colon-prefixed statistic lines from this test and preserves nonzero-exit
rejection after SAT/UNSAT. Three mocked protocol tests cover all four adapters;
all logic/profile gates were rerun, without reusing rejected classifications. The {arithmeticcombsource}`configuration generator <scripts/qualification/arithmetic_combinations/configure.py>`
creates private corpus-root/binary configs and integer/real cohorts. Run
{arithmeticcombsource}`qualify.py <scripts/qualification/arithmetic_combinations/qualify.py>`
with `generated`, `seeds` and `mutations` for every slot 0–23 through
{arithmeticcombsource}`qualify.sbatch <scripts/qualification/arithmetic_combinations/qualify.sbatch>`.
Generated and mutation seeds are their respective protocol bases plus slot;
task order is fixed by `profiles.tasks()`. The protocol pins generator options,
limits, reference command and regression-manifest hash.

Only after receipt audits may
{arithmeticcombsource}`run.sbatch <scripts/qualification/arithmetic_combinations/run.sbatch>`
run three sequential timing arrays, then separate model arrays, through the
{arithmeticcombsource}`cohort router <scripts/qualification/arithmetic_combinations/run.py>`
and existing pinned timing/model driver. Binary hashes, actual private config
hashes and submission receipts must be recorded before execution; no pending
identity is accepted as completed provenance.


## Reproducing the analysis

The reporting tool is separate from the frozen native build. Run
{arithmeticreporttools}`analyze.py <scripts/qualification/arithmetic_combinations/analyze.py>` with `--configs`
pointing to the private timing-config directory, `--rows` to its collected
`full` directory, `--protocol` and `--selection` to the tracked manifests, and
`--out` to a new evidence directory. The default audit requires every timing
allocation and every selected SAT model. `--allow-pending-models` permits an
explicitly provisional timing report; `--partial` only screens durable rows
and produces no performance summary. A live collection can omit a diagnostic
file until the next transfer, so the partial audit defers that diagnostic;
the final audit requires it. The initial timing attempt was canceled after
this collection race and preserved separately. All repetitions restarted;
none of its partial measurements enter the final comparison.

The report checks native/configuration/input/profile/order identities, process
status before answers, expected and cross-profile answers, complete model
coverage and propagation-counter partitions. Handled errors coincident with
the deadline still require the known memory-exhaustion diagnostic; an
unexplained handled error stops the audit. It emits `analysis.json` and
`measurements.csv`, with one row per actual native measurement. Counter totals
use successful rows with statistics; interrupted missing counters remain
unavailable. Paired mechanism comparisons use only common solved cases in all
three repetitions and repetition pairs where both arms have statistics. They
report the available and excluded pair counts; no available pair yields
`null`, not zero, for both totals. Peak generated atoms also receive a maximum, separate from sums.
The companion {arithmeticreporttools}`plot.py <scripts/qualification/arithmetic_combinations/plot.py>`
(`--evidence OUTPUT`) requires qualified model coverage
and uses optional Matplotlib to render cactus and interaction figures. Cactus
curves use cases solved in all three repetitions and their median times.


Normal resource-limit returns receive a separate table of explicitly printed
counter snapshots. This exposes private-node-limit work even where completion
returns `unknown`, without counting it as a successful solve or extrapolating
the unfinished search. CSV counter cells likewise retain explicitly printed
snapshots, including interrupted runs; an unprinted interrupted counter stays
blank. These snapshots do not enter the successful matched-counter totals.

For every predeclared stable loss or consistent runtime regression, the
{arithmeticreporttools}`follow-up controller <scripts/qualification/arithmetic_combinations/followups.py>`
prepares three additional adjacent paired repetitions of that single technique
addition. It retains the two original profiles, input hash and 120-second/
8 GiB limits, counterbalances each pair against its actual original order,
and binds the derived manifest to the
parent analysis and original configuration hashes. Its manifest also records
every derived configuration hash; final auditing rejects any changed options
before reading results. Each side runs first three times across the six
original/additional repetitions. Some original cross-group pairs ran in one
relative order in all three main repetitions, so their extra runs use the
opposite order throughout; alternation is used when compatible with balance.
Follow-up timings start
after all main timing/model jobs finish, with their own model phase after their
last timing repetition. Every selected SAT result again requires Dolmen.
The follow-up report says whether the slowdown or solve loss reproduced;
these diagnostic measurements never replace the original factorial scores.
The {arithmeticreporttools}`shared submission controller <scripts/qualification/bound_propagation/submit.py>`
option `--task-count` limits these arrays to the
actual number of flagged pairs; its default remains 697.


The {arithmeticreporttools}`selected-work diagnostic <scripts/qualification/arithmetic_combinations/mechanisms.py>`
reads only a qualified measurement CSV. It compares SAT decisions, SAT
conflicts, pivots, private nodes and split requests. Within each profile it
reports cases whose selected counters vary across repetitions; across a
one-switch pair it reports cases with equal or changed selected counters.
Its cohort requires successful solves in all three repetitions and available
values for all five counters. Missing data and coverage differences remain
outside this comparison. Equal selected counters do not prove identical
search traces, and changed counters do not by themselves attribute CPU time.
The resulting `mechanisms.json` pins the measurement CSV hash and native
identity, and does not alter timing scores.
