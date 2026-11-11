#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-asan
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_HYBRID_ASAN_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/asan.log") 2>&1
CXXFLAGS=$(python3 - "$PRIMO_CXX_HEADER_MANIFEST" <<'PY'
from pathlib import Path
import sys
print(' '.join('-isystem '+line for line in Path(sys.argv[1]).read_text().splitlines() if line))
PY
)
export CXXFLAGS
export LD_LIBRARY_PATH="${PRIMO_ASAN_RUNTIME_LIBDIR:?}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S . -B build-asan \
 -DCMAKE_EXE_LINKER_FLAGS="-L$PRIMO_ASAN_RUNTIME_LIBDIR -Wl,-rpath,$PRIMO_ASAN_RUNTIME_LIBDIR" \
 -DPRIMO_GIT_REVISION_OVERRIDE="${PRIMO_EXPECTED_GIT:?}"
make -o contrib-required asan-test
build-asan/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-asan/primo > "$out/binary.sha256"
