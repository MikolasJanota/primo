#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-repairs-verify
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_VERIFY_BIN:?}:${PRIMO_CMAKE_BIN:?}:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
# Match the preparation helper's provisioned compiler header roots.
header_flags=
while IFS= read -r header_root; do
  case "$header_root" in
    ''|*[!a-zA-Z0-9_./-]*) exit 2 ;;
  esac
  test -d "$header_root"
  header_flags="$header_flags -isystem $header_root"
done < "${PRIMO_HEADER_MANIFEST:?}"
export CXXFLAGS="${CXXFLAGS:-}$header_flags"
export CMAKE_BUILD_PARALLEL_LEVEL="${SLURM_CPUS_PER_TASK:-1}"
mkdir -p "${PRIMO_REPAIR_OUT:?}"
case "${SLURM_ARRAY_TASK_ID:?}" in
 0)
 # Dependencies were checked by the preparation helper; do not rebuild a
 # shared dependency installation via Make's timestamp rules.
 make -o contrib-required check BUILD_DIR=build-slurm-rel \
   PYTHON=python3.11 RUFF="${PRIMO_VERIFY_BIN:?}/ruff" > "$PRIMO_REPAIR_OUT/check.log" 2>&1
 ;;
 1)
 # The system GCC linker script names a runtime absent on compute nodes.
 # Use the user-owned runtime extracted from the matching distribution RPM.
 runtime="${PRIMO_ASAN_RUNTIME:?}"
 test -f "$runtime/libasan.so.6.0.0"
 export CXXFLAGS="$CXXFLAGS -L$runtime -Wl,-rpath,$runtime"
 make -o contrib-required asan-test ASAN_BUILD_DIR=build-repairs-asan-fixed \
   > "$PRIMO_REPAIR_OUT/asan.log" 2>&1
 ;;
 *) exit 2 ;;
esac
