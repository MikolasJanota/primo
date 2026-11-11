#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-coefficients
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${COEFFICIENT_WORKFLOW:?frozen Python workflow}"
: "${COEFFICIENT_GIT:?workflow commit}"
: "${FARKAS_Z3:?reference and optimization executable}"
: "${FARKAS_CADICAL:?SAT executable}"
: "${FARKAS_MICROMATH:?pinned checker directory}"
: "${FARKAS_MICROMATH_GIT:?checker revision}"
: "${FARKAS_CHECKER_BUILD:?checker build hashes}"
: "${COEFFICIENT_LRA_INPUTS:?fourteen retained LRA input certificates}"
cd "$PRIMO_WORKDIR"
python3 -m unittest discover -s "$COEFFICIENT_WORKFLOW/tests/python" -p 'test_*coefficients.py'
if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then mode=generated; else mode=kernel; fi
python3 "$COEFFICIENT_WORKFLOW/scripts/qualify_arithmetic_coefficients.py" \
  --mode "$mode" --out "$PWD/results/arithmetic-coefficients-$SLURM_ARRAY_JOB_ID/$mode" \
  --workflow-git "$COEFFICIENT_GIT" --z3 "$FARKAS_Z3" --cadical "$FARKAS_CADICAL" \
  --micromath "$FARKAS_MICROMATH" --micromath-git "$FARKAS_MICROMATH_GIT" \
  --build-report "$FARKAS_CHECKER_BUILD" --lra-inputs "$COEFFICIENT_LRA_INPUTS" \
  --examples "$COEFFICIENT_WORKFLOW/docs/examples"
