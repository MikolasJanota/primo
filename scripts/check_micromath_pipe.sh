#!/usr/bin/env bash
# Stream a raw Micromath proof into an independent kernel process.
set -euo pipefail
if [[ ${1:-} != --worker ]]; then
  if (( $# < 5 || $# > 6 )); then
    printf 'Usage: %s MICROMATH_DIR CHALLENGE INPUT LRAT FRESH_LOG_DIR [SECONDS]\n' "$0" >&2
    exit 2
  fi
  exec timeout --signal=TERM --kill-after=2s "${6:-135}s" bash "$0" --worker "$@"
fi
shift
checker=$1
challenge=$2
input=$3
lrat=$4
logs=$5
time_command=${MICROMATH_TIME:-/usr/bin/time}
mkdir -- "$logs"
set +e
"$time_command" --verbose --output="$logs/producer-time.txt" \
  prlimit --as=3221225472 --fsize=1073741824 -- \
  "$checker/smt/prove-lrat" "$challenge" "$input" "$lrat" - --raw \
  2> "$logs/producer.err" |
  "$time_command" --verbose --output="$logs/kernel-time.txt" \
    prlimit --as=3221225472 --fsize=1073741824 -- \
    "$checker/check-solution" "$challenge" - \
    > "$logs/kernel.out" 2> "$logs/kernel.err"
statuses=("${PIPESTATUS[@]}")
set -e
printf '%s %s\n' "${statuses[0]}" "${statuses[1]}" > "$logs/exit-status.txt"
cat -- "$logs/producer.err" "$logs/kernel.err" >&2
cat -- "$logs/kernel.out"
if (( statuses[0] != 0 || statuses[1] != 0 )); then
  exit 1
fi
grep -Fxq 'challenge proven' "$logs/kernel.out"
if grep -Fxq 'Inconsistency proven!' "$logs/kernel.out"; then
  exit 1
fi
