#!/usr/bin/env bash
#SBATCH --job-name=primo-euf-smtcomp-triage
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 "${PRIMO_TRIAGE_METHODS:?}/triage.py" --config "${PRIMO_TRIAGE_CONFIG:?}"
