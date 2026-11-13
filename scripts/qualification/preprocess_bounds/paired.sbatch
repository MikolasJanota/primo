#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-paired
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 scripts/qualification/lia_hybrid/run.py "${PRIMO_BOUND_PHASE:?}" \
 --config "${PRIMO_BOUND_CONFIG:?}" --slot "${SLURM_ARRAY_TASK_ID:?}"
