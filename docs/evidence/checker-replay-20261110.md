# Replaying the remaining arithmetic checker failures

**Status: SLURM replay in progress; results are not yet qualified.**
This replay revisits the complete 109-case failure index from the
[full arithmetic certificate sweep](../reference/qf-lra-certificates.md#full-sweep-outcomes-and-failure-boundaries).
The updated checker verified three compact representatives and recovered
challenge construction for the fourth, but that
[representative comparison](../reference/qf-lra-certificates.md#paired-checker-update-10-november)
did not establish recovery of the full index.

## Scope and fixed inputs

The {download}`selection <../../benchmarks/checker-regressions/checker-replay-20261110/selection.json>`
contains the unchanged original failure-index rows: 16 QF_LRA and 93 QF_LIA
cases. Their historical outcomes were 86 proof errors, 20 proof/kernel pipeline
timeouts and three challenge-builder timeouts. Four previously replayed
representatives are included again to obtain one consistently accounted set.

Native certificates and exported-CNF statements are reused directly from
primary array `365550`, with hash and size checks before checker execution.
No original SMT solving, native export, CORE selection or source-to-CNF
conversion is rerun. The checker proves the exported CNF; the transformation
from the original source remains trusted. Reference timeouts, producer export
failures and reconstruction failures are outside this checker-only cohort.

The checker is Micromath `62353d2da3035fdc544c374156bd616b44fb8640`, from the
qualified build `382225`. All six binary hashes, a clean tracked checkout,
CaDiCaL's hash and the optimization executable's hash are checked before work.
LIA uses the preserved version-2 envelope copy: bounded-chunk comparison
requires the native version-3 body to be identical and only byte 28 to change
from `3` to `2`. LIA weights and integer cuts are otherwise unchanged.

## LRA coefficient comparison

Each case receives an original-certificate replay. Fifteen of the LRA cases
also receive cheap and optimized profiles from the
[coefficient adapter](../leads/farkas-coefficient-minimization.md). Clock-7's
worst-case-skew certificate receives only the original replay here: its two
simplified profiles already have [independent qualification](../leads/farkas-coefficient-minimization.md#bounded-qualification-10-november-2026).
Thus there are 109 original and 30 additional planned profiles. A failed
challenge prevents its proof profiles; a failed adapter prevents only that
simplified profile's proof. These outcomes must not count as kernel successes.

All profiles for a case use the same prepared challenge on the same worker.
The optimized profile independently starts from the saved original certificate
and includes the cheap passes. Adapter process wall time and proof/kernel wall
time are separate fields. The internal processing timer in `statistics.seconds`
is not total adapter wall time. Compare kernel step counts and total process
work; smaller printed coefficients alone do not establish faster checking.
These are single-pass comparisons, with no general performance promotion.

## Execution, ownership and acceptance

The {remainingcheckersource}`replay driver <scripts/replay_remaining_checker_failures.py>`
owns a shard-local report, one case's stage records and paths into immutable
primary outputs. It checks one LIA input in bounded chunks and delegates each
LRA transformation to a bounded child process. It does not parse the complete
certificate into a Python syntax tree. Per-stage checkpoints use atomic JSON
replacement, so completed profiles survive an interrupted worker. Expected
checker timeouts/errors are recorded and the worker continues to later cases.

The {remainingcheckersource}`SLURM wrapper <scripts/slurm_replay_remaining_checker_failures.sbatch>`
uses eight workers, two CPUs and 12 GiB each, with 90-minute allocation limits.
Deterministic greedy assignment keeps a case's profiles together and balances
a rough cost estimate; this is workflow scheduling, not a solver algorithm.
The {remainingcheckersource}`pure metadata tests <tests/python/test_checker_handoff_replay.py>`
require every index to occur once and the profile total to
be 139, and check copy-body corruption and strict kernel acceptance.

Stage limits are 30 seconds for finitary axioms per worker, 60 per challenge,
120 per live raw-proof producer/kernel pipeline, and 600 per LRA adapter.
Each external process has a 3 GiB address-space and 256 MiB file bound; optimizer
children have the adapter's separate 16 MiB file bound, one second per call,
30 seconds summed call time and at most 32 queries. No expanded raw proof is
written: the producer's `--raw` output is piped directly into `check-solution`.
Both process exits must be zero, the kernel must print the exact line
`challenge proven`, and `Inconsistency proven!` must be absent.

## Results

Array `382354` is active. Original and simplified outcomes will be reported
separately, including omissions caused by failed earlier stages. The committed
result ledger will retain the source/selection/binary hashes, limits, commands,
per-case stage results, coefficient costs and kernel steps. This page does not
claim that all indexed failures have been recovered.
