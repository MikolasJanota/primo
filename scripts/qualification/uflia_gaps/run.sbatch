#!/usr/bin/env bash
# Focused native timings/profiles; model arrays depend on the complete timing stage.
#SBATCH --job-name=primo-uflia-gap
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=12G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export DEBUGINFOD_URLS=''
export PATH="$PWD/contrib/local/bin:$PATH"
exec python3 "${PRIMO_GAP_RUNNER:?}"
