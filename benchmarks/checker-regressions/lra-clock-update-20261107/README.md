# Remaining clock proof-volume feedback at updated Micromath

Pinned checker: cfd36f46e756f080295d469753c9f2989dab1fee.
Both unchanged, independently audited inputs still reach the overall60s live
proof/kernel deadline in SLURM362914. Both complete with separate300s export
and300s gzip-fed kernel stages in362921, with both exits0 and challenge proven.
These are proof-volume/budget observations, not correctness failures.
The5clock proof has171076770 arithmetic steps;6clock has273514122.
No checker implementation was inspected or modified. Primo was not rerun.
Original-to-CNF preprocessing/clausification remains trusted.

Each directory contains original input.smt2, the actual exported-cnf.smt2 being
proved, the unchanged native.sexp CORE certificate, case.json and new capture
logs/progress. Full gzip proofs remain in the recorded cluster result paths;
they are omitted here. No challenge from the old checker is reused: rebuild
with the pinned updated checker from exported-cnf.smt2.

On an allocated compute node, from that Micromath root, set CASE to an absolute
extracted case directory and CADICAL to the pinned LRAT-capable CaDiCaL. Use a
fresh output directory per case. The original replay bounds are:

    set -euo pipefail
    export CASE CADICAL
    timeout --kill-after=2s 30s prlimit --as=3221225472 --fsize=268435456 -- \
      ./build-axioms --fragment finitary axioms.umd
    timeout --kill-after=2s 30s prlimit --as=3221225472 --fsize=268435456 -- \
      ./smt/build-lra-challenge axioms.umd "$CASE/exported-cnf.smt2" challenge.umd
    replay() {
      prlimit --as=3221225472 --fsize=268435456 -- \
        ./smt/prove-lra challenge.umd "$CASE/native.sexp" - --raw --cadical "$CADICAL" \
        2> producer.err |
      prlimit --as=3221225472 --fsize=268435456 -- \
        ./check-solution challenge.umd - 2> kernel.err
    }
    export -f replay
    timeout --kill-after=2s "${PROOF_SECONDS:-60}s" bash -o pipefail -c replay > kernel.out
    grep -Fxq 'challenge proven' kernel.out
    ! grep -Fxq 'Inconsistency proven!' kernel.out

The default60s deadline is expected to stop these resource cases on the
reported machine. PROOF_SECONDS=300 allows a longer live replay, but the
recorded successful diagnostics used separate export and kernel stages;
see case.json for exact commands and raw/gzip bounds. This packet does not
claim execution of the manual snippet or a new300s live-pipeline measurement.
