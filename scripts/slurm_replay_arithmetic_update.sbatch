#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-checker-update
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${CHECKER_UPDATE_WORKFLOW:?frozen update replay helpers}"
: "${MICROMATH_UPDATE_DIR:?isolated checker worktree}"
: "${MICROMATH_UPDATE_GIT:?pinned checker revision}"
: "${CHECKER_UPDATE_BUILD_REPORT:?completed build provenance}"
: "${CHECKER_UPDATE_LRA_AUDIT:?three validated LRA cases}"
: "${CHECKER_UPDATE_LIA_AUDIT:?five validated LIA cases}"
: "${PRIMO_SAT_SOLVER:?same LRAT-capable CaDiCaL}"
cd "$PRIMO_WORKDIR"
if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then
  logic=LRA
  audit=$CHECKER_UPDATE_LRA_AUDIT
else
  logic=LIA
  audit=$CHECKER_UPDATE_LIA_AUDIT
fi
test -f "$CHECKER_UPDATE_BUILD_REPORT/FINISHED"
python3 "$CHECKER_UPDATE_WORKFLOW/replay_arithmetic_checker_update.py" \
  --logic "$logic" --audit "$audit" --micromath "$MICROMATH_UPDATE_DIR" \
  --micromath-git "$MICROMATH_UPDATE_GIT" --cadical "$PRIMO_SAT_SOLVER" \
  --build-report "$CHECKER_UPDATE_BUILD_REPORT" \
  --out "$PWD/results/arithmetic-checker-update-$SLURM_ARRAY_JOB_ID/$logic"
