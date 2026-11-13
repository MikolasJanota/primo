# Preprocessing bounds with arithmetic propagation

## Question and current status

This follow-up tests whether unconditional preprocessing bounds help when
forward and reverse tableau-row propagation are enabled during search. The
[previous experiment](../leads/preprocessing-bound-tightening.md#qualification-is-blocked)
left both row-propagation directions off. Its small aggregate PAR2 difference
does not establish a speedup.

Qualification passed: 7,972 generated reference comparisons passed and 28 timed
out; all 6,021 selected generated SAT models passed direct Dolmen validation.
The supplemental 2,400 cases and tracked regression gates also passed. Fresh
timing repetitions are running; no performance acceptance or default promotion
is claimed here yet. Solver source and defaults are unchanged.

## Four profiles

All profiles use one fresh Release binary and enable the existing same-variable
arithmetic propagation. Integer branching retains its private default.

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

After all timing arrays finish, every successful native SAT timing row is
replayed with its exact profile and independently checked with Dolmen. Model
validation time is excluded from runtime and PAR2. Repetition and profile
identities remain distinct.

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
provide the reproducible workflow; the {boundpropsource}`run ledger <docs/run-log.json>`
records execution and provenance.
