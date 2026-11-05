#!/usr/bin/env bash
#SBATCH --job-name=primo-phase-region-models
#SBATCH --partition=compute
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_PROBE_REPLAYER:-scripts/qualification/phase_region/replay.py}" \
 --parent "${PRIMO_PROBE_PARENT:?}/task-${SLURM_ARRAY_TASK_ID:-0}" \
 --root "${PRIMO_CORPUS_ROOT:?}" --solver "${PRIMO_PROBE_SOLVER:-build-slurm-rel/primo}" \
 --out "${PRIMO_PROBE_REPLAY_OUT:?}/task-${SLURM_ARRAY_TASK_ID:-0}"
