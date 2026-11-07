#!/usr/bin/env bash
# Replay one exported-CNF feedback case without storing the raw proof.
set -euo pipefail
if (( $# < 3 || $# > 4 )); then
  echo 'usage: replay.sh MICROMATH_ROOT CADICAL CASE_DIR [FRESH_OUTPUT_DIR]' >&2
  exit 2
fi
checker=$(realpath "$1")
cadical=$(realpath "$2")
case_dir=$(realpath "$3")
if (( $# == 4 )); then
  output=$(realpath -m "$4")
  mkdir "$output"
else
  output=$(mktemp -d)
fi
cd "$checker"
printf 'logs: %s\n' "$output"
timeout --kill-after=2s 30s prlimit --as=3221225472 --fsize=268435456 -- \
  ./build-axioms --fragment finitary "$output/axioms.umd" \
  > "$output/axioms.out" 2> "$output/axioms.err"
timeout --kill-after=2s 30s prlimit --as=3221225472 --fsize=268435456 -- \
  ./smt/build-lia-challenge "$output/axioms.umd" "$case_dir/exported-cnf.smt2" \
  "$output/challenge.umd" > "$output/challenge.out" 2> "$output/challenge.err"
set +e
timeout --kill-after=2s 60s prlimit --as=3221225472 --fsize=268435456 -- \
  ./smt/prove-lia "$output/challenge.umd" "$case_dir/micromath-v2.sexp" \
  - --raw --cadical "$cadical" 2> "$output/producer.err" |
timeout --kill-after=2s 60s prlimit --as=3221225472 --fsize=268435456 -- \
  ./check-solution "$output/challenge.umd" - \
  > "$output/kernel.out" 2> "$output/kernel.err"
statuses=("${PIPESTATUS[@]}")
set -e
printf '%s %s\n' "${statuses[0]}" "${statuses[1]}" > "$output/exit-status.txt"
cat "$output/exit-status.txt" "$output/producer.err" "$output/kernel.out" "$output/kernel.err"
if (( statuses[0] != 0 || statuses[1] != 0 )); then exit 1; fi
grep -Fxq 'challenge proven' "$output/kernel.out"
! grep -Fxq 'Inconsistency proven!' "$output/kernel.out"
