#!/usr/bin/env bash
#SBATCH --job-name=primo-checker-remaining
#SBATCH --array=0-7%8
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${CHECKER_REPLAY_WORKFLOW:?frozen replay source and index}"
: "${CHECKER_REPLAY_SAVED:?preserved full arithmetic outputs}"
: "${MICROMATH_UPDATE_DIR:?pinned checker}"
: "${MICROMATH_UPDATE_GIT:?checker revision}"
: "${CHECKER_UPDATE_BUILD_REPORT:?qualified checker hashes}"
: "${PRIMO_SAT_SOLVER:?pinned LRAT-capable CaDiCaL}"
: "${CHECKER_REPLAY_Z3:?pinned optimization executable}"
cd "$PRIMO_WORKDIR"
python3 "$CHECKER_REPLAY_WORKFLOW/scripts/replay_remaining_checker_failures.py" \
  --selection "$CHECKER_REPLAY_WORKFLOW/benchmarks/checker-regressions/checker-replay-20261110/selection.json" \
  --saved "$CHECKER_REPLAY_SAVED" --micromath "$MICROMATH_UPDATE_DIR" \
  --micromath-git "$MICROMATH_UPDATE_GIT" --build-report "$CHECKER_UPDATE_BUILD_REPORT" \
  --cadical "$PRIMO_SAT_SOLVER" --z3 "$CHECKER_REPLAY_Z3" \
  --shard "$SLURM_ARRAY_TASK_ID" --shards 8 \
  --out "$PWD/results/checker-remaining-$SLURM_ARRAY_JOB_ID/shard-$SLURM_ARRAY_TASK_ID"
