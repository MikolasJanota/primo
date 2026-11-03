#!/usr/bin/env bash
#SBATCH --job-name=primo-micromath-core
#SBATCH --array=0-3%4
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err

# Prepare the immutable manifest in SLURM and run the canonical --check-only
# preflight before submission. Override --array/time/memory as needed at sbatch.
set -euo pipefail
: "${PILOT_PYTHON:?set Python >=3.10}"
: "${PILOT_SCRIPT:?set the tracked micromath_stress.py snapshot}"
: "${PRIMO_BINARY:?set the prepared Primo binary}"
: "${PRIMO_EXPECTED_GIT:?set its printed git revision}"
: "${MICROMATH_DIR:?set the built Micromath directory}"
: "${MICROMATH_GIT:?set its archived revision}"
: "${CADICAL_BINARY:?set the built LRAT-capable CaDiCaL executable}"
: "${CADICAL_GIT:?set its archived revision}"
: "${REFERENCE_BINARY:?set the Z3 executable}"
: "${QF_UF_CORPUS:?set the existing nonincremental QF_UF corpus root}"
: "${PILOT_OUT:?set a fresh absolute sweep results root}"
: "${PILOT_MANIFEST:?set the hash-pinned known-UNSAT manifest}"
: "${SLURM_ARRAY_TASK_ID:?submit as an array indexed from zero}"
shard_count="${CORE_SHARD_COUNT:-4}"
if (( shard_count < 1 || SLURM_ARRAY_TASK_ID >= shard_count )); then
  echo 'invalid shard count or array index' >&2
  exit 2
fi
"$PILOT_PYTHON" "$PILOT_SCRIPT" --corpus "$QF_UF_CORPUS" \
  --primo "$PRIMO_BINARY" --expected-git "$PRIMO_EXPECTED_GIT" \
  --micromath "$MICROMATH_DIR" --cadical "$CADICAL_BINARY" \
  --reference "$REFERENCE_BINARY" --manifest "$PILOT_MANIFEST" \
  --out "$PILOT_OUT/shard-$SLURM_ARRAY_TASK_ID" \
  --modes core --shard-count "$shard_count" --shard-index "$SLURM_ARRAY_TASK_ID" \
  --workers 4 --reference-timeout "${CORE_REFERENCE_TIMEOUT:-20}" \
  --mode-budget "${CORE_PROFILE_BUDGET:-60}" \
  --export-timeout "${CORE_EXPORT_TIMEOUT:-30}" --compact-summary \
  --keep-success-artifacts "${CORE_KEEP_SUCCESS_ARTIFACTS:-certificates}"
