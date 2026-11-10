#!/usr/bin/env bash
#SBATCH --job-name=primo-farkas-coefficients
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${FARKAS_WORKFLOW:?frozen adapter qualification inputs}"
: "${FARKAS_Z3:?optimization and reference Z3}"
: "${FARKAS_CADICAL:?propositional reference}"
: "${FARKAS_MICROMATH:?pinned Micromath}"
: "${FARKAS_MICROMATH_GIT:?checker revision}"
: "${FARKAS_CHECKER_BUILD:?qualified checker hashes}"
cd "$PRIMO_WORKDIR"
python3 -m unittest discover -s "$FARKAS_WORKFLOW/tests/python" -p test_farkas_coefficients.py
if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then mode=generated; else mode=real; fi
python3 "$FARKAS_WORKFLOW/scripts/qualify_farkas_coefficients.py" \
  --mode "$mode" --out "$PWD/results/farkas-coefficients-$SLURM_ARRAY_JOB_ID/$mode" \
  --z3 "$FARKAS_Z3" --cadical "$FARKAS_CADICAL" --micromath "$FARKAS_MICROMATH" \
  --micromath-git "$FARKAS_MICROMATH_GIT" --build-report "$FARKAS_CHECKER_BUILD" \
  --handoff "$FARKAS_WORKFLOW/benchmarks/checker-regressions/micromath-handoff-20261109/micromath-checker-feedback.tar.gz" \
  --handoff-manifest "$FARKAS_WORKFLOW/benchmarks/checker-regressions/micromath-handoff-20261109/summary.json" \
  --starters "$FARKAS_WORKFLOW/benchmarks/checker-regressions/lra-starter-20261104/lra-starter.tar.gz" \
  --authored "$FARKAS_WORKFLOW/docs/examples/farkas-coefficient-choice.sexp"
