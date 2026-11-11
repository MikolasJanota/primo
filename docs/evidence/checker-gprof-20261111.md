# Gprof profiles of successful arithmetic checking

**The producer command uses most measured CPU; kernel samples concentrate on number decoding.**
All three cases verify with both builds, and complete function profiles are now
available after enlarging Clock7's profiler buffer. The profiling experiment separates Micromath's raw proof producer from its
kernel on three original saved CORE certificates: Rings (QF_LIA), LassoRanker
and Clock7 main invariant (QF_LRA). It follows the
[bounded coefficient comparison](checker-medium-comparison-20261111.md), which
found unchanged proof volume and no optimization gain on these inputs.

## Baseline CPU and memory

| Case | Pipeline wall seconds | Producer CPU seconds | Kernel CPU seconds | Producer maximum RSS MiB | Kernel maximum RSS MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rings | 45.72 | 45.42 | 16.14 | 90.17 | 12.68 |
| LassoRanker | 76.71 | 76.19 | 30.34 | 116.82 | 12.41 |
| Clock7 main invariant | 69.84 | 69.39 | 28.63 | 93.49 | 11.63 |

The wall column uses GNU time's producer elapsed value; the independently
recorded controller pipeline time includes a small startup/cleanup increment.
CPU is user plus system time. These initial uninstrumented measurements put
71–74% of the combined CPU accounting in the producer command, including
waited-for external SAT. Its CPU time is close
to its elapsed time; the kernel spends substantial elapsed time waiting for
streamed proof bytes. With this allocation and these inputs, the producer
command is the main CPU cost. This does not establish the bottleneck of other cases
or of a standalone kernel consuming a pre-existing proof file.

Both commands report zero filesystem input blocks and only eight filesystem
output blocks in these baselines. Cached input reads can account for the former;
these counters do not prove that no disk reads occurred. Expanded proof steps
are never written to disk. The measured maximum RSS and CPU use give no sign
of a memory-cap or slow-disk bottleneck in this three-case experiment.

## Function profiles and proof volume

Percentages below are shares of gprof's sampled self time within each executable's
covered code. They are not shares of pipeline wall time or complete command CPU.

| Measurement / function | Rings | LassoRanker | Clock7 main invariant |
| --- | ---: | ---: | ---: |
| Exported primitive proof steps | 90,417,780 | 165,625,482 | 162,602,600 |
| Producer `theory_define` | 10.59% | 11.75% | 9.25% |
| Producer `proof_stream_write` | 5.05% | 5.73% | 5.53% |
| Kernel `proof_stream_read_number` | 48.23% | 51.87% | 50.48% |
| Kernel `proof_stream_replay` | 21.40% | 17.82% | 19.17% |

The kernel makes respectively 355.9, 652.2 and 636.7 million calls to
`proof_stream_read_number`. `rule_substitute2` adds 10–12% of sampled kernel
self time. The producer's costs are more distributed: `theory_define` makes
67.9, 104.4 and 99.6 million calls; node, scratch, boundary and definition-table
symbols also appear near the top. Rings additionally spends 8.10% of covered
samples in `numeral_prove_mul`. Full flat tables and call graphs are retained,
including unattributed/runtime symbols such as `_init`.

The exported native certificates are only 2.46, 3.47 and 1.02 MB respectively,
yet their primitive proofs contain 90–166 million steps. Proof volume and
construction/decoding work are concrete leads for the checker author. The
profile does not identify which proof-sharing, numeral or encoding change
would improve them. It also does not establish that coefficients are irrelevant:
Rings shows measurable numeral-multiplication work, and reducing proof volume
through a different witness remains a possible approach. The earlier bounded
weight profiles simply did not reduce this work. No checker implementation is
inspected or optimized here.

## Measurement boundary

