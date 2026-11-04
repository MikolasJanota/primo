#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-export-gate
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Prepare/build with slurm_prepare_benchmark_workspace.py --build-via-slurm,
# then --check-only immediately before sbatch. Never run this on a login host.
set -euo pipefail
cd "${PRIMO_WORKDIR:-$PWD}"
: "${PRIMO_EXPECTED_GIT:?expected source revision}"
: "${PRIMO_CMAKE:?prepared cmake executable}"
: "${PRIMO_CXX_HEADER_MANIFEST:?matching compiler include roots}"
: "${PRIMO_SAT_SOLVER:?independent DIMACS SAT executable}"
: "${PRIMO_RUFF_PYTHON:?Python with the pinned Ruff dependency}"
export PATH="$(dirname "$PRIMO_CMAKE"):$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL="${SLURM_CPUS_PER_TASK:-4}"
export CTEST_PARALLEL_LEVEL=2
cxx_header_flags=""
while IFS= read -r root; do
  case "$root" in ''|*[!a-zA-Z0-9_./-]*) echo 'invalid header root' >&2; exit 2 ;; esac
  test -d "$root"
  cxx_header_flags="$cxx_header_flags -isystem $root"
done < "$PRIMO_CXX_HEADER_MANIFEST"
export CXXFLAGS="${CXXFLAGS:-}$cxx_header_flags"
artifacts="results/lra-export-$SLURM_JOB_ID"
mkdir -p "$artifacts"
build-slurm-rel/primo --version > "$artifacts/version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$artifacts/version.txt"
sha256sum build-slurm-rel/primo "$PRIMO_SAT_SOLVER" contrib/local/bin/z3 > "$artifacts/binaries.sha256"
sha256sum tests/lra_export_cli.py scripts/check_lra_certificate.py > "$artifacts/workflow.sha256"
lscpu > "$artifacts/cpu.txt"
ctest --test-dir build-slurm-rel -R '^primo_(lra_certificate|term_printer)_tests$' --timeout 120 --output-on-failure \
  > "$artifacts/focused-native.log" 2>&1
reuse_native=(-o contrib/local/.cli11.stamp -o contrib/local/.cadical.stamp -o contrib/local/.gmp.stamp)
make "${reuse_native[@]}" check BUILD_DIR=build-slurm-rel \
  RUFF="$PRIMO_RUFF_PYTHON -m ruff" > "$artifacts/check.log" 2>&1
python3 tests/lra_export_cli.py --solver build-slurm-rel/primo \
  --reference contrib/local/bin/z3 --sat-solver "$PRIMO_SAT_SOLVER" \
  --count 1000 --seed 202611050 --out "$artifacts/lra-fuzz" \
  > "$artifacts/lra-fuzz.log" 2>&1
python3 tests/cnf_export_cli.py --solver build-slurm-rel/primo \
  --reference contrib/local/bin/z3 --fuzz-count 1000 --seed 202611051 \
  --out "$artifacts/uf-fuzz" > "$artifacts/uf-fuzz.log" 2>&1
asan_dir=build-slurm-lra-asan
asan_flags=(-DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address
  "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT")
if [[ -n ${PRIMO_ASAN_RUNTIME_DIR:-} ]]; then
  test -f "$PRIMO_ASAN_RUNTIME_DIR/libasan.so"
  asan_flags+=("-DCMAKE_EXE_LINKER_FLAGS=-L$PRIMO_ASAN_RUNTIME_DIR")
  export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
"$PRIMO_CMAKE" -S . -B "$asan_dir" "${asan_flags[@]}" > "$artifacts/asan-config.log" 2>&1
make "${reuse_native[@]}" asan-test ASAN_BUILD_DIR="$asan_dir" \
  > "$artifacts/asan-test.log" 2>&1
printf '%s\n' 'native, Python, LRA/UF fresh export gates and ASan passed' > "$artifacts/PASSED"
