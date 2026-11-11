# Successful medium-cost arithmetic checker cases

**Status: eight medium-cost successes and one harder reserve, freshly replayed.**
Nine real cases from nine benchmark families still verify with Micromath
`62353d2da3035fdc544c374156bd616b44fb8640`. Eight take 27.77–74.34 seconds in the combined
proof-producer/kernel pipeline; the bit-adder takes 98.23 seconds. This is a
reusable set for coefficient experiments on cases already known to succeed.

The {download}`selection <../../benchmarks/checker-regressions/checker-medium-20261111/selection.json>` records exact file
names, original locations, hashes, CORE counts and historical checker evidence.
The {download}`fresh ledger <../coverage/checker-medium-20261111.json>` records
all stages, coefficient metrics, proof steps, commands and binary/source hashes.
The {download}`replay bundle <../../benchmarks/checker-regressions/checker-medium-20261111/results.tar.gz>` is 5,366,233 bytes,
SHA-256 `e9c3ac8bc880d2d52213dd16665f86df404caabbb31953d73e74effcb33b4bd1`. All 176 payload hashes/sizes,
original source/native/statement hashes and compatibility copies were checked
after collection. Private metadata prefixes are normalized; input bytes are
unchanged. The bundle omits raw proof streams, executable binaries and external
solver/checker implementations.

The subsequent [three-case comparison](checker-medium-comparison-20261111.md)
checks original, cheap, size-first and support-first profiles. All verify, with
unchanged arithmetic proof counts and no optimizer gain under the stated budget.

## Fresh measurements

| Case | Logic | Proof + kernel seconds | Cheap scan seconds | Witness bits: original → cheap |
| --- | --- | ---: | ---: | ---: |
| Fischer 11–14 | QF_LIA | 45.40 | 2.52 | 1 → 1 |
| WISA5 | QF_LIA | 27.77 | 61.22 | 3 → 3 |
| Rings: 2exp14, 4vars, 3ite | QF_LIA | 45.34 | 11.79 | 17 → 17 |
| Scheduling 165 | QF_LIA | 32.62 | 12.44 | 5 → 5 |
| UART28 | QF_LRA | 28.27 | 16.55 | 15 → 8 |
| LassoRanker: Braverman, 4-piece | QF_LRA | 74.34 | 22.10 | 22 → 19 |
| MIPLIB pp08a-3000 | QF_LRA | 43.53 | 14.64 | 13 → 5 |
| Clock7 main invariant | QF_LRA | 68.47 | 10.43 | 57 → 50 |

The CIRC 6-bit adder is a verified harder reserve: 98.23
seconds for proof plus kernel, 36.13 seconds for
its cheap scan, and witness bit height 6 before and after. It falls outside the
chosen 10–90 second medium band. These are single-pass timings on the same
pinned checker, not a statistical speed comparison or an old/new checker claim.

Proof/kernel wall time excludes challenge construction and coefficient scanning.
Cheap scan time includes process startup, parsing, exact candidate passes,
output writing and hashing. Only original certificates receive a kernel replay
here; `cheap.sexp` outputs are retained for statistics and future comparisons.
No optimizer is called and no optimization speedup is inferred from smaller
printed witnesses. Every case uses the already completed CORE selection.

The bit metric measures witness multipliers (numerators/denominators for LRA)
and divisors for LIA. It does not measure coefficients or bounds in the original
inequalities or intermediate derived rows. Small multipliers therefore do not
establish that all arithmetic in a proof uses small numbers.

## Which cases to try first

- Rings, LassoRanker and Clock7 give 45–74 second successes with 17–57-bit
  original witnesses. Their reported arithmetic proof work is respectively
  88.8, 163.0 and 160.5 million kernel steps. These are the first candidates for
  trials that change weight ratios or support.
- Scheduling gives a 33-second LIA case with 1,632 conflicts and 76,145
  nonzero weights. Fischer and WISA add cases with already small multipliers
  and divisors (1 and 3 bits), useful for testing support-oriented changes.
  Future optimizer trials on these controls need `--min-bits 0`; the default
  8-bit threshold would skip them.
- UART and MIPLIB add distinct LRA families. Cheap normalization reduces their
  maximum witness heights from 15 to 8 and 13 to 5 bits. Their transformed
  checker cost has not been measured by this discovery run.
- WISA's 61-second Python scan already exceeds its 28-second proof pipeline.
  End-to-end experiments must include that overhead; it is a less suitable
  first timing target for the current standalone adapter. Proof-step categories
  are volume counters, not measurements of CPU time inside the checker.

The slow large NEC successes were not selected: many have no arithmetic proof
steps, and their roughly 100–170 MB inputs target other costs. The current set
keeps native certificates between 1.02 and 18.89 MB, 50.57 MB total, and retains
both solver/export success and successful checker evidence for every candidate.

## Selection, ownership and execution

Candidates came from the saved full sweep and the later positive checker
replays. Newer outcomes supersede older statuses: Fischer, WISA and Clock7
have successful current-checker replays despite older timeout/error records.
The six other cases had successful older-checker records and were rechecked
here. One case per top-level family provides variety; this is a curated sample,
not an exhaustive corpus ranking. All nine historical selection times were
10–90 seconds, and certificates were limited to 25 MB with retained lemmas.

The {mediumcheckersource}`driver <scripts/replay_checker_medium.py>` owns a
worker report and one case's paths/stage records. It streams file hashes and
LIA compatibility checks, delegates each proof and scan to a bounded child,
and retains completed case records in atomic JSON checkpoints. It does not
parse complete certificates into a Python syntax tree. The optional coefficient
scan uses the separately qualified streaming adapter; a scan failure remains
separate from original-kernel verification. The
{mediumcheckersource}`pure tests <tests/python/test_checker_medium.py>` cover
selection safety, duplicated cases, failed-process acceptance and band edges.

SLURM `382375_0..3` completed with exit `0:0`; each worker passed four pure
metadata/acceptance tests. The {mediumcheckersource}`wrapper <scripts/slurm_checker_medium.sbatch>`
uses four workers, two CPUs and 8 GiB each, with 15-minute allocations. Per-stage
limits are 30 seconds for axioms, 60 for challenges, 120 for proof/kernel and
180 for scans. Children have 3 GiB address-space and 256 MiB file limits. Each
raw proof is piped directly to the kernel; both exits must be zero, the exact
line `challenge proven` must occur, and `Inconsistency proven!` must be absent.
All six checker executables, the tracked checkout and CaDiCaL are pinned.

Native solving, export and CORE selection are reused from `365550` at producer
`a5123159`. They were not rerun, and source-to-CNF equivalence remains trusted.
Workflow source is `05a51f90`; arithmetic postprocessor code is unchanged from
its prior qualification. Archived frozen scripts, inputs and stage records
support replay without the original benchmark corpus. Archival job `382379`
completed with exit `0:0`; the {download}`post-archive scheduler receipt
<../../benchmarks/checker-regressions/checker-medium-20261111/accounting.tsv>`
records replay and archival completion. The canonical entry is in
{download}`the run log <../run-log.json>`.
