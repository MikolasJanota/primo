#!/usr/bin/env bash
#SBATCH --job-name=primo-overhead-baseline-fixtures
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
headers=()
while IFS= read -r header; do headers+=(-isystem "$header"); done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
# This source is the candidate regression suite with only the assertions
# referring to newly added counters removed. Link the pristine baseline libs.
/usr/bin/c++ -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}" tmp/baseline-fixture-probe.cpp \
 build-slurm-rel/libprimo_theory.a build-slurm-rel/libprimo_terms.a \
 build-slurm-rel/libprimo_util.a contrib/local/lib/libgmpxx.a contrib/local/lib/libgmp.a \
 -o tmp/baseline-fixture-probe
tmp/baseline-fixture-probe
