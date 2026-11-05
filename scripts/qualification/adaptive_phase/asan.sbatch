#!/usr/bin/env bash
#SBATCH --job-name=primo-adaptive-phase-asan
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_PROBE_ASAN_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/asan.log") 2>&1
unset PRIMO_PHASE_PROBE PRIMO_PHASE_PROBE_K PRIMO_PHASE_PROBE_WINDOW PRIMO_PHASE_PROBE_TRACE || true
CXXFLAGS=$(python3 - "$PRIMO_CXX_HEADER_MANIFEST" <<'PY'
from pathlib import Path
import sys
print(' '.join('-isystem '+line for line in Path(sys.argv[1]).read_text().splitlines() if line))
PY
)
export CXXFLAGS
if [[ -n ${PRIMO_ASAN_RUNTIME_LIBDIR:-} ]]; then
 export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
 cmake -S . -B build-asan -DCMAKE_EXE_LINKER_FLAGS="-L$PRIMO_ASAN_RUNTIME_LIBDIR -Wl,-rpath,$PRIMO_ASAN_RUNTIME_LIBDIR" \
  -DPRIMO_GIT_REVISION_OVERRIDE="${PRIMO_EXPECTED_GIT:?}"
fi
make -o contrib-required asan-test
build-asan/primo --version > "$out/version.txt"
sha256sum build-asan/primo > "$out/binary.sha256"
for mode in release invert; do
 export PRIMO_PHASE_PROBE=$mode PRIMO_PHASE_PROBE_K=1 PRIMO_PHASE_PROBE_WINDOW=64
 ASAN_OPTIONS=detect_leaks=1:strict_init_order=1 python3 scripts/qf_uflia_fuzz.py \
  --solver build-asan/primo --solver-arg=--quiet --solver-arg=--arithmetic-model-phase=on \
  --solver-arg=--lia-primitive-int-rows --ref z3 --count 20 --seed 2026110513 --timeout 10 \
  --out "$out/fuzz-$mode"
done
