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
four positions. Allocations have one CPU and 10 GiB SLURM memory. Replication
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
0.10 (30/30-second limits). Z3 is 5.1.0. The
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

## Timing status and reproduction

The frozen pilot is running. No timing benefit is claimed until all 960 rows
and their separate SAT-model coverage have been audited.

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
