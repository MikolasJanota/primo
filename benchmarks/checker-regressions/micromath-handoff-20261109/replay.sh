#!/usr/bin/env bash
set -euo pipefail
base=$(cd -- "$(dirname -- "$0")" && pwd)
case "${1:---list}" in
  --list)
    printf '%s\n' 'wisa5: definition index allocation diagnostic' 'clock7: overall proof/kernel deadline' 'lia-oom: producer out of memory' 'challenge-p15: challenge-builder deadline; no proof attempted'
    exit 0;;
  wisa5)
    exec bash "$base/packets/definition-index/replay.sh" wisa-definition-allocation "${2:?fresh output directory}";;
  clock7)
    exec bash "$base/packets/proof-limits/replay.sh" lra-clock-7 "${2:?fresh output directory}";;
  lia-oom)
    exec bash "$base/packets/proof-limits/replay.sh" lia-native-read-oom "${2:?fresh output directory}";;
  challenge-p15)
    exec bash "$base/packets/challenge-deadline/replay.sh" "${2:?fresh output directory}";;
  *) printf '%s\n' 'Unknown case. Use --list.' >&2; exit 2;;
esac
