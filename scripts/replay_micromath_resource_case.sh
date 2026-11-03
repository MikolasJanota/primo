#!/usr/bin/env bash
# Bundled beside saved Micromath inputs; reproduces only proof generation.
set -euo pipefail
if (( $# < 1 || $# > 2 )); then
  printf 'Usage: %s MICROMATH_DIR [FRESH_OUTPUT_DIR]\n' "$0" >&2
  exit 2
fi
checker="$(cd -- "$1" && pwd)"
here="$(cd -- "$(dirname -- "$0")" && pwd)"
output="${2:-$here/replay-output}"
test -x "$checker/smt/prove-lrat"
(cd -- "$here" && sha256sum --check SHA256SUMS)
mkdir -- "$output"
if prlimit --as=3221225472 --fsize=268435456 -- \
  timeout --signal=TERM --kill-after=2s "${PROOF_TIMEOUT_SECONDS:-30}s" \
  "$checker/smt/prove-lrat" "$here/challenge.umd" "$here/input.primo" \
  "$here/problem.lrat" "$output/solution.ump" \
  > "$output/proof.out" 2> "$output/proof.err"; then
  status=0
else
  status=$?
fi
printf 'prove-lrat exit: %s\nLogs and generated output: %s\n' "$status" "$output"
exit "$status"
