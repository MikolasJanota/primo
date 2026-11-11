#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-model-unit-compare
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 "${PRIMO_UNIT_METHODS:?}/compare.py" "${PRIMO_UNIT_PHASE:?}" \
 --slot "${SLURM_ARRAY_TASK_ID:?}" --manifest "${PRIMO_UNIT_CASES:?}" \
 --root "${PRIMO_CORPUS_ROOT:?}" --candidate build-slurm-rel/primo \
 --baseline "${PRIMO_BASELINE_SOLVER:?}" --out "${PRIMO_UNIT_OUT:?}/timings"
