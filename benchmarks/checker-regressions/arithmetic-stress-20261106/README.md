# Arithmetic stress replay cases, 6 November 2026

These seven unchanged SMT-LIB sources come from SMT-LIB's QF_LRA/QF_LIA benchmark families. Original paths, historical native UNSAT measurements, hashes and observed failures are in case.json and manifest.json. Cases were selected for small source/certificate sizes within each failure category; they are not delta-minimized. Unsupported syntax and missing source-level witnesses are separate from invalid arithmetic or solver wrong answers. No checker implementation was inspected or modified for this pilot.

Pinned Micromath: LRA 5f22700410c0e37044c03557dbd21e009303b7ba; LIA 8f9cff132140462b141183ddb9cb88a9beb80a4f. Native producer a512315901c56ba95c555935a0bd3a7f092c9c00, default preprocessing, --export-cnf-reduction=core. Case logs retain the original run paths for diagnosis. Native files are exact exporter bytes. LIA micromath-v2.sexp changes only byte 28 from 3 to 2; native.sexp is preserved.

Run external solver/checker commands on an allocated compute node, from the appropriate Micromath checkout after building its documented targets. Set CASE to an absolute extracted case directory, CADICAL to the LRAT-capable executable, and ARITH to lra or lia:

    set -o pipefail
    ./build-axioms --fragment finitary axioms.umd
    ./smt/build-${ARITH}-challenge axioms.umd "$CASE/input.smt2" challenge.umd
    # Use native.sexp for LRA; micromath-v2.sexp for LIA.
    CERT="$CASE/native.sexp"
    if [ "$ARITH" = lia ]; then CERT="$CASE/micromath-v2.sexp"; fi
    ./smt/prove-${ARITH} challenge.umd "$CERT" - --raw --cadical "$CADICAL" |
      ./check-solution challenge.umd -

Both producer and kernel must exit zero and the kernel must print exactly `challenge proven`. The pilot used 3 GiB address space and 256 MiB maximum file size per child, 30 s for challenge/export, 60 s for proof+kernel, 15 s for Z3. Raw proofs were streamed and never saved. Failed challenges can be regenerated; platform-dependent challenge dumps and upstream code are not in this archive.

Expected failure categories:
- lra-ite-variable: unknown source Real variable `$primo_ite_0` in the exported signature.
- lia-source-refutation: supplied conflicts do not refute Micromath's original-source Boolean abstraction; `s SATISFIABLE` refers to that abstraction, not the arithmetic source.
- lia-sort-mismatch: source/certificate sort mismatch.
- lia-closed-boolean: Logic failure: evaluation needs a closed Boolean term.
- lia-nesting: source challenge builder exceeds nesting 512.
- lra-defined-function: source challenge builder rejects define-fun.
- lia-reconstruction-limit: native main solver proves UNSAT, but bounded integer proof reconstruction reports unknown and writes no certificate. Replay this producer-side case with:

      primo --quiet --stats --export-cnf native.sexp --export-cnf-reduction=core "$CASE/input.smt2"

The six checker-side categories are feedback artifacts, not accepted proofs. The reconstruction case is a producer limitation. No checker timeout, memory kill or file-size cap was observed in this pilot. These cases have not been rerun separately after packaging; their bytes and original results are retained from SLURM array 362720.
