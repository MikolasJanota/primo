#!/usr/bin/env bash
#SBATCH --job-name=primo-micromath-pilot
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:25:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Run slurm_prepare_benchmark_workspace.py --check-only before submission.
# The corpus is an existing read-only SMT-LIB copy; no archive is retransmitted.
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
: "${PILOT_OUT:?set a fresh absolute results directory}"
manifest_option=()
if [[ -n ${PILOT_MANIFEST:-} ]]; then
  manifest_option=(--manifest "$PILOT_MANIFEST")
fi
"$PILOT_PYTHON" "$PILOT_SCRIPT" --corpus "$QF_UF_CORPUS" \
  --primo "$PRIMO_BINARY" --expected-git "$PRIMO_EXPECTED_GIT" \
  --micromath "$MICROMATH_DIR" --cadical "$CADICAL_BINARY" \
  --reference "$REFERENCE_BINARY" --out "$PILOT_OUT" \
  --workers 4 --reference-timeout 20 --mode-budget 60 "${manifest_option[@]}"
