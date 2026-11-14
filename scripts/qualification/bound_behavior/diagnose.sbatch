#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-behavior
#SBATCH --cpus-per-task=1
#SBATCH --mem=16G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_BEHAVIOR_RUNNER:?}" --config "${PRIMO_BEHAVIOR_CONFIG:?}" \
  --mode "${SLURM_ARRAY_TASK_ID:?}"
