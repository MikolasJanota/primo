#!/usr/bin/env bash
#SBATCH --job-name=primo-certora-paper
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=12G
#SBATCH --time=00:08:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
export DEBUGINFOD_URLS=''
python3 "${PRIMO_CERT_METHODS:?}/run.py"
