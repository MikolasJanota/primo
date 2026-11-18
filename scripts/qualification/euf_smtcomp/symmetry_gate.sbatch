#!/usr/bin/env bash
#SBATCH --job-name=primo-symmetry-composition-gate
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:25:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_SYMMETRY_METHODS:?}/symmetry_gate.py" --config "${PRIMO_SYMMETRY_CONFIG:?}" \
 --slot "${SLURM_ARRAY_TASK_ID:?}"
