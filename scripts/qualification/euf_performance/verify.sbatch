#!/usr/bin/env bash
#SBATCH --job-name=primo-euf-performance-verify
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_VERIFY_BIN:?}:${PRIMO_CMAKE_BIN:?}:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
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
 make -o contrib-required check BUILD_DIR=build-slurm-rel \
   PYTHON=python3.11 RUFF="${PRIMO_VERIFY_BIN:?}/ruff" > "$PRIMO_REPAIR_OUT/check.log" 2>&1
 ;;
 1)
 asan_runtime="${PRIMO_ASAN_RUNTIME:?}"
 test -f "$asan_runtime/libasan.so.6.0.0"
 export CXXFLAGS="$CXXFLAGS -L$asan_runtime -Wl,-rpath,$asan_runtime"
 asan_build="build-euf-performance-asan-${PRIMO_EXPECTED_GIT:?}"
 # The synchronized source archive has no Git checkout. Pin this build's
 # generated revision as well as compiling the exact snapshot under ASan.
 cmake -S . -B "$asan_build" -DCMAKE_BUILD_TYPE=Debug \
   -DCMAKE_PREFIX_PATH="$PWD/contrib/local" -DPRIMO_SANITIZERS=address \
   -DPRIMO_GIT_REVISION_OVERRIDE="$PRIMO_EXPECTED_GIT" > "$PRIMO_REPAIR_OUT/asan-config.log" 2>&1
 make -o contrib-required asan-test ASAN_BUILD_DIR="$asan_build" \
   > "$PRIMO_REPAIR_OUT/asan.log" 2>&1
 "$asan_build/primo" --version | tee "$PRIMO_REPAIR_OUT/asan-version.txt"
 grep -Fx "git: $PRIMO_EXPECTED_GIT" "$PRIMO_REPAIR_OUT/asan-version.txt"
 sha256sum "$asan_build/primo" > "$PRIMO_REPAIR_OUT/asan.sha256"
 ;;
 *) exit 2 ;;
esac
