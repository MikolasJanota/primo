#!/usr/bin/env bash
#SBATCH --job-name=primo-checker-update-capture
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${CHECKER_UPDATE_WORKFLOW:?frozen staged replay helpers}"
: "${MICROMATH_UPDATE_DIR:?isolated checker worktree}"
: "${MICROMATH_UPDATE_GIT:?pinned checker revision}"
: "${CHECKER_UPDATE_BUILD_REPORT:?completed build provenance}"
: "${CHECKER_UPDATE_LRA_AUDIT:?validated LRA inputs}"
: "${PRIMO_SAT_SOLVER:?same LRAT-capable CaDiCaL}"
cd "$PRIMO_WORKDIR"
if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then name=shard-2-case-006; else name=shard-5-case-007; fi
python3 "$CHECKER_UPDATE_WORKFLOW/replay_arithmetic_checker_update.py" \
  --logic LRA --audit "$CHECKER_UPDATE_LRA_AUDIT" --micromath "$MICROMATH_UPDATE_DIR" \
  --micromath-git "$MICROMATH_UPDATE_GIT" --cadical "$PRIMO_SAT_SOLVER" \
  --build-report "$CHECKER_UPDATE_BUILD_REPORT" --case-name "$name" \
  --capture-only --capture-seconds 300 --raw-proof-bytes 17179869184 \
  --gzip-proof-bytes 4294967296 \
  --out "$PWD/results/arithmetic-update-capture-$SLURM_ARRAY_JOB_ID/case-$SLURM_ARRAY_TASK_ID"
