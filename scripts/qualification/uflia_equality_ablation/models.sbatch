#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-models
#SBATCH --partition=compute
#SBATCH --array=0-23%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
python3 "${PRIMO_EQ_REPLAY:?}" --parent "${PRIMO_EQ_PARENT:?}/shard-$slot" \
 --root "${PRIMO_CORPUS_ROOT:?}" --out "${PRIMO_EQ_OUT:?}/shard-$slot" \
 --timeout "${PRIMO_EQ_MODEL_TIMEOUT:-120}" --dolmen-timeout 30
