#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-paired
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=10G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_HYBRID_METHODS:?}/run.py" "${PRIMO_HYBRID_PHASE:?}" \
 --config "${PRIMO_HYBRID_CONFIG:?}" --slot "${SLURM_ARRAY_TASK_ID:?}"
