#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-starter
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Run the workspace helper's --check-only immediately before submission.
set -euo pipefail
cd "${PRIMO_WORKDIR:?qualified native workspace}"
: "${PRIMO_PRODUCER_GIT:?full qualified implementation revision}"
: "${PRIMO_EXAMPLE_INPUTS:?directory of small SMT-LIB examples}"
: "${PRIMO_SAT_SOLVER:?external DIMACS CaDiCaL executable}"
example_tools="${PRIMO_EXAMPLE_TOOLS:-$PWD/scripts}"
python3 "$example_tools/replay_lra_checker_examples.py" \
  --inputs "$PRIMO_EXAMPLE_INPUTS" --out "results/lra-starter-$SLURM_JOB_ID" \
  --producer-git "$PRIMO_PRODUCER_GIT" --solver "$PWD/build-slurm-rel/primo" \
  --reference "$PWD/contrib/local/bin/z3" --sat-solver "$PRIMO_SAT_SOLVER"
