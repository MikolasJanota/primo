#!/usr/bin/env bash
# Metadata-only export; submit after the final qualification audit succeeds.
#SBATCH --job-name=primo-uniform120-export
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
exec python3 "${PRIMO_EXPORT_OWNER:?}" --out "${PRIMO_LONG_OUT:?}" --destination "${PRIMO_PUBLIC_OUT:?}"
