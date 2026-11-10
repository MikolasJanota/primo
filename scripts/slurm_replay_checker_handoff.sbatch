#!/usr/bin/env bash
#SBATCH --job-name=primo-checker-handoff-update
#SBATCH --array=0-7%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${CHECKER_HANDOFF_WORKFLOW:?frozen replay helpers and handoff}"
: "${CHECKER_OLD_DIR:?preserved old checker}"
: "${CHECKER_NEW_DIR:?isolated new checker}"
: "${CHECKER_OLD_GIT:?old revision}"
: "${CHECKER_NEW_GIT:?new revision}"
: "${CHECKER_OLD_BUILD:?old qualified build report}"
: "${CHECKER_NEW_BUILD:?new qualified build report}"
: "${PRIMO_SAT_SOLVER:?original LRAT-capable CaDiCaL}"
cd "$PRIMO_WORKDIR"
cases=(wisa5 clock7 lia-oom challenge-p15)
(( SLURM_ARRAY_TASK_ID < 8 ))
case_name=${cases[$((SLURM_ARRAY_TASK_ID / 2))]}
if (( SLURM_ARRAY_TASK_ID % 2 == 0 )); then
  profile=old; checker=$CHECKER_OLD_DIR; revision=$CHECKER_OLD_GIT; build=$CHECKER_OLD_BUILD
else
  profile=new; checker=$CHECKER_NEW_DIR; revision=$CHECKER_NEW_GIT; build=$CHECKER_NEW_BUILD
fi
bundle="$CHECKER_HANDOFF_WORKFLOW/benchmarks/checker-regressions/micromath-handoff-20261109"
python3 "$CHECKER_HANDOFF_WORKFLOW/scripts/replay_checker_handoff.py" \
  --bundle "$bundle/micromath-checker-feedback.tar.gz" --manifest "$bundle/summary.json" \
  --micromath "$checker" --micromath-git "$revision" --build-report "$build" \
  --cadical "$PRIMO_SAT_SOLVER" --case "$case_name" --profile "$profile" \
  --out "$PWD/results/checker-handoff-update-$SLURM_ARRAY_JOB_ID/$case_name/$profile"