The qualified checker remains at `62353d2da3035fdc544c374156bd616b44fb8640`.
A detached worktree of that revision supplies a separate profiling build.
Compiler options remain `-O2 -g -Wall -Wextra`, with `-pg` added at compilation
and linking. Build metadata records flags, compiler, dependencies, binary hashes
and instrumentation symbols. The qualified binaries are preserved. Neither
Primo solving/export nor coefficient optimization is rerun.

For each input, an uninstrumented run measures the producer and kernel
separately with GNU time. An instrumented run then uses the same original
certificate and exported-CNF challenge. Proofs remain direct pipes. Each pair
must exit zero and print exact `challenge proven` without `Inconsistency proven!`.
The two runs must also report equal proof-step categories and counts.
Source-to-CNF equivalence remains trusted; original LIA v3 certificates use
the checked body-preserving v2 compatibility envelope.

The producer's process CPU accounting includes waited-for external SAT work;
gprof does not instrument that separate SAT executable. The two elapsed times
overlap because the processes run concurrently, and the kernel waits for proof
bytes. Add user and system CPU seconds to compare CPU use; do not add process
elapsed times to obtain pipeline wall time. Maximum RSS belongs to each timed
command, including waited-for descendant accounting; it is not the summed
concurrent pipeline peak. Instrumented timings include profiling overhead and are not solver-speed or optimization comparisons.

Gprof's flat tables estimate sampled self CPU time within their covered code.
Call counts come from instrumentation; child times in call graphs are estimates.
Uninstrumented dependencies and shared libraries have incomplete call/sample
coverage, and optimized/inlined code affects symbol attribution. Sampling
percentages are not percentages of wall time or exact cost attribution. No
checker implementation or source annotation is inspected in this experiment.

## Workflow and limits

The {checkerprofsource}`profiling driver <scripts/profile_checker_medium.py>`
owns one case's immutable paths, process records and atomic JSON checkpoint.
It checks the original input, CNF and certificate hashes, six binary hashes
for each build, both tracked checkouts and the SAT executable. Each process
gets a distinct absolute `GMON_OUT_PREFIX`, preventing concurrent files from
overwriting one another. The driver requires one gmon file per instrumented
process and invokes `gprof -b EXECUTABLE GMON` to produce ordinary function
tables and call graphs. It retains timings, gmon records and diagnostics,
without retaining an expanded proof stream or parsing a whole certificate.

The {checkerprofsource}`build wrapper <scripts/slurm_build_micromath_gprof.sbatch>`
uses four CPUs, 8 GiB and 20 minutes. The
{checkerprofsource}`profiling wrapper <scripts/slurm_profile_checker_medium.sbatch>`
uses three workers, two CPUs and 8 GiB each, with 15-minute allocations.
Stage limits are 30 seconds for axioms, 60 for challenges, 120 for the baseline
pipeline, 600 for the instrumented pipeline and 30 per gprof report. Checker
children have 3 GiB address-space and 256 MiB per-file limits. These limits
also bound gmon output; a killed run is incomplete profiling, not a new
checker timeout under the original qualification budget.

The profiling scripts were frozen over base `e8153853` and committed unchanged
as `3c5f48fa`; per-file hashes record the added workflow independently of
that base commit. Native certificates remain from `a5123159`. GNU time is provisioned on shared storage and hash-pinned
because it exists on the login host but not in the compute-node image.
Three pure tests validate distinct CPU/elapsed metric parsing, rejected
incomplete/nonfinite metrics and generated shell syntax. They do not execute the solver or checker.

## Profiler setup and recovery

Build `382587` succeeded. Setup array `382591` stopped on a missing staged
Python import; `382595` stopped because `/usr/bin/time` was absent from compute
nodes. These setup errors occurred before proof checking. The shared-storage
copy of GNU time is pinned by hash in the final wrapper and summaries.

Array `382598_0..2` verifies all six baseline/instrumented proofs. Its Clock7
worker then fails to collect a producer profile: glibc prints
`mcount: call graph buffer size limit exceeded, gmon.out will not be generated`.
The proof itself still passes, with identical step counts. Its kernel gmon
file survives. Rings and LassoRanker have complete function reports.

