#!/usr/bin/env bash
# Audits and final plotting have no solver timing work.
#SBATCH --job-name=primo-uniform120-audit
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
ulimit -v 8388608
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_LONG_REPORT:?}" "${PRIMO_LONG_STAGE:?}" --workspace "$PWD" --out "${PRIMO_LONG_OUT:?}"
