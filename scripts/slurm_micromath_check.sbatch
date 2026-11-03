#!/usr/bin/env bash
#SBATCH --job-name=primo-micromath-check
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Prepare the Primo binary with slurm_prepare_benchmark_workspace.py first.
# External sources are separate checkouts/archives, never copied into Primo.
set -euo pipefail
: "${MICROMATH_DIR:?set the absolute Micromath source directory}"
: "${MICROMATH_GIT:?set the archived Micromath revision}"
: "${CADICAL_DIR:?set the absolute CaDiCaL source directory}"
: "${CADICAL_GIT:?set the archived CaDiCaL revision}"
: "${PRIMO_SOURCE_DIR:?set the prepared Primo source directory}"
: "${PRIMO_BINARY:?set the prepared Primo binary}"
: "${PRIMO_EXPECTED_GIT:?set its printed git revision}"
: "${PRIMO_ARTIFACTS:?set the absolute results directory}"
mkdir -p "$PRIMO_ARTIFACTS"
PRIMO_ARTIFACTS=$(realpath "$PRIMO_ARTIFACTS")
export PRIMO_ARTIFACTS
if [[ -n ${MICROMATH_PYTHON:-} ]]; then
  mkdir -p "$PRIMO_ARTIFACTS/tools"
  ln -sf "$MICROMATH_PYTHON" "$PRIMO_ARTIFACTS/tools/python3"
  export PATH="$PRIMO_ARTIFACTS/tools:$PATH"
fi
python3 -c 'import sys; assert sys.version_info >= (3, 10), "Micromath tests require Python >= 3.10"'
python3 --version > "$PRIMO_ARTIFACTS/python-version.txt"
if [[ -n ${PRIMO_CXX_HEADER_MANIFEST:-} ]]; then
  header_flags=""
  while IFS= read -r header_root; do
    case "$header_root" in
      ''|*[!a-zA-Z0-9_./-]*) echo 'invalid C++ header path' >&2; exit 2 ;;
    esac
    test -d "$header_root"
    header_flags="$header_flags -isystem $header_root"
  done < "$PRIMO_CXX_HEADER_MANIFEST"
  export CXXFLAGS="${CXXFLAGS:-}$header_flags"
fi
jobs=${SLURM_CPUS_PER_TASK:-2}
if [[ ${MICROMATH_REUSE_BUILD:-0} != 1 ]]; then
  (cd "$CADICAL_DIR" && ./configure && make -j"$jobs") > "$PRIMO_ARTIFACTS/cadical-build.log" 2>&1
  (cd "$MICROMATH_DIR" && make -j"$jobs") > "$PRIMO_ARTIFACTS/micromath-build.log" 2>&1
fi
export PATH="$CADICAL_DIR/build:$PATH"
(cd "$MICROMATH_DIR" && make check-smt) > "$PRIMO_ARTIFACTS/upstream-check-smt.log" 2>&1
"$PRIMO_BINARY" --version > "$PRIMO_ARTIFACTS/primo-version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$PRIMO_ARTIFACTS/primo-version.txt"
"$CADICAL_DIR/build/cadical" --version > "$PRIMO_ARTIFACTS/cadical-version.txt"
cd "$MICROMATH_DIR"
./build-axioms --fragment equality "$PRIMO_ARTIFACTS/axioms.umd" > "$PRIMO_ARTIFACTS/axioms.log" 2>&1
for variant in original erased-multisort; do
  input="$PRIMO_SOURCE_DIR/docs/examples/qf-uf-export.smt2"
  export_options=()
  if [[ $variant == erased-multisort ]]; then
    input="$PRIMO_SOURCE_DIR/docs/examples/qf-uf-export-multisort.smt2"
    export_options+=(--experimental-export-erase-sorts)
  fi
  for mode in none core mus; do
    case_dir="$PRIMO_ARTIFACTS/$variant/$mode"
    mkdir -p "$case_dir"
    timeout --kill-after=3s 30s "$PRIMO_BINARY" --quiet --stats \
      --export-cnf "$case_dir/input.primo" --export-cnf-reduction="$mode" \
      "${export_options[@]}" "$input" \
      > "$case_dir/primo-result.txt" 2> "$case_dir/primo-statistics.txt"
    grep -Fx unsat "$case_dir/primo-result.txt"
    ./smt/check-conflicts "$case_dir/input.primo" --stats > "$case_dir/conflicts.log" 2>&1
    ./smt/build-challenge "$PRIMO_ARTIFACTS/axioms.umd" "$case_dir/input.primo" \
      "$case_dir/challenge.umd" > "$case_dir/challenge.log" 2>&1
    ./smt/prove-cnf "$case_dir/input.primo" "$case_dir/problem.cnf" \
      "$case_dir/problem.lrat" --cadical "$CADICAL_DIR/build/cadical" > "$case_dir/cnf.log" 2>&1
    ./smt/prove-lrat "$case_dir/challenge.umd" "$case_dir/input.primo" \
      "$case_dir/problem.lrat" "$case_dir/solution.ump" > "$case_dir/proof.log" 2>&1
    ./check-solution "$case_dir/challenge.umd" "$case_dir/solution.ump" > "$case_dir/check.log" 2>&1
    grep -F 'challenge proven' "$case_dir/check.log"
  done
