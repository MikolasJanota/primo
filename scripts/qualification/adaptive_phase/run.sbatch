#!/usr/bin/env bash
#SBATCH --job-name=primo-adaptive-phase-probe
#SBATCH --partition=compute
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A-%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
mkdir -p "${PRIMO_PROBE_OUT:?}"
{ hostname; lscpu; } > "$PRIMO_PROBE_OUT/node-${SLURM_ARRAY_TASK_ID:-0}.txt"
read -r -a profiles <<< "${PRIMO_PROBE_ARMS:?}"
python3 "${PRIMO_PROBE_RUNNER:-scripts/qualification/adaptive_phase/run.py}" \
 --expected-git "${PRIMO_EXPECTED_GIT:?}" \
 --solver "${PRIMO_PROBE_SOLVER:-build-slurm-rel/primo}" --root "${PRIMO_CORPUS_ROOT:?}" \
 --manifest "${PRIMO_PROBE_MANIFEST:?}" --out "$PRIMO_PROBE_OUT/task-${SLURM_ARRAY_TASK_ID:-0}" \
 --timeout "${PRIMO_PROBE_TIMEOUT:-15}" --profiles "${profiles[@]}" \
 --shards "${PRIMO_PROBE_SHARDS:-1}" --shard "${SLURM_ARRAY_TASK_ID:-0}" \
 --rounds "${PRIMO_PROBE_ROUNDS:-1}" --trace "${PRIMO_PROBE_TRACE_LIMIT:-0}"
