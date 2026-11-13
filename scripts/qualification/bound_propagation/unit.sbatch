#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-propagation-unit
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
mkdir -p "${PRIMO_PROP_OUT:?}/gates"
ctest --test-dir build-slurm-rel --output-on-failure --timeout 120 \
 -R '^primo_qf_(lra|lia|uflia)_solver_tests$' > "$PRIMO_PROP_OUT/gates/unit.log" 2>&1
