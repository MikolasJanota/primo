# Conflict-selected Ackermann pilot

This study builds and evaluates a bounded opt-in prototype of the
conflict-selection idea in Dutertre and de Moura (2006), §2 p. 3
{cite:p}`dutertre2006yices`. It exposes a full congruence clause to SAT when
that congruence step appears in an online EUF conflict explanation. It requires
existing equality atoms; it never creates missing atoms. The
[design checklist](../leads/conflict-ackermann-prototype.md) states the
representation, ownership, proof obligations and exclusions.

The candidate native source is `13230b6d`, SHA256
`39b346b31f830e7c79aeeb3829e9cd469e2ee9399830a26756b4a2d412f6af7e`.
Both options default off. This is a development pilot; it supplies no default
promotion or representative solver-ranking claim.

## Frozen comparison

The {download}`protocol <../../benchmarks/conflict-ackermann-20261117/protocol.json>`
and {download}`selection <../../benchmarks/conflict-ackermann-20261117/selection.json>`
were frozen before candidate corpus timing. Each logic has forty inputs:
twenty family-round-robin priority cases and twenty deterministic hash-ordered
family-round-robin fills. A family is the first path component. UF priorities
use historical native/fast-reference gaps; UFLIA priorities use printed
congruence-explanation activity from the prior arithmetic combination study.
That activity includes propagation explanations and does not establish
conflict-only opportunities. This target enrichment prevents global conclusions.

| Profile | Native source | Additional option |
| --- | --- | --- |
| BASE | Frozen `4f02a8b7`, production-equivalent to pre-change `fca39be2` | None |
| OFF | `13230b6d` | None |
| DIAG | `13230b6d` | `--euf-conflict-ackermann-diagnostics` |
| ON | `13230b6d` | `--euf-conflict-ackermann` |

All use `--quiet --stats`, existing defaults, one-sided explanations and
private integer branching. No preprocessing/row/hybrid option is changed.
Each input gets three paired repetitions, four profiles per allocation,
30 seconds per native process and an 8 GiB address-space limit. Profile order
rotates/reverses by input and repetition. Three repetitions do not fully balance
four positions. Allocations request one CPU and 10 GiB SLURM memory; recorded native
affinity contains two logical CPUs per allocation. Replication
arrays run sequentially; profiling and correctness jobs finish before timings.
This gives 960 native rows over 240 allocations. Unknown and timeout rows are
unsolved; PAR2 charges each unsolved row 60 seconds.

One selected Certora input has unknown expected status; historical references
also timed out. Its expected answer is not invented. Paired/cross-repetition
answer agreement is checked, and every native SAT timing row must be replayed
separately through Dolmen. Validation uses 180 seconds for native replay and
60 seconds for Dolmen, outside the speed measurements.

## Correctness qualification

The {download}`qualification summary <../../benchmarks/conflict-ackermann-20261117/qualification.json>`
contains receipt hashes. Six exact logic/profile tasks cover QF_UF and QF_UFLIA
with OFF, DIAG and ON:

| Stage | Coverage | Result |
| --- | ---: | --- |
| Generated differential checks against Z3 | 6,000 | 5,999 pass; one existing integer-node-limit unknown |
| Tracked regression measurements | 87 | Passed, including scoped query/model checks |
| Real-seed original measurements | 72 | Passed |
| Real-seed mutated inputs | 1,951 | 1,935 pass; 16 native recheck timeouts |
| Independent SAT-model checks across these stages | 5,416 | Direct Dolmen pass |

Generated seeds start at `2026121700`; mutation seeds at `2026121800`, plus
task slot. The repaired build restarts qualification with these seeds; initial
`427f5618` gates are superseded. UF uses FuzzSMT; UFLIA uses the repository's
mixed depth-three generator. Each task mutates twelve real seeds through
YinYang 0.3.0 with thirty iterations per seed, a 600-second campaign limit and
five-second mutation solver limits. Rechecks allow ten seconds. Campaign output
counts can differ; at least one hundred mutants per task are required. Native
SAT inputs are replayed with `--check-model` and independently checked by Dolmen
0.10. Generated, original-seed and mutant replays use 30 seconds native /
30 seconds Dolmen; tracked regression and scoped checks use 120 seconds native /
30 seconds Dolmen. Z3 is 5.1.0. The
{download}`tool receipt <../../benchmarks/conflict-ackermann-20261117/tools.json>`
records versions and available binary hashes.

