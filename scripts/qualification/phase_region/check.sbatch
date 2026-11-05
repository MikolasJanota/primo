#!/usr/bin/env bash
#SBATCH --job-name=primo-phase-region-check
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_PROBE_CHECK_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/check.log") 2>&1
unset PRIMO_PHASE_PROBE PRIMO_PHASE_PROBE_K PRIMO_PHASE_PROBE_WINDOW PRIMO_PHASE_PROBE_TRACE PRIMO_REGION_PHASE PRIMO_REGION_SEED PRIMO_REGION_RATE || true
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/identities.sha256"
cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for root in ("src", "tests", "docs/examples") for p in Path(root).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
make -o contrib-required check BUILD_DIR=build-slurm-rel RUFF="$PWD/tmp/qualification-tools/bin/python -m ruff" CPP_FORMAT_FILES="$cpp_files"
flags=(--solver-arg=--lia-round-integer-bounds --solver-arg=--lia-primitive-int-rows
       --solver-arg=--no-lia-integer-slacks --solver-arg=--lia-patch-integral-moves
       --solver-arg=--arithmetic-model-phase=on)
for mode in random gated least most; do
 export PRIMO_PHASE_PROBE=observe PRIMO_REGION_PHASE=$mode PRIMO_REGION_RATE=1 PRIMO_REGION_SEED=2026110541
 seed=2026110541
 case $mode in gated) seed=2026110542;; least) seed=2026110543;; most) seed=2026110544;; esac
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  "${flags[@]}" --ref z3 --count 1000 --seed "$seed" --shape mixed --depth 3 --timeout 5 \
  --out "$out/fuzz-$mode"
 python3 - "$out/fuzz-$mode/summary.csv" <<'PYCODE' > "$out/sat-$mode.txt"
import csv
import sys
for row in csv.DictReader(open(sys.argv[1])):
    if row['ours'] == 'sat' and row['ours_code'] == '0' and row['outcome'] == 'PASS':
        print(row['case'])
PYCODE
 mapfile -t sat_cases < "$out/sat-$mode.txt"
 (( ${#sat_cases[@]} > 0 ))
 python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  "${flags[@]}" --require-dolmen-pass --expect-sat --timeout 10 --dolmen-timeout 10 \
  --dolmen-arg=--strict=false --out "$out/models-$mode" "${sat_cases[@]}"
done
