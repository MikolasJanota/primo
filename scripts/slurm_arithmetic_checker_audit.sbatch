#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-checker-audit
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${ARITHMETIC_AUDIT_WORKFLOW_DIR:?frozen workflow and helpers}"
: "${ARITHMETIC_PREVIOUS_RUN:?saved QF_LIA run directory}"
: "${ARITHMETIC_REFERENCE:?Yices SMT-LIB executable}"
: "${MICROMATH_LIA_DIR:?pinned built external checker}"
: "${MICROMATH_LIA_GIT:?expected pinned revision}"
: "${PRIMO_SAT_SOLVER:?LRAT-capable CaDiCaL}"
cd "$PRIMO_WORKDIR"
out="$PWD/results/arithmetic-checker-audit-$SLURM_JOB_ID"
python3 "$ARITHMETIC_AUDIT_WORKFLOW_DIR/audit_arithmetic_checker_failures.py" \
  --previous-run "$ARITHMETIC_PREVIOUS_RUN" --micromath "$MICROMATH_LIA_DIR" \
  --micromath-git "$MICROMATH_LIA_GIT" --yices "$ARITHMETIC_REFERENCE" \
  --cadical "$PRIMO_SAT_SOLVER" --out "$out"
