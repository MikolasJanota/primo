#!/usr/bin/env bash
# Metadata-only certification and archival; no solver is run here.
#SBATCH --job-name=primo-uniform120-recover
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
ulimit -v 8388608
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_LONG_RECOVER:?}" --workspace "$PWD" --out "${PRIMO_LONG_OUT:?}"
