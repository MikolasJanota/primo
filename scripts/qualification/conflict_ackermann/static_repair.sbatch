#!/usr/bin/env bash
#SBATCH --job-name=primo-conflict-ack-static-repair
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_VERIFY_BIN:?}:${PRIMO_CMAKE_BIN:?}:$PATH"
out="${PRIMO_ACK_STATIC_REPAIR_OUT:?}"
mkdir "$out"
# Only these bodies changed after the full141-TU inventory; main.cpp
# consumes the regenerated native revision header and is rechecked as well.
pattern='/(src/theory/equality/flat_congruence_closure.cpp|src/smt/conflict_ackermann.cpp|src/main.cpp)$'
for scan in tidy includes; do
 args=()
 if [[ $scan == includes ]]; then args+=('-checks=-*,clang-diagnostic-*,misc-include-cleaner'); fi
 status=0
 python3.11 "${PRIMO_RUN_CLANG_TIDY:?}" -p "${PRIMO_ACK_STATIC_DATABASE:?}" -j 4 \
  -clang-tidy-binary "${PRIMO_VERIFY_BIN:?}/clang-tidy" "${args[@]}" \
  -export-fixes "$out/$scan-fixes/" "$pattern" > "$out/$scan.log" 2>&1 || status=$?
 printf '%s\n' "$status" > "$out/$scan.exit"
done
