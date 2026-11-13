#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-diagnose
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 "${PRIMO_BOUND_DIAGNOSE_RUNNER:?}" --config "${PRIMO_BOUND_CONFIG:?}" \
 --out "${PRIMO_BOUND_DIAGNOSE_OUT:?}" --slots 213 229
