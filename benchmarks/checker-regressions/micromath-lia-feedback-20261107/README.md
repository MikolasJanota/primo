# Smallest freshly reproduced Micromath LIA error

This is the 014-closed-boolean subset of the independently validated full
feedback archive (SLURM audit 362757). It preserves all case payloads and logs
byte-for-byte, plus standalone replay/validation helpers. The larger case 019
has the same reproduced error and is omitted here; three historical resource
cases are in micromath-lia-feedback.tar.gz.

Run on an allocated Linux compute node after building the pinned Micromath
revision 8f9cff132140462b141183ddb9cb88a9beb80a4f:

    bash replay.sh /path/to/Micromath /path/to/cadical 014-closed-boolean /fresh/log/directory

Observed producer diagnostic: Logic failure: evaluation needs a closed Boolean term.
Both producer and kernel exit 1. The source statement is exported-cnf.smt2;
micromath-v2.sexp is the explicit one-byte envelope copy, native.sexp is intact.

Yices 2.7.0 confirmed UNSAT of original and exported-CNF statements. Independent
Python arithmetic/type checks and CaDiCaL confirmed propositional UNSAT. This
case has zero theory conflicts, so no integer-cut proof is being supplied.
Regenerated statement and header-copy checks matched exact bytes. These checks
found no producer-side defect; they do not prove preprocessing or checker
soundness. No checker implementation was inspected or changed.

The replay uses 30-second challenge and 60-second proof/kernel process deadlines,
3 GiB address space and 256 MiB file-size caps per process. It streams --raw proof
output and does not save raw proofs. Challenge dumps are regenerated, not bundled.
The instance is unchanged, not delta-minimized. manifest.json records every
payload hash and the parent full-archive SHA256.

Full bundle: five validated cases, including two freshly reproduced errors and three historical limits.
