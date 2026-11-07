# Micromath QF_LRA staged-export feedback

The manifest records saved proof locations, raw/compressed SHA256 and byte
counts, complete/partial status, progress, stage results and pinned binaries.
All listed captured gzip files were fully decompressed in SLURM and their CRC,
raw byte count and raw SHA256 checked. Full proofs remain at the recorded
cluster artifact paths; they are too large for this small feedback bundle.

five-clock contains the independently audited original input, exported-CNF,
native CORE certificate, exact saved challenge compressed with gzip, and ONLY
the first 1 MiB of the exported binary proof (proof-prefix.umd.gz). That prefix
is a sample, NOT a complete proof and must not be counted as verification.
The complete five-clock proof was accepted by the pinned independent kernel;
see the captured stage summaries for the other cases and historical caps.
Source-to-CNF preprocessing/clausification is still trusted.

On an allocated compute node, from the pinned Micromath root, set CASE to the
absolute extracted five-clock directory and CADICAL to the pinned LRAT build:

    set -euo pipefail
    gzip -dc "$CASE/challenge.umd.gz" > challenge.umd
    ./smt/prove-lra challenge.umd "$CASE/native.sexp" - --raw --cadical "$CADICAL" |
      gzip -1 > proof.umd.gz
    gzip -dc proof.umd.gz | ./check-solution challenge.umd - > kernel.out
    grep -Fxq 'challenge proven' kernel.out
    ! grep -Fxq 'Inconsistency proven!' kernel.out

Use external time/memory/disk bounds matching the manifest. Generation and
replay are separate phases and include compression/decompression/I/O overhead.
No checker internals were inspected/modified and no upstream fetch occurred.
No delta minimization was performed.
