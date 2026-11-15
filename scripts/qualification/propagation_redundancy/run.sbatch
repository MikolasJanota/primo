#!/usr/bin/env bash
#SBATCH --job-name=primo-propagation-redundancy
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
case "${PRIMO_AUDIT_PHASE:?}" in timing|models) ;; *) exit 2 ;; esac
for ((repetition=0; repetition<${PRIMO_AUDIT_REPETITIONS:-3}; repetition++)); do
 python3 scripts/qualification/lia_hybrid/run.py "$PRIMO_AUDIT_PHASE" \
  --config "${PRIMO_AUDIT_CONFIGS:?}/case-${SLURM_ARRAY_TASK_ID:?}/config-$repetition.json" --slot 0
done
