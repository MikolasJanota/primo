# Preprocessing bounds with arithmetic propagation

## Question and current status

This follow-up tests whether unconditional preprocessing bounds help when
forward and reverse tableau-row propagation are enabled during search. The
[previous experiment](../leads/preprocessing-bound-tightening.md#qualification-is-blocked)
left both row-propagation directions off. Its small aggregate PAR2 difference
does not establish a speedup.

Qualification passed: 7,972 generated reference comparisons passed and 28 timed
out; all 6,021 selected generated SAT models passed direct Dolmen validation.
The supplemental 2,400 cases and tracked regression gates also passed. All
8,364 timing measurements are complete, and all 4,524 successful SAT rows
passed separate direct Dolmen validation. Adding preprocessing to row
propagation yields a modest, mixed benefit on the retained selection. The
three quarantined cases remain unqualified. Solver source and defaults are
unchanged.

The [selected behavior diagnosis](bound-behavior.md) subsequently reproduced
missed reverse scans, missing integer endpoint rounding, and a redundant bound
that triggers expensive UF fallback. It also isolated the LP work reduction
on a selected LIA success. Those findings leave the aggregate qualification
and opt-in decision below unchanged.

## Four profiles

All profiles use one fresh Release binary and enable the existing same-variable
arithmetic propagation. Integer branching used the private default at the
measured revision; the [later QF_LIA default profile](qf-lia-defaults-20261119.md)
is a separate change.

| Profile | Preprocessing bound worklist | Forward and reverse row propagation |
|---|---|---|
| `default` | Off | Off |
| `pre` | On | Off |
| `rows` | Off | On |
| `both` | On | On |

`rows` and `both` set `--lra-row-propagation` and
`--lra-bidirectional-row-propagation`; `pre` and `both` set
`--preprocess-bound-tightening`. Forward row-size/fanout caps stay at 64/64 and
reverse caps at 32/32, as defined in the
{boundpropsource}`option defaults <src/smt/check_sat.hpp>`. The preprocessing budget stays at 100,000 counted
operations and at most 4,096 emitted endpoints. There is no cap tuning.

The experiment covers QF_LIA, QF_UFLIA, QF_LRA and QF_UFLRA. Despite their CLI
prefix, these propagation options are also passed to the online integer
component's {boundpropsource}`ComponentLraExecution through LiaTheoryComponent::reset
<src/smt/solver_engines.cpp#L10514>`. They use current SAT-assigned bounds and
propagate consequences to existing registered atoms. They do not implement
unrestricted closure over every newly inferred numeric endpoint. See the
[propagation implementation](../implementation/lra.md#propagate-and-explain-consequences)
and [preprocessing worklist](../leads/preprocessing-bound-tightening.md).

## Selection and execution

The input selection is frozen before measurement: the previous 700-input
manifest minus exactly three previously reproduced memory-exhaustion cleanup
aborts. This leaves 200 LIA, 300 UFLIA, 100 LRA and 97 UFLRA instances.
The excluded inputs and their hashes remain in the new manifest's quarantine
list. They are not passes, and this restricted study cannot qualify the whole
solver or establish behavior on those inputs.

Each of the 697 retained inputs gets all four profiles and three fresh
repetitions: 8,364 native measurements. The profiles run sequentially within
each allocation; order rotates by input index and repetition. Repetition
arrays run sequentially to avoid simultaneous repetitions of the same case.
Each native solve has a 120-second deadline and an 8 GiB address-space limit,
with one requested CPU and 10 GiB allocation memory. All solver work uses
SLURM. Previous timing rows are not reused.

Provenance spans three compute hosts, each reporting two sockets, 14 cores per
socket and two threads per core. Affinity masks contain two logical CPU IDs
despite the one-CPU request. The CPU model and physical-core isolation were
not established; the paired profiles share each case's allocation.

Before broad timing, the two new propagation profiles receive 1,000 fresh
generated cases per arithmetic logic, comparison with Z3, and separate direct
Dolmen validation of successful generated SAT results. The pure-LRA generator
specifically covers SAT-by-construction Boolean/arithmetic chains and fans;
UNSAT coverage is supplied by relevant unit and regression cases. Tracked arithmetic
seeds are checked with complete answer sequences, including scoped sessions.
The canonical UFLRA gate adds mixed and finite-domain correctness coverage;
its finite-domain profile checks answers without claiming AMO activation.
Relevant arithmetic unit groups include the existing forward/reverse
propagation regressions. One lexical seed uses cvc5 because Z3 rejects its
quoted `|let|` application before returning a partial-input SAT result; that
unchanged original also passes direct Dolmen validation. New crashes, wrong answers, invalid models or
untriaged disagreements stop broad measurement.

After all timing arrays finished, every successful native SAT timing row was
replayed with its exact profile and independently checked with Dolmen. Model
validation time is excluded from runtime and PAR2. Repetition and profile
identities remain distinct. Replay uses a 180-second native budget, a 60-second
Dolmen budget and Dolmen's `--size=1G` setting within a 10 GiB allocation. The
replay stage does not impose the timing stage's separate 8 GiB address-space
limit. These correctness resources do not change the recorded timing results.
Direct passes cover 1,128 `default`, 1,128 `pre`, 1,131 `rows` and 1,137 `both`
timing rows. Model validation does not certify UNSAT answers; reference
comparisons and known benchmark-answer metadata provide separate checks.

## Interpretation

Report solved counts, all unsolved outcome classes, PAR2, runtime variation
across the three repetitions, and memory by logic and family. PAR2 assigns
240 seconds to each unsolved run and measured runtime to each successful solve.
The interaction comparison is `rows` versus `both`: does preprocessing help when
row propagation is already active? Also compare `default` versus `rows` and
`pre` versus `both` to measure propagation's contribution.

Mechanism evidence includes same-variable/row propagations, pivots, conflicts,
SAT decisions, private integer branch nodes and preprocessing counters. Missing
statistics on interrupted runs must be counted explicitly. Row propagations
are included in total arithmetic propagations, and reverse-row propagations
are included in row propagations; these overlapping counters must not be summed.
Emitted-bound counts
alone do not show that those bounds survived later preprocessing or caused a
particular propagation; the current counters do not tag that full chain.

The analysis audit checks each repetition against the frozen selection,
profile arguments, binary hash and timing limits. It also checks each selected
SAT replay's input path, completed metadata and effective solver arguments.
Missing replay coverage leaves validation pending. A signal crash stops the
audit even if its diagnostic mentions allocation failure; handled exit-code-2
memory exhaustion remains an unsolved resource result.

## Repeated timing results

Each solved-count triple follows repetitions 0, 1 and 2. PAR2 is the median
of the three per-repetition means; parentheses show their observed minimum
and maximum, not a confidence interval. Every successful SAT timing row has
passed the separate model-validation stage.

| Profile | Solved out of 697 | Mean PAR2, seconds per input |
|---|---|---|
| `default` | 539 / 539 / 539 | 58.402 (58.380–58.408) |
| `pre` | 539 / 539 / 539 | 58.174 (58.171–58.174) |
| `rows` | 540 / 540 / 539 | 58.582 (58.582–58.725) |
| `both` | 542 / 542 / 541 | 57.833 (57.828–57.983) |

| Logic and retained inputs | `default` | `pre` | `rows` | `both` |
|---|---|---|---|---|
| QF_LIA, 200 | 104 / 104 / 104 | 104 / 104 / 104 | 104 / 104 / 104 | 104 / 104 / 104 |
| QF_UFLIA, 300 | 259 / 259 / 259 | 257 / 257 / 257 | 259 / 259 / 258 | 259 / 259 / 258 |
| QF_LRA, 100 | 85 / 85 / 85 | 87 / 87 / 87 | 86 / 86 / 86 | 88 / 88 / 88 |
| QF_UFLRA, 97 | 91 / 91 / 91 | 91 / 91 / 91 | 91 / 91 / 91 | 91 / 91 / 91 |

Adding preprocessing to `rows` improves mean PAR2 by 0.749, 0.754 and 0.742
seconds per input. It consistently gains three LRA instances and loses one:
`miplib/danoint-166.smt2` and the two `latendresse` glycerol cases ending in
`Yices3-5000.smt2` and `Yices3-50000.smt2` are gained;
`miplib/danoint-266.smt2` is lost. The other logics' solved sets do not change
in this comparison.

Compared with `default`, `both` consistently gains five instances and loses
two. Two gains and both losses are UFLIA, leaving its solved count unchanged
in the first two repetitions. The third repetition additionally times out on
`wisas/xs_21_41.smt2` under both row-enabled profiles; those runs had completed
in roughly 109–117 seconds in the first two repetitions. The stable UFLIA
losses are the Certora input beginning `65782_cd31513f` and
`wisas/xs_23_43.smt2`. Equal counts therefore do not mean equal solved sets.

Large changes repeat within the common solved set. For example, adding
preprocessing to `rows` reduces LIA `30_30_18_12_sat.smt2` from 81.6–83.0
seconds to 7.0–7.1 seconds. UFLIA `mathsat/Wisa/xs-20-19-2-2-2-5.smt2`
increases from 0.36–0.40 seconds to 31.4–33.5 seconds. LRA
`TM/p-3-bucket_s10.smt2` increases from 13.1–13.8 seconds to 34.7–36.3 seconds.
These changes deserve separate diagnosis before tuning or default promotion.

For `rows` versus `both`, 538 inputs solve in both profiles in all three
repetitions. Their geometric mean of per-case paired median runtime ratios
(`both / rows`) is 1.016; the median is 1.003. On the 229 of those inputs whose
`rows` median runtime is at least one second, the geometric mean is 0.880.
These equally weighted ratios measure a different quantity from PAR2, which
also penalizes unsolved inputs. Against `default`, the 536 commonly solved
inputs have a geometric mean ratio of 1.219 and a median ratio of 1.040 for
`both / default`. The lower aggregate PAR2 therefore does not establish a
general runtime improvement on commonly solved inputs.

All profiles return 63 QF_LIA private-branch-budget `unknown` results per
repetition. QF_LIA retains the same 96 unsolved inputs in every profile: their
PAR2 penalty alone contributes 115.2 seconds per input to its mean. Changes
to successful runtimes affect only the remaining contribution, so substantial
individual improvements can coexist with a small aggregate PAR2 change.
Across all
8,364 timing rows there are 1,077 timeouts and 53 handled memory-exhaustion
results; there are no new signal crashes or other unclassified failures in
this retained selection. Depending on profile and repetition, 61–70 rows
lack printed statistics, mostly interruptions. Native peak RSS maxima are
approximately 7.40–7.41 GiB for all four profiles; no memory improvement is
established.

## Mechanism evidence and decision

Row propagation delivers consequences in all four arithmetic logics. The
`rows` profile records positive delivery counts on 59–60 LIA cases, 262–264
UFLIA cases, 70–72 LRA cases and 72 UFLRA cases per repetition. These counts
include interrupted or inconclusive runs that printed statistics; they are
not solved counts.

The following counters compare `rows` with `both` on the same inputs solved
by both profiles in all three repetitions. Counts sum all three repetitions
and use only completed runs with printed statistics. Each arrow is
`rows` to `both`.

| Logic | Common inputs | Pivots | Private integer branch nodes |
|---|---:|---:|---:|
| QF_LIA | 104 | 2,804,475 → 1,747,794 | 871,191 → 871,191 |
| QF_UFLIA | 258 | 7,131,243 → 7,154,217 | 1,800,477 → 1,200,576 |
| QF_LRA | 85 | 3,200,850 → 3,200,643 | 0 → 0 |
| QF_UFLRA | 91 | 92,469 → 92,448 | 0 → 0 |

Across these 538 common inputs, preprocessing is associated with 7.8% fewer
pivots and 22.5% fewer private branch nodes, while SAT decisions increase
2.3%. Recorded row propagations fall from 32,952,765 to 31,800,624; their
reverse-direction subset rises from 4,316,685 to 4,452,990. More delivered
propagations are not required for a faster solve. These aggregate counters
do not identify which emitted endpoint caused a particular search change.
For the 91 common UFLRA successes, preprocessing emits only 13 endpoints per
repetition and leaves SAT decision counts unchanged.

Keep both options opt-in. The combination's lower PAR2 is repeatable on this
restricted selection, by 0.68–0.99% against `default`, with three more LRA
successes and material UFLIA trades. Row propagation alone worsens aggregate
PAR2 in every repetition. The study supports a modest, mixed benefit from
combining the settings on the retained selection; it does not
establish a general integer-solving improvement or qualify the quarantined
inputs. No default promotion, cap tuning or unrestricted internal numeric
bound closure is included.

## Sources and implementation status

This experiment exercises existing interval rules and their documented
implementation limits. It adds no new arithmetic algorithm. The committed
{boundpropsource}`protocol <benchmarks/bound-propagation-20261113/protocol.json>`,
{boundpropsource}`selection <benchmarks/bound-propagation-20261113/selection.json>`,
{boundpropsource}`seed hashes <benchmarks/bound-propagation-20261113/regression-seeds.json>`
and {boundpropsource}`qualification <benchmarks/bound-propagation-20261113/qualification.json>`
pin this experiment. The {boundpropsource}`configuration generator
<scripts/qualification/bound_propagation/configure.py>` and
{boundpropsource}`measurement launcher <scripts/qualification/bound_propagation/paired.sbatch>`
provide the reproducible workflow; the {boundpropevidence}`run ledger <docs/run-log.json>`
records execution and provenance.

The completed {boundpropevidence}`evaluation
<benchmarks/bound-propagation-20261113/evaluation.json>`,
{boundpropevidence}`measurement CSV
<benchmarks/bound-propagation-20261113/measurements.csv>` and
{boundpropevidence}`execution record
<benchmarks/bound-propagation-20261113/execution.json>` include per-family
results, missing-statistics counts, selected-model coverage hashes and tool
identities. The {boundpropevidence}`accounting
<benchmarks/bound-propagation-20261113/accounting.psv>` verifies all six
completed arrays, sequential timing repetitions, and model validation
beginning after timing finished. An initial scheduling attempt was
cancelled and rejected; only the restarted arrays appear in these results.
The frozen protocol and qualification record retain their premeasurement
status; the evaluation and execution records describe the completed study.

The {boundpropevidence}`analysis driver
<scripts/qualification/bound_propagation/analyze.py>` validates configuration,
receipt and replay identities before computing the summaries. Current
verification comprises the fresh Release build, three arithmetic CTest groups,
the recorded generated/seed/scoped gates, seven focused metadata tests and
`make lint`. Full `make check`, ASan and clang-tidy were not rerun for this
experiment because all 201 tracked production sources are unchanged. Earlier
full-suite qualification remains separate evidence. Documentation build,
link, rendering and independent review checks accompany this report.