done
# Negative controls are authored here; no external checker implementation or
# tests are copied. This workflow targets the fixed structured example above.
python3 - <<'PY'
import copy
import os
import runpy
from pathlib import Path

root = Path(os.environ['PRIMO_ARTIFACTS'])
helpers = runpy.run_path(str(Path(os.environ['PRIMO_SOURCE_DIR']) / 'tests/cnf_export_cli.py'))
document = helpers['parse']((root / 'original/mus/input.primo').read_text())
bad = copy.deepcopy(document)
# A lone disequality has a model, so it is an invalid claimed theory conflict.
bad[5][1] = ['conflict', bad[5][1][-1]]
(root / 'bad-conflict.primo').write_text(helpers['text'](bad) + '\n')
missing = copy.deepcopy(document)
missing[5] = ['conflicts']
(root / 'missing-conflicts.primo').write_text(helpers['text'](missing) + '\n')
(root / 'corrupt.ump').write_bytes(b'corrupt proof\n')
PY
set +e
./smt/check-conflicts "$PRIMO_ARTIFACTS/bad-conflict.primo" > "$PRIMO_ARTIFACTS/bad-conflict.log" 2>&1
bad_conflict_status=$?
./smt/prove-cnf "$PRIMO_ARTIFACTS/missing-conflicts.primo" "$PRIMO_ARTIFACTS/missing.cnf" \
  "$PRIMO_ARTIFACTS/missing.lrat" --cadical "$CADICAL_DIR/build/cadical" > "$PRIMO_ARTIFACTS/missing.log" 2>&1
missing_status=$?
./check-solution "$PRIMO_ARTIFACTS/original/mus/challenge.umd" "$PRIMO_ARTIFACTS/corrupt.ump" > "$PRIMO_ARTIFACTS/corrupt.log" 2>&1
corrupt_status=$?
set -e
export bad_conflict_status missing_status corrupt_status
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path

root = Path(os.environ['PRIMO_ARTIFACTS'])
status = {key: int(os.environ[key]) for key in ('bad_conflict_status', 'missing_status', 'corrupt_status')}
result = {
    'micromath_git': os.environ['MICROMATH_GIT'],
    'cadical_git': os.environ['CADICAL_GIT'],
    'primo_git': os.environ['PRIMO_EXPECTED_GIT'],
    'reused_external_build': os.environ.get('MICROMATH_REUSE_BUILD') == '1',
    'binary_sha256': {name: hashlib.sha256(Path(path).read_bytes()).hexdigest() for name, path in {'primo': os.environ['PRIMO_BINARY'], 'cadical': str(Path(os.environ['CADICAL_DIR']) / 'build/cadical'), 'check-solution': str(Path(os.environ['MICROMATH_DIR']) / 'check-solution')}.items()},
    'job_id': os.environ['SLURM_JOB_ID'],
    'input_sha256': {name: hashlib.sha256((Path(os.environ['PRIMO_SOURCE_DIR']) / 'docs/examples' / name).read_bytes()).hexdigest() for name in ('qf-uf-export.smt2', 'qf-uf-export-multisort.smt2')},
    'modes': ['none', 'core', 'mus'],
    'variants': ['original', 'erased-multisort'],
    'negative_status': status,
    'resources': {'cpus': 2, 'memory_gib': 4, 'wall_minutes': 20},
    'certificate_files': {str(p.relative_to(root)): {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                          for p in root.glob('*/*/*') if p.suffix in ('.primo', '.umd', '.ump', '.lrat')},
}
(root / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
assert 0 < status['bad_conflict_status'] < 128, status
assert status['missing_status'] == 2, status
assert 0 < status['corrupt_status'] < 128, status
PY
printf '%s\n' 'upstream SMT checks, six full kernel replays, and negative controls passed' > "$PRIMO_ARTIFACTS/PASSED"
