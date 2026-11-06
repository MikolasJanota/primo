# LIA envelope compatibility reproduction

This is one original integer problem and the exact 462-byte native Primo
certificate from source a5123159 (export job 357090). The native artifact has a
version-3 envelope. The second certificate changes one byte in that header,
from 3 to 2. All source variables, integer cuts and other bytes are identical.

With Micromath 8f9cff132140462b141183ddb9cb88a9beb80a4f, the native version-3 file
is rejected at line 2 with `unexpected or duplicate section`. The version-2 copy
is accepted and its proof independently reaches `challenge proven`. This tests
an envelope mismatch, not missing integer arithmetic. The input's real
relaxation is satisfiable at x=1/2, y=1, so integer rounding is essential.

After building the upstream LIA tools, run from the Micromath repository root
(replace the two paths):

```bash
set -o pipefail
case_dir=/path/to/lia-envelope-20261106
cadical=/path/to/cadical
mkdir -p build/lia-envelope
./build-axioms --fragment finitary build/lia-envelope/axioms.umd
./smt/build-lia-challenge build/lia-envelope/axioms.umd \
  "$case_dir/input.smt2" build/lia-envelope/challenge.umd
./smt/prove-lia build/lia-envelope/challenge.umd "$case_dir/micromath-v2.sexp" \
  - --raw --cadical "$cadical" | \
  ./check-solution build/lia-envelope/challenge.umd -
```

Replacing `micromath-v2.sexp` by `native-v3.sexp` reproduces the parser rejection
at the pinned revision. No checker code was patched. Proofs were streamed
without saving raw proof files. The successful compatibility check ran in
SLURM 357146 under a 60-second stage deadline and 3 GiB per-process address cap.
The failed direct-native attempt was 357144. This archive contains no proof
binary, external source, connection configuration or credentials.
