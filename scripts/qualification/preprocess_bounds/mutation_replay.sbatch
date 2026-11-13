#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-mutation-replay
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 "${PRIMO_BOUND_MUTATION_RUNNER:?}" --slot "${SLURM_ARRAY_TASK_ID:?}" \
 --mutants "${PRIMO_BOUND_MUTANT_ROOT:?}" --out "${PRIMO_BOUND_TIGHTENING_OUT:?}"
