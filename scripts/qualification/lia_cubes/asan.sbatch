#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-cube-asan
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_CUBE_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/asan.log") 2>&1
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
CXXFLAGS=$(python3 - "$PRIMO_CXX_HEADER_MANIFEST" <<'PY'
from pathlib import Path
import sys
print(' '.join('-isystem '+line for line in Path(sys.argv[1]).read_text().splitlines() if line))
PY
)
export CXXFLAGS
config=(-DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address
        "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT")
if [[ -n ${PRIMO_ASAN_RUNTIME_LIBDIR:-} ]]; then
 export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
 config+=("-DCMAKE_EXE_LINKER_FLAGS=-L$PRIMO_ASAN_RUNTIME_LIBDIR -Wl,-rpath,$PRIMO_ASAN_RUNTIME_LIBDIR")
fi
cmake -S . -B build-asan "${config[@]}"
make -o contrib-required asan-test
build-asan/primo --version > "$out/version.txt"
sha256sum build-asan/primo > "$out/binary.sha256"
for mode in uniform difference; do
 ASAN_OPTIONS=detect_leaks=1:strict_init_order=1 python3 scripts/fuzz_external.py \
  --fuzzer fuzzsmt --solver "$PWD/build-asan/primo --quiet --lia-cube=$mode" --refs z3 \
  --logic QF_LIA --count 100 --seed 202611081 --timeout 10 \
  --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' --out "$out/fuzz-$mode"
done
