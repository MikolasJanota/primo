#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-repairs-followup
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
case "${PRIMO_FOLLOWUP_PHASE:?}" in timing|models) ;; *) exit 2 ;; esac
for repetition in 0 1 2; do
 python3 scripts/qualification/lia_hybrid/run.py "$PRIMO_FOLLOWUP_PHASE" \
  --config "${PRIMO_FOLLOWUP_CONFIGS:?}/case-${SLURM_ARRAY_TASK_ID:?}/config-$repetition.json" --slot 0
done
