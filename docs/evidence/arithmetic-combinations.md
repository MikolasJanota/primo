# Arithmetic technique combinations

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
remain separate from agreement passes. Full timings are pending. An initial attempt at `ebe8d4b2` was stopped before any
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
coverage and propagation-counter partitions. It emits `analysis.json` and
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
8 GiB limits, rotates pair direction, and binds the derived manifest to the
parent analysis and original configuration hashes. Its manifest also records
every derived configuration hash; final auditing rejects any changed options
before reading results. Follow-up timings start
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
