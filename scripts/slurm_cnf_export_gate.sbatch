#!/usr/bin/env bash
#SBATCH --job-name=primo-cnf-export-gate
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Submit only after slurm_prepare_benchmark_workspace.py --check-only.
# Source synchronization and the initial release build belong to that helper.
set -euo pipefail
cd "${PRIMO_WORKDIR:-$PWD}"
: "${PRIMO_EXPECTED_GIT:?set the expected binary revision}"
: "${PRIMO_CMAKE:?set the prepared cmake executable}"
: "${PRIMO_CXX_HEADER_MANIFEST:?set the matching C++ header manifest}"
export PATH="$(dirname "$PRIMO_CMAKE"):$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL="${SLURM_CPUS_PER_TASK:-4}"
export CTEST_PARALLEL_LEVEL=2
cxx_header_flags=""
while IFS= read -r header_root; do
  case "$header_root" in
    ''|*[!a-zA-Z0-9_./-]*) echo 'invalid C++ header path' >&2; exit 2 ;;
  esac
  test -d "$header_root"
  cxx_header_flags="$cxx_header_flags -isystem $header_root"
done < "$PRIMO_CXX_HEADER_MANIFEST"
export CXXFLAGS="${CXXFLAGS:-}$cxx_header_flags"
artifacts="results/cnf-export-${SLURM_JOB_ID}"
mkdir -p "$artifacts"
export PRIMO_EXPORT_ARTIFACTS="$artifacts"
build-slurm-rel/primo --version | tee "$artifacts/version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$artifacts/version.txt"
sha256sum build-slurm-rel/primo "$PRIMO_CXX_HEADER_MANIFEST" > "$artifacts/checksums.txt"
python3 - <<'PY'
import json
import os
import platform
from pathlib import Path

root = Path(os.environ['PRIMO_EXPORT_ARTIFACTS'])
(root / 'provenance.json').write_text(json.dumps({
    'commit': os.environ['PRIMO_EXPECTED_GIT'],
    'job_id': os.environ['SLURM_JOB_ID'],
    'host': platform.node(),
    'seed': 20261102,
    'count': 1000,
    'reduction_modes': 'round-robin none/core/mus',
    'sort_erasure': 'odd indices enabled; every fifth input has multiple object sorts',
    'solver_paths': '16-case cycles cover off/auto preprocessing, four EUF propagation modes, both engines; ite every seventh case',
    'mus_validation': 'drop each retained lemma and require propositional SAT',
    'timeout_per_process_seconds': 10,
    'reference': 'contrib/local/bin/z3 -in',
    'command': 'python3 tests/cnf_export_cli.py --solver build-slurm-rel/primo '
               '--reference contrib/local/bin/z3 --fuzz-count 1000 --seed 20261102 '
               '--out results/cnf-export-$SLURM_JOB_ID/fuzz',
    'resources': {'cpus': 4, 'memory_gib': 16, 'wall_minutes': 45},
}, indent=2) + '\n')
PY
# The checked-in structured example is also the user's external/1.smt2 case.
build-slurm-rel/primo --quiet --export-cnf "$artifacts/example.sexp"   docs/examples/qf-uf-export.smt2 > "$artifacts/example-result.txt"
grep -Fx unsat "$artifacts/example-result.txt"
test -s "$artifacts/example.sexp"

# Provision the Python lint tool inside the allocation, not on the login host.
python3 -m venv .venv
.venv/bin/python -m pip install ruff==0.16.1 > "$artifacts/lint-dependencies.log" 2>&1
# The preparation helper has verified these installed dependencies. Suppress
# stamp recipes explicitly: archive timestamps otherwise trigger downloads on
# compute nodes, which may have no network access. Do not suppress source builds.
reuse_native=(-o contrib/local/.cli11.stamp -o contrib/local/.cadical.stamp -o contrib/local/.gmp.stamp)
make "${reuse_native[@]}" check BUILD_DIR=build-slurm-rel > "$artifacts/check.log" 2>&1
build-slurm-rel/primo --help > "$artifacts/primo-help.txt"
python3 tests/cnf_export_cli.py --solver build-slurm-rel/primo \
  --reference contrib/local/bin/z3 --fuzz-count 1000 --seed 20261102 \
  --out "$artifacts/fuzz" > "$artifacts/export-fuzz.log" 2>&1
asan_dir=${PRIMO_EXPORT_ASAN_BUILD_DIR:-build-slurm-asan}
asan_config=(-DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address
  "-DCMAKE_PREFIX_PATH=$PWD/contrib/local"
  "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT")
if [[ -n ${PRIMO_ASAN_RUNTIME_DIR:-} ]]; then
  test -f "$PRIMO_ASAN_RUNTIME_DIR/libasan.so"
  asan_config+=("-DCMAKE_EXE_LINKER_FLAGS=-L$PRIMO_ASAN_RUNTIME_DIR")
  export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
cmake -S . -B "$asan_dir" "${asan_config[@]}" > "$artifacts/asan-config.log" 2>&1
make "${reuse_native[@]}" asan-test ASAN_BUILD_DIR="$asan_dir" > "$artifacts/asan-test.log" 2>&1
printf '%s\n' 'check, export fuzz gate, and asan-test passed' | tee "$artifacts/PASSED"
