#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-abort-trace
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 "${PRIMO_BOUND_ABORT_RUNNER:?}" --config "${PRIMO_BOUND_CONFIG:?}" \
 --out "${PRIMO_BOUND_ABORT_OUT:?}"
