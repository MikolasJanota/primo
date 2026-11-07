# QF_LRA larger-sweep checker feedback

These 3 original failed cases passed fresh Yices UNSAT checks on both
original and exported-CNF statements, exact rational witness/SAT checking, saved
payload hashes and regenerated-statement byte equality. Audit SLURM 362769.
No producer-side defect was found by these checks. Preprocessing and checker
soundness are not formally established by the audit.

The checker observations are historical results from array 362761, pinned
Micromath 5f22700410c0e37044c03557dbd21e009303b7ba. Their checker stages were not repeated during this audit.
Timeouts are workload-and-budget feedback, not correctness bugs. No checker
implementation was inspected/changed and no upstream fetch occurred.
These sources and native certificates are unchanged, not delta-minimized.
No raw proofs, checker binaries or challenge dumps are bundled.

From an allocated compute node and the pinned Micromath root, with CASE set
to an absolute extracted case directory and CADICAL to an LRAT-capable build:

    set -o pipefail
    ./build-axioms --fragment finitary axioms.umd
    ./smt/build-lra-challenge axioms.umd "$CASE/exported-cnf.smt2" challenge.umd
    ./smt/prove-lra challenge.umd "$CASE/native.sexp" - --raw --cadical "$CADICAL" |
      ./check-solution challenge.umd -

Original limits: 30 seconds for challenge building, one overall 60-second
proof/kernel deadline, 3 GiB address space and 256 MiB file limit per child.
The source being checked is exported-cnf.smt2, not the original input.smt2.
The original source is retained for provenance and independent reference checks.
Only both exits zero and exact challenge proven count as a successful proof.
See case.json and original-logs for the actual observed stage and diagnostics.
