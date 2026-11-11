# Medium arithmetic coefficient comparison

**All 12 checker replays passed; these bounded optimizer profiles give no improvement.**
On Rings, LassoRanker and Clock7 main invariant, size-first and support-first
produce exactly the cheap pass's certificate bytes. Arithmetic proof-step
counts are identical even to the originals. The size/support adapters add 12–24 seconds
without reducing checking cost, so these trials justify neither repetition
nor a default change. The subsequent [gprof experiment](checker-gprof-20261111.md) separates producer
and kernel CPU before further optimization.

## Results

| Case | Original proof seconds | Cheap: adapter + proof seconds | Size: adapter + proof seconds | Support: adapter + proof seconds | Arithmetic steps, every profile |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rings, QF_LIA | 44.89 | 11.79 + 44.94 | 12.94 + 45.14 | 13.54 + 46.15 | 88,826,359 |
| LassoRanker, QF_LRA | 73.99 | 22.10 + 74.14 | 23.87 + 76.27 | 23.72 + 75.88 | 162,989,173 |
| Clock7 main invariant, QF_LRA | 68.73 | 10.54 + 68.73 | 12.09 + 69.65 | 12.30 + 69.65 | 160,508,527 |

Proof times measure the streaming producer/kernel pipeline. Adapter times
include startup, scanning, exact passes, writing and hashing. Their sum omits
shared challenge construction and small controller/envelope/hash operations;
it is not complete end-to-end wall time. These are single measurements,
not evidence of small timing differences. Counts measure proof volume, not CPU.

The cheap passes reduce LassoRanker's maximum witness height from 22 to 19 bits
and Clock7's from 57 to 50; Rings remains at 17. Those changes do not reduce
arithmetic proof volume. Witness heights exclude input and derived-row numbers.
Size/support add no further change under this budget. This finding concerns
these three inputs and settings, not all coefficient optimization techniques.

## Optimizer diagnostics

Each size/support run attempts 32 queries: 192 calls in total. All 150 calls
that successfully return a model yield no better candidate. The remaining
42 calls are on LassoRanker: stdout begins `unsat`, followed by a model-unavailable
error, and the process exits nonzero. The adapter records an external error,
rejects the output and retains its checked incumbent. These are not Micromath
failures. They are consistent with infeasibility of the bounded integer search;
the failed process is not accepted as successful optimization or proof of global
minimality. Standalone queries and complete stdout/stderr remain in the bundle.

Budgets are unchanged: query only incumbents of at least 8 bits, 32 queries per profile, at most
256 premises and 4,096 encoding bits, one second per call and 30 seconds summed
optimizer-call time. Size means min-max then L1; support first minimizes the
number of nonzero weights before those objectives. Both start with cheap passes
and preserve the statement and exact LIA cut conclusions. Selection is already
CORE-minimized. No native solving, export or CORE extraction was repeated.

## Reproduction and evidence

The {download}`selection <../../benchmarks/checker-regressions/checker-medium-comparison-20261111/selection.json>` fixes the three inputs from the
[nine-case discovery](checker-medium-20261111.md). The
{download}`ledger <../coverage/checker-medium-comparison-20261111.json>` contains
challenge, adapter and proof-pipeline times, proof counts, adapter diagnostics and binary/input hashes.
The {download}`bundle <../../benchmarks/checker-regressions/checker-medium-comparison-20261111/results.tar.gz>` is 2,572,034 bytes, SHA-256
`d05e0e00de6d020100fcdad1792b6d7ae67c068ed1c6356e8882ab854197258c`.
Its 736 payload hashes and sizes were checked after collection. It contains
saved input/CNF/certificate files, all profile logs, optimizer problems/models,
frozen scripts and scheduler accounting. It excludes expanded proof streams,
executable binaries and checker implementation. Private text metadata prefixes
are normalized; benchmark and certificate bytes are unchanged.

Workflow `e8153853` uses {mediumcomparesource}`replay_checker_medium.py
<scripts/replay_checker_medium.py>` and
{mediumcomparesource}`the comparison wrapper <scripts/slurm_checker_medium_comparison.sbatch>`.
The driver owns each profile's stage records and paths, checkpoints atomically,
and streams LIA envelope copies without retaining a file-sized syntax tree.
Each profile proves the same challenge built from the original exported CNF.
Acceptance requires both process exits zero, exact `challenge proven`, and no
`Inconsistency proven!`. Source-to-CNF equivalence remains trusted.

SLURM `382392_0..2` used two CPUs and 8 GiB per worker, 20-minute allocations,
120-second proof and 150-second adapter limits, 3 GiB child address space and
256 MiB child file limits. All workers passed six pure workflow tests. Archive
job `382558` also completed successfully; the
{download}`scheduler receipt <../../benchmarks/checker-regressions/checker-medium-comparison-20261111/accounting.tsv>` records both stages. Checker revision is
`62353d2da3035fdc544c374156bd616b44fb8640`; native exports remain `a5123159`.
Z3 5.1.0's executable and all six checker executables are hash-pinned in the ledger.
The canonical summary is in {download}`the run log <../run-log.json>`.
