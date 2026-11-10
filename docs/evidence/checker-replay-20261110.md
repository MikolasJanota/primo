# Replaying the remaining arithmetic checker failures

**Status: complete bounded replay of the frozen checker-failure cohort.**
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

All eight tasks in array `382354` completed with scheduler exit `0:0`, in
31–39 minutes. All 109 original cases and 30 additional coefficient profiles
have terminal outcomes. No input-integrity or replay-workflow error occurred.

| Original certificate logic | Kernel verified | Proof pipeline timeout | Producer error | Total |
| --- | ---: | ---: | ---: | ---: |
| QF_LRA | 5 | 11 | 0 | 16 |
| QF_LIA | 63 | 5 | 25 | 93 |
| Total | 68 | 16 | 25 | 109 |

All three former challenge-builder timeouts now complete full kernel
verification. Of the 83 former producer out-of-memory cases, 58 verify and
25 report allocation failures. The three former definition-index errors become
one verified case and two pipeline timeouts. Of the 20 former pipeline
timeouts, six verify and fourteen remain timeouts. These are historical-to-new
transitions on identical saved inputs; the full old checker cohort was not
freshly rerun in this campaign.

The 25 producer errors report `free-list allocation` (16), `shared theorem
allocation` (6), or `theorem slot allocation` (3), each prefixed by `Logic failure:`.
The following kernel `invalid solution` occurs after producer failure and does
not establish rejection of a complete proof. Allocation wording does not by
itself identify memory exhaustion or the internal cause. Timeouts likewise do
not localize a phase inside the producer/kernel pipeline. Checker internals
were not inspected or modified.

The {download}`result ledger <../coverage/remaining-checker-replay-20261110.json>`
retains the exact selection, source/binary hashes, resource limits, command
lines, all profile outcomes, coefficient statistics, kernel steps and scheduler
receipts. The {download}`raw metadata archive <../../benchmarks/checker-regressions/checker-replay-20261110/results.tar.gz>`
retains all eight shard reports, diagnostic logs and optimization queries.
Private paths and host fields are normalized; input bytes are unchanged.
The canonical summary is also in {download}`the run log <../run-log.json>`.

The {download}`compact author feedback <../../benchmarks/checker-regressions/checker-replay-20261110/feedback.tar.xz>`
contains five smallest natural representatives: separate LRA/LIA pipeline
timeouts and one case per allocation diagnostic. Selection minimizes required
uncompressed statement plus certificate bytes within each group; no delta
reduction was attempted. The ten-clock LRA pair is 1,770,916 bytes; the LIA
timeout pair is 11,705,579 bytes. The three allocation pairs dominate the
roughly 0.98 GB of uncompressed input, despite selecting the smallest examples.
The bundle includes portable replay commands, observed logs, input hashes and
an inventory of all 41 remaining failures. No expanded raw proof is stored.

These results cover the previous failure index, not a fresh sweep of previously
verified cases or producer/reference failures. They do not qualify a new native
solver default, source-to-CNF proof, or full-corpus checker success.

### Completed coefficient comparison

The fifteen LRA comparisons use the same saved originals as the main replay.

| Profile | Kernel verified | Proof pipeline timeout | Adapter timeout |
| --- | ---: | ---: | ---: |
| Original | 4 | 11 | — |
| Cheap | 4 | 10 | 1 |
| Optimized | 4 | 10 | 1 |

The four verified clock cases have exactly the same arithmetic kernel step
counts in all three profiles. No case gains verification through coefficient
simplification. Both adapter passes on `miplib/vpm2-10` reach their 600-second
cap, so neither produces a replacement certificate; its original proof also
reaches the separate 120-second checker cap. These are distinct limitations.

The fourteen completed optimized transformations issue 448 bounded queries:
370 successful calls produce no better candidate, 77 calls exit nonzero and
one times out. No completed output adopts a new optimizer candidate. The timed-out VPM
profile also leaves 32 query logs, without a completed metrics report or
published replacement; these are outside the 448-query total. Failed calls retain the checked incumbent;
these backend outcomes are separate from checker errors. The configured
32-query limit means this does not establish global coefficient optimality.

The nine blending inputs each retain roughly 12,300 conflicts and more than
500,000 coefficient entries. Their cheap transformations reduce text but still
reach the checker deadline; the completed optimized transformations take
roughly four minutes per file, before proof checking. This cohort gives no
checker-performance justification for routinely adding coefficient optimization.
The adapter remains an optional experiment. The separately authored example
still demonstrates that changing weight ratios can reduce kernel work; it is
not representative performance evidence for these real cases.
