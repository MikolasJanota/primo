#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-starter-verify
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Verify the actual forwardable archive with the external SAT solver in SLURM.
set -euo pipefail
cd "${PRIMO_WORKDIR:?qualified workspace}"
: "${PRIMO_BUNDLE_ARCHIVE:?starter tar.gz path}"
: "${PRIMO_SAT_SOLVER:?external DIMACS CaDiCaL executable}"
artifacts="results/lra-starter-verify-$SLURM_JOB_ID"
mkdir -p "$artifacts"
cp "$PRIMO_BUNDLE_ARCHIVE" "$artifacts/lra-starter.tar.gz"
sha256sum "$artifacts/lra-starter.tar.gz" > "$artifacts/archive.sha256"
tar -xzf "$artifacts/lra-starter.tar.gz" -C "$artifacts"
python3 "$artifacts/lra-starter/verify.py" --sat-solver "$PRIMO_SAT_SOLVER" \
  > "$artifacts/summary.json"
