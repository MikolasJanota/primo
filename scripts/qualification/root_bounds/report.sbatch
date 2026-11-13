#!/usr/bin/env bash
#SBATCH --job-name=primo-root-bounds-report
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 "${PRIMO_ROOT_BOUND_RUNNER:?}" --solver "$PWD/build-slurm-rel/primo" \
 --selection benchmarks/lia-hybrid-20261111/selection.json \
 --root "QF_LIA=${PRIMO_LIA_CORPUS:?}" --root "QF_UFLIA=${PRIMO_UFLIA_CORPUS:?}" \
 --timeout 5 --out "${PRIMO_ROOT_BOUNDS_REPORT_OUT:?}"
