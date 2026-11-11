#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-smtcomp2026
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:25:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_COMP_METHODS:?}/run.py" "${PRIMO_COMP_PHASE:?}" \
 --manifest "${PRIMO_COMP_MANIFEST:?}" --slot "${SLURM_ARRAY_TASK_ID:?}" \
 --root "${PRIMO_CORPUS_ROOT:?}" --solver "${PRIMO_COMP_SOLVER:?}" --out "${PRIMO_COMP_OUT:?}"
