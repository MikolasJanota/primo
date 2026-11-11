# Sparse witnesses and integer-cut simplification qualification

**Status: completed bounded SLURM qualification, 11 November 2026.**
The optional [coefficient adapter](../leads/farkas-coefficient-minimization.md)
now accepts native version-3 QF_LIA certificates and a support-first objective
for both logics. Production Python code and qualification workflows are pinned to `9b4b490f`. Native C++ remains `a5123159`. This is
proof-postprocessor qualification, not a native solver performance evaluation.

The {download}`ledger <../coverage/arithmetic-coefficients-20261111.json>`
records hashes, limits, commands and all 80 profile outcomes. The
{download}`replay archive <../../benchmarks/checker-regressions/arithmetic-coefficients-20261111/results.tar.gz>`
contains 17,097 payload files plus its manifest,
2,274,901 compressed bytes, SHA-256
`2f29e6832d806b46e759f429c84c75393ddc4cc7f2093d2710ee7cd90fbc7318`. Every payload hash/size was checked after collection.
It retains input/output certificates, standalone optimization problems/models,
reference and checker diagnostics, accounting and frozen workflows. Private
absolute prefixes in text metadata are replaced by environment names. Raw proof
streams and binary challenges are omitted; kernel proofs streamed via `--raw`.

## Exact/reference gate

SLURM `382370_0` and `382370_1` each ran 1,000 freshly generated cases,
filtered to LRA and LIA respectively; each generator starts independently with
seed `20261111`.
Each logic has 750 UNSAT witness cases and 250 SAT negative controls. Z3
(`Z3 version 5.1.0 - 64 bit`; executable hash in the ledger) agreed with every source
status. All 1,500 valid certificates passed exact replay, unchanged
non-numeral structure and CaDiCaL exported-CNF UNSAT. All 500 SAT controls failed
normally without publishing output. Wrong answers, unexpected rejections,
crashes, unexpected parser/type errors, reference disagreements, and
source-reference/adapter timeouts were zero. External optimizer fallbacks are
recorded separately in each adapter report.

The first 80 cases per logic enable the external optimizer, alternating
`size`/`support`; remaining cases run cheap passes only. Reports check the bit
height and combined weight/divisor byte guards. SAT controls are expected
invalid arithmetic witnesses, not rejections of valid source syntax. LRA cases
exercise rational rows, strictness and duplicates; LIA cases use half-integral
intervals, negative bounds, multiple cuts, duplicates and zero-weight references.
These generated classes do not represent all arithmetic proof shapes.

## Independent kernel replay

SLURM `382370_2` verified all 72 profiles: fourteen LRA inputs (twelve retained
starters, clock-7 and the authored four-row example), plus four LIA inputs
(the native and hand-written proposal, the authored four-row example and a
scaled negative-floor derivation), each in original/cheap/size/support modes.
SLURM `382370_3` verified eight more profiles for two authored sparsity probes.
Every accepted pipeline has producer and kernel exit zero, `challenge proven`,
and no `Inconsistency proven!`. Checker revision is `62353d2da3035fdc544c374156bd616b44fb8640`.
The unchanged LIA body is wrapped in the checker's required version-2 envelope.

| Authored case/profile | Nonzero weights | Maximum numeral bits | Arithmetic kernel steps |
| --- | ---: | ---: | ---: |
| LIA coefficient choice, original | 4 | 4 | 26,808 |
| LIA coefficient choice, size | 4 | 3 | 15,198 |
| LRA support probe, original/cheap | 4 | 6 | 48,226 |
| LRA support probe, size | 4 | 3 | 30,062 |
| LRA support probe, support | 3 | 6 | 30,755 |
| LIA support probe, original/cheap | 4 | 6 | 33,532 |
| LIA support probe, size | 4 | 3 | 15,198 |
| LIA support probe, support | 3 | 6 | 22,665 |

The LIA coefficient-choice example changes weights $(11,7,5,9)$ and divisor 9
to $(2,3,4,1)$ and divisor 1, preserving the exact conclusion $0\le-1$.
The support probes reuse the four-row matrix with incumbent weights
$(41,33,31,31)$ (divisor 31 for LIA). `support` chooses $(34,13,0,31)$,
whereas `size` chooses $(2,3,4,1)$. This tests a nontrivial support alternative
that the cheap passes do not find. Both satisfy the guards against the original
incumbent, but fewer premises does not imply fewer checker steps than the
smaller-numeral solution. `size` remains the default; neither mode promises a
globally smallest serialized witness.

These are single passes. Subsecond cases establish no timing gain; the ledger
keeps adapter/optimizer and proof-pipeline wall time separate. All original
representatives already verified. No new solved-instance coverage or full-corpus
LIA checker benefit follows from this set. Interpolation quality was not tested.

## Ownership and completion checks

The 500-cut chain unit probe keeps sampled row-table occupancy at two rows;
retirement counters exclude transient instruction data and the terminal row.
All four jobs passed all 29 focused tests, including exact adoption of the
sparse four-row witness, early proof-byte cutoff and second-scan offsets beyond
a lexer block.
Local `make lint`, documentation builds, local/Git link checks and rendered
formula/table inspection accompany the changes. No native C++ code changed, so native sanitizer/tidy/model-validation suites were not
rerun for this standalone Python postprocessor.

Main jobs use two CPUs and 8 GiB per task, with a 15-minute limit; the small
probe shares the same 15-minute allocation limit. Reference calls have five seconds, adapter
processes 150 seconds and checker pipelines 120 seconds; child address space
is 3 GiB. Optimization defaults remain one second per call, 30 seconds summed,
32 queries and 256 premises. Archival job `382374` checks completion before
packaging. Allocation and source provenance are independent of certificate
size or checker cost.
