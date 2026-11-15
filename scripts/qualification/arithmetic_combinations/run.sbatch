#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-combinations
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 scripts/qualification/arithmetic_combinations/run.py "${PRIMO_COMB_PHASE:?}" \
 --campaign "${PRIMO_COMB_CAMPAIGN:?}" --repetition "${PRIMO_COMB_REPETITION:?}" \
 --slot "${SLURM_ARRAY_TASK_ID:?}"