No wrong answer, crash, invalid model or unexplained reference disagreement was
observed. Resource-incomplete results remain separate from passes. The generated
unknown was replayed at thirty seconds and diagnosed as the existing integer
branch-and-bound node limit; no mutant unknown required another diagnostic replay.
These gates exercise the exact three profiles at default explanation/combination
settings, not every option combination.

The helper built the native through SLURM. Repaired `make check` passed forty-nine
native groups and 369 Python tests; ASan passed all forty-nine groups. Three new
pure workflow tests and `make lint` also pass. The
{download}`static coverage record <../../benchmarks/conflict-ackermann-20261117/static-coverage.json>`
keeps the original whole-tree failure visible: 141 translation units were
attempted, with a Clang initialization error fixed in `13230b6d`. The two changed
bodies and `main.cpp`, which consumes the generated revision header, were
rescanned successfully. The other 138 units have unchanged owned inputs.
This composed coverage is not a fresh successful whole-tree aggregate, and
baseline advisory warnings remain. The new selector has no rescan finding.

## Bounded instruction profiles

The {download}`profile summary <../../benchmarks/conflict-ackermann-20261117/profiles.json>`
records OFF/DIAG Callgrind 3.22.0 captures on `PEQ/PEQ011_size8.smt2` and
`mathsat/EufLaArithmetic/hard/hard14.smt2`. Each returns the expected resource
unknown at one hundred reported theory conflicts. The sixty-second profiling
deadline was not reached after disabling remote debug-symbol downloads.
Earlier attempts stalled with those downloads enabled and are excluded.
These are capped diagnostic executions, not successful full-query timings.

On the UF capture, preprocessing accounts for 54.24% of OFF's inclusive
instruction count, including symmetry breaking at 26.74%; the recorded
`explain_equal_into` entry accounts for 6.54%. On the UFLIA capture,
model reconciliation accounts for 34.95%, and class merging for 6.59%.
Inclusive costs overlap. DIAG increases total instructions by about 0.69%
and 1.33%, respectively, in these single capped pairs. These numbers do not
establish a whole-query runtime overhead.

The UF DIAG capture visits 18,438 congruence steps in one hundred conflict
explanations, retains 3,897 prefix entries and catalogs 717 semantic pairs.
It reports 716 missing-result events and 340 missing-argument occurrences;
forty-two traces are truncated. The UFLIA capture has only three traced EUF
conflicts and three semantic pairs by the mixed-theory hundred-conflict cap;
all three lack a result equality. Thus historical congruence-explanation
activity alone can overestimate the opportunities available to this selector.
Missing-event counts are overlapping outcomes, not a partition of pairs.

## Paired timing result

All 960 native rows agree on the same solved inputs in every profile and
repetition: 34/40 QF_UF and 28/40 QF_UFLIA. There are 216 timed-out rows,
no normal resource unknowns, and no errors or answer disagreements.
All **348 successful native SAT timing rows** pass separate direct Dolmen
validation. The
{download}`full audit <../../benchmarks/conflict-ackermann-20261117/full/analysis.json>`
checks all 240 allocation receipts, exact options/input hashes and model
coverage. The
{download}`measurement CSV <../../benchmarks/conflict-ackermann-20261117/full/measurements.csv>`
retains every repetition, runtime, RSS and available mechanism counter.
Family summaries in the audit use the first path component; solved input
sets are unchanged within every family as well.

| Logic | Profile | Solved in each repetition | Mean PAR2 seconds, repetitions 0 / 1 / 2 |
| --- | --- | ---: | --- |
| QF_UF | BASE | 34 | 11.049 / 11.072 / 11.072 |
| QF_UF | OFF | 34 | 11.058 / 11.067 / 11.067 |
| QF_UF | DIAG | 34 | 11.117 / 11.110 / 11.109 |
| QF_UF | ON | 34 | 11.115 / 11.112 / 11.121 |
| QF_UFLIA | BASE | 28 | 21.129 / 21.145 / 21.130 |
| QF_UFLIA | OFF | 28 | 21.139 / 21.138 / 21.120 |
| QF_UFLIA | DIAG | 28 | 21.142 / 21.128 / 21.136 |
| QF_UFLIA | ON | 28 | 21.146 / 21.143 / 21.131 |

