#!/usr/bin/env bash
# Source and configuration are frozen before the first native timing task.
#SBATCH --job-name=primo-uniform120
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
ulimit -v 8388608
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
exec python3 "${PRIMO_LONG_DRIVER:?}" "${PRIMO_LONG_PHASE:?}" --workspace "$PWD" \
 --out "${PRIMO_LONG_OUT:?}" --references "${PRIMO_REFERENCE_CAMPAIGN:?}"
