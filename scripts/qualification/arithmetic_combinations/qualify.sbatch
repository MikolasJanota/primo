#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-combinations-qualify
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 scripts/qualification/arithmetic_combinations/qualify.py "${PRIMO_COMB_PHASE:?}" \
 --campaign "${PRIMO_COMB_CAMPAIGN:?}" --slot "${SLURM_ARRAY_TASK_ID:?}"
