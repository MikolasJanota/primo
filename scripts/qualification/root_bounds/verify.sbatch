#!/usr/bin/env bash
#SBATCH --job-name=primo-root-bounds-verify
#SBATCH --cpus-per-task=4
#SBATCH --mem=24G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export PRIMO_EXPECTED_GIT=${PRIMO_EXPECTED_GIT:?}
out=${PRIMO_ROOT_BOUNDS_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version | grep -Fx "git: $PRIMO_EXPECTED_GIT"
slot=${SLURM_ARRAY_TASK_ID:?}
case $slot in
 0)
  export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
  cpp_files=$(python3 - <<'PY'
from pathlib import Path
print(' '.join(str(p) for root in ('src', 'tests', 'docs/examples')
               for p in Path(root).rglob('*') if p.suffix in ('.cpp', '.hpp', '.h')))
PY
)
  make -o contrib-required check BUILD_DIR=build-slurm-rel \
   RUFF="${PRIMO_RUFF:?}" CPP_FORMAT_FILES="$cpp_files" > "$out/check.log" 2>&1
 ;;
 1|2|3|4)
  export PATH="${PRIMO_FUZZSMT_BIN_DIR:?}:$PATH"
  target="$out/fuzz-$slot"
  case $slot in
   1)
    python3 scripts/fuzz_external.py --fuzzer fuzzsmt \
     --solver 'build-slurm-rel/primo --quiet --arithmetic-bound-diagnostics --check-model' \
     --refs z3 --logic QF_LIA --count 1000 --seed 2026111301 --timeout 5 \
     --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' --out "$target"
   ;;
   2)
    python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo \
     --solver-arg=--quiet --solver-arg=--arithmetic-bound-diagnostics --solver-arg=--check-model \
     --ref z3 --count 1000 --seed 2026111302 --shape mixed --depth 3 --timeout 5 --out "$target"
   ;;
   3)
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver build-slurm-rel/primo \
     --solver-arg=--quiet --solver-arg=--arithmetic-bound-diagnostics --solver-arg=--check-model \
     --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
     --count 1000 --seed 2026111303 --timeout 5 --out "$target"
   ;;
   4)
    python3 scripts/qf_uflra_smoke_fuzz.py --solver build-slurm-rel/primo \
     --solver-arg=--quiet --solver-arg=--arithmetic-bound-diagnostics --solver-arg=--check-model \
     --ref z3 --count 1000 --seed 2026111304 --timeout 5 --out "$target"
   ;;
  esac
 ;;
 5)
  export PRIMO_HYBRID_STATIC_OUT="$out/static"
  bash scripts/qualification/lia_hybrid/static.sbatch
 ;;
 6)
  export PRIMO_HYBRID_ASAN_OUT="$out/asan"
  bash scripts/qualification/lia_hybrid/asan.sbatch
 ;;
 *) exit 2 ;;
esac
