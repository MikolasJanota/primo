#!/usr/bin/env bash
#SBATCH --job-name=primo-euf-smtcomp-audit
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 "${PRIMO_EUF_METHODS:?}/audit.py" --phase "${PRIMO_EUF_AUDIT_PHASE:?}" \
 --configs "${PRIMO_EUF_CONFIGS:?}" --rows "${PRIMO_EUF_ROWS:?}" \
 --out "${PRIMO_EUF_AUDIT_OUT:?}"