Clock7 alone is retried as `382605_2` using the same checker binaries and scripts,
with `GLIBC_TUNABLES=glibc.gmon.minarcs=1000000:glibc.gmon.maxarcs=1000000`.
This enlarges the profiler's arc table, without changing checker source.
The [glibc gmon manual](https://sourceware.org/glibc/manual/latest/html_node/gmon-Tunables.html)
describes this failure and configurable table sizes. The retry wrapper and
its setting are preserved in the bundle. Original baseline measurements above are retained for comparison;
the retry includes a fresh baseline solely as part of the unchanged two-stage
driver. Both retry proofs pass and both function reports are complete.
Archival dependency `382602` was cancelled after the initial profiling
failure; replacement archival job `382608` depends on the retry.

## Evidence and reproduction

The {download}`ledger <../coverage/checker-gprof-20261111.json>` records original
baselines, selected instrumented results, top symbols, all input/binary/source
hashes, limits and the initial Clock7 profiling-only failure. The
{download}`profile and replay bundle <../../benchmarks/checker-regressions/checker-gprof-20261111/results.tar.gz>` is 2,234,594 bytes, SHA-256
`268a0c15688e507d8dc252b514740630d16cbfd592df48655034f51e605bfdb1`.
All 180 payload hashes/sizes and the frozen workflow hashes were checked after
collection. The bundle retains original SMT/CNF/certificate inputs, all time
and gprof reports, gmon records, raw diagnostics, both build reports, frozen
scripts and the Clock7 retry wrapper. It omits binary challenges, executable
binaries, expanded raw proofs and checker implementation. Private text metadata
prefixes are normalized; input and gmon bytes are unchanged. Raw gmon records
need the exact matching executable for reliable symbol resolution; the rendered
function reports are directly readable without rebuilding.

Build and proof workers use node `node-01`, an Intel Xeon E5-2690 v4 at 2.60 GHz;
GCC is 11.4.1, GNU time 1.9 and gprof 2.35.2. The
{download}`final scheduler receipt <../../benchmarks/checker-regressions/checker-gprof-20261111/accounting.tsv>` includes every setup, profiling, retry and archival job.
Initial workers 0 and 1, the Clock7 retry and archive `382608` finish `0:0`;
initial worker 2's nonzero status is exclusively missing profiler output after
successful proof checks. Four uninstrumented and four instrumented pipelines
pass in total, counting the Clock7 retry. Three cases supply six selected
complete function reports. There are no checker timeouts or checker errors in
these proof runs. All proof workers pass three pure workflow tests.
The canonical record is in {download}`the run log <../run-log.json>`.

Reproduce using the source-pinned build/profiling wrappers above, the
{download}`three-case selection <../../benchmarks/checker-regressions/checker-medium-comparison-20261111/selection.json>` and the bundle's `inputs/` tree as saved-export root.
Preserve the qualified checker while building the detached profiling copy.
Provide the required environment listed in each wrapper, including the pinned
SAT executable and a shared-storage GNU time copy with its matching hash.
For Clock7 use the preserved retry wrapper or its `GLIBC_TUNABLES` setting.
Source scripts and exact invocations are in the bundle and ledger; reusing the
inputs does not require solving or exporting again.

## Sources and implementation status

This implements ordinary gprof instrumentation and per-process resource
measurement, rather than a checker optimization. The
[GNU compilation instructions](https://sourceware.org/binutils/docs/gprof/Compiling.html)
require `-pg` at compile and link time. The
[execution instructions](https://sourceware.org/binutils/docs/gprof/Executing.html)
describe normal-exit gmon output and `GMON_OUT_PREFIX`.
The manual's [sampling discussion](https://sourceware.org/binutils/docs/gprof/Sampling-Error.html)
and [implementation notes](https://sourceware.org/binutils/docs/gprof/Implementation.html)
explain sampling/counting distinctions and why blocked I/O time is not sampled.
No checker algorithm is implemented or changed here.