For each input solved in all three repetitions by both compared profiles,
first take the median of its three paired runtime ratios. The geometric mean
over inputs with OFF median runtime at least one second is 1.0290 for ON/OFF
on UF (fourteen inputs) and 1.0078 on UFLIA (twelve inputs). DIAG/OFF gives
1.0318 and 1.0078. ON/DIAG is approximately 1.0014 and 1.0011, respectively.
The feature shows no speedup here; most observed UF cost is already present in
the diagnostic profile. OFF/BASE ratios are 1.0005 and 0.9996 on the analogous
baseline-qualified sets. No common solved input meets the auditor's follow-up
rule of being at least 10% and 0.5 seconds slower in all three pairs.
These small selected-set differences are descriptive, not significance tests.

Across all three repetitions, median/max process RSS in KiB is 20,352/195,560
for UF OFF and 20,690/198,120 for UF ON; UFLIA OFF gives 16,086/691,300 and ON
16,078/696,844. RSS is measured per process, including unsuccessful rows.
Matched successful-row counters show no meaningful reduction in search:
UF SAT conflicts are 230,622 OFF versus 230,625 ON, with 590,667 versus 590,685
reported theory conflicts. UFLIA has identical 73,572 SAT conflicts and
232,368 theory conflicts. These are sums over three repetitions on the same
34 UF / 28 UFLIA common solved inputs.

### Mechanism coverage and limits

On successful UF ON rows, the selector records 590,685 conflict explanations,
121,364,325 congruence-step visits, 22,071,933 retained prefix entries and
138,723 semantic-pair entries. It constructs and delivers only thirty clauses
(sixty literals), five per repetition on each of two small QG inputs:
`QG-classification/qg5/dead_dnd044.smt2` and
`QG-classification/qg5/iso_icl089.smt2`. SAT-conflict counts do not improve
materially. Missing-result events number 138,681 and missing-argument
occurrences 105,759; these overlap. Counts span repeated solves and, where
applicable, rebuilt catalogues, so they are not globally distinct pairs.

The bounded policy also reaches its engineering limits: successful UF rows
have 236,319 truncated traces and 7,575,603 catalogue-limit exits; peak catalogue
size is 4,096 and peak pending queue is one. A full catalogue can exclude later
opportunities even if they would have all required atoms. Coverage therefore
measures this bounded prefix/catalogue, not all conflicts or all potential
Ackermann clauses.

Two additional QG inputs time out in every profile while ON delivers 37 and
21 clauses, respectively, in each printed timeout snapshot. These 174 delivered
clauses across six unfinished runs are kept separate from the thirty clauses
on successful rows. All eighteen UF incomplete rows per profile print some statistics.
For UFLIA, printed snapshots are available on 22 OFF, 24 DIAG and 23 ON of the
36 incomplete rows per profile; unprinted counters remain unavailable.
No printed UFLIA row has a ready or sent clause. Successful UFLIA ON rows have
39,240 traced EUF conflicts and 50,298 catalogue entries, all with missing-result
events; peak catalogue size is 363. The broader combination search's conflicts
are not all EUF conflict-explanation opportunities.

### Decision

The bounded existing-atom policy is retained as an explicitly experimental,
default-off prototype and diagnostic aid. It is not adopted as a performance
profile: it adds modest UF overhead without solving another input or reducing
search materially in this pilot. This result does not reject conflict-selected
Ackermannization with fresh atoms. It identifies missing result equalities and
bounded selection saturation as concrete limitations to address in a separate
extension, with safe atom registration and new correctness/evaluation gates.
No budget tuning, atom-growth implementation or default promotion is performed
in this pass.

## Reproduction and artifacts

Source/checklist: {conflictacksource}`selector <src/smt/conflict_ackermann.cpp>`,
{conflictacksource}`trace and callback owners <src/smt/solver_engines.cpp>`.
The {download}`run ledger <../run-log.json>` records provenance. Workflows live in
`scripts/qualification/conflict_ackermann/`, with immutable shared timing/model
runners under `scripts/qualification/lia_hybrid/` and
`scripts/qualification/arithmetic_combinations/`. `configure.py` recreates all 242 private timing cards from a qualification
campaign and the frozen selection; byte-for-byte reproduction was checked.
Private SSH configuration,
cluster workspace mappings, hostnames and job identifiers remain outside tracked
files; raw artifacts are under `results/conflict-ackermann-20261116/`.
