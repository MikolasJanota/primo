#!/usr/bin/env bash
#SBATCH --job-name=primo-checker-gprof
#SBATCH --array=0-2%3
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${CHECKER_PROFILE_WORKFLOW:?frozen profiling scripts}"
: "${CHECKER_PROFILE_GIT:?workflow base commit}"
: "${CHECKER_REPLAY_SAVED:?saved arithmetic exports}"
: "${MICROMATH_UPDATE_DIR:?qualified checker}"
: "${MICROMATH_UPDATE_GIT:?pinned checker revision}"
: "${CHECKER_UPDATE_BUILD_REPORT:?qualified build hashes}"
: "${MICROMATH_PROFILE_DIR:?isolated gprof build}"
: "${CHECKER_PROFILE_BUILD_REPORT:?gprof build hashes}"
: "${CHECKER_TIME_TOOL:?shared-storage GNU time executable}"
: "${CHECKER_TIME_SHA256:?GNU time executable hash}"
: "${PRIMO_SAT_SOLVER:?qualified SAT executable}"
cd "$PRIMO_WORKDIR"
test "$(sha256sum "$CHECKER_TIME_TOOL" | cut -d' ' -f1)" = "$CHECKER_TIME_SHA256"
"$CHECKER_TIME_TOOL" --version
python3 -m unittest discover -s "$CHECKER_PROFILE_WORKFLOW/tests/python" -p test_checker_profile.py
python3 "$CHECKER_PROFILE_WORKFLOW/scripts/profile_checker_medium.py" \
  --selection "$CHECKER_PROFILE_WORKFLOW/benchmarks/checker-regressions/checker-medium-comparison-20261111/selection.json" \
  --saved "$CHECKER_REPLAY_SAVED" --checker "$MICROMATH_UPDATE_DIR" \
  --build-report "$CHECKER_UPDATE_BUILD_REPORT" --checker-git "$MICROMATH_UPDATE_GIT" \
  --profile-checker "$MICROMATH_PROFILE_DIR" --profile-build-report "$CHECKER_PROFILE_BUILD_REPORT" \
  --cadical "$PRIMO_SAT_SOLVER" --time-tool "$CHECKER_TIME_TOOL" --workflow-git "$CHECKER_PROFILE_GIT" \
  --index "$SLURM_ARRAY_TASK_ID" \
  --out "$PWD/results/checker-gprof-$SLURM_ARRAY_JOB_ID/case-$SLURM_ARRAY_TASK_ID"
