#!/usr/bin/env bash
#SBATCH --job-name=primo-checker-medium
#SBATCH --array=0-3%4
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${CHECKER_MEDIUM_WORKFLOW:?frozen discovery workflow}"
: "${CHECKER_MEDIUM_GIT:?workflow commit}"
: "${CHECKER_REPLAY_SAVED:?saved arithmetic exports}"
: "${MICROMATH_UPDATE_DIR:?pinned checker}"
: "${MICROMATH_UPDATE_GIT:?checker revision}"
: "${CHECKER_UPDATE_BUILD_REPORT:?checker executable hashes}"
: "${PRIMO_SAT_SOLVER:?pinned SAT executable}"
cd "$PRIMO_WORKDIR"
python3 -m unittest discover -s "$CHECKER_MEDIUM_WORKFLOW/tests/python" -p test_checker_medium.py
python3 "$CHECKER_MEDIUM_WORKFLOW/scripts/replay_checker_medium.py" \
  --selection "$CHECKER_MEDIUM_WORKFLOW/benchmarks/checker-regressions/checker-medium-20261111/selection.json" \
  --saved "$CHECKER_REPLAY_SAVED" --micromath "$MICROMATH_UPDATE_DIR" \
  --micromath-git "$MICROMATH_UPDATE_GIT" --build-report "$CHECKER_UPDATE_BUILD_REPORT" \
  --cadical "$PRIMO_SAT_SOLVER" --workflow-git "$CHECKER_MEDIUM_GIT" \
  --shard "$SLURM_ARRAY_TASK_ID" --shards 4 --coefficient-statistics \
  --out "$PWD/results/checker-medium-$SLURM_ARRAY_JOB_ID/shard-$SLURM_ARRAY_TASK_ID"
