#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-repairs-prefix
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:25:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
config="${PRIMO_PREFIX_CONFIGS:?}/config-${SLURM_ARRAY_TASK_ID:?}.json"
python3 scripts/qualification/lia_hybrid/run.py timing --config "$config" --slot 0
# These are diagnostic prefixes, not full-runtime measurements. SAT prefixes
# still require their own exact-option independent model validation.
python3 scripts/qualification/lia_hybrid/run.py models --config "$config" --slot 0
