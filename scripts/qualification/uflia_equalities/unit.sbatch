#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-unit
#SBATCH --partition=compute
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_EQ_OUT:?}
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/unit.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
build-slurm-rel/primo --help > "$out/help.txt"
sha256sum build-slurm-rel/primo > "$out/binary.sha256"
build-slurm-rel/primo_component_combination_tests
build-slurm-rel/primo_qf_uflia_solver_tests
