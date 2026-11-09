#!/usr/bin/env bash
#SBATCH --job-name=primo-overhead-cite-baseline-fixtures
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
# Candidate semantic fixtures, excluding assertions on the new counters, linked
# to the frozen baseline. Production source and solver binaries stay untouched.
/usr/bin/c++ -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}" tmp/cite-baseline-fixture-probe.cpp \
 -Wl,--start-group build-slurm-rel/libprimo_*.a contrib/local/lib/libcadical.a \
 contrib/local/lib/libgmpxx.a contrib/local/lib/libgmp.a -Wl,--end-group -pthread -ldl \
 -o tmp/cite-baseline-fixture-probe
tmp/cite-baseline-fixture-probe
