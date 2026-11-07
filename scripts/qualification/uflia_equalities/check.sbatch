#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-check
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_EQ_OUT:?}
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/check.log") 2>&1
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/identities.sha256"
cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for r in ("src", "tests", "docs/examples") for p in Path(r).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
make -o contrib-required check BUILD_DIR=build-slurm-rel RUFF="$PWD/tmp/qualification-tools/bin/python -m ruff" CPP_FORMAT_FILES="$cpp_files"
index=0
for profile in native-online native-rebuild m124-online m124-rebuild native-complete native-default m124-default; do
 flags=()
 case $profile in *default) ;; *) flags+=(--solver-arg=--qf-uflia-tableau-equalities);; esac
 case $profile in *rebuild) flags+=(--solver-arg=--no-qf-uflia-online-components);; esac
 case $profile in m124-*) flags+=(--solver-arg=--lia-round-integer-bounds --solver-arg=--lia-primitive-int-rows --solver-arg=--no-lia-integer-slacks --solver-arg=--lia-patch-integral-moves);; esac
 case $profile in *complete) flags+=(--solver-arg=--no-qf-uflia-model-combination);; esac
 index=$((index+1))
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  "${flags[@]}" --ref z3 --count 1000 --seed "$((2026110710+index))" --shape mixed --depth 3 --timeout 5 --out "$out/fuzz-$profile"
 python3 scripts/select_fuzz_sat_inputs.py "$out/fuzz-$profile/summary.csv" --out "$out/sat-$profile.txt"
 test -s "$out/sat-$profile.txt"
 python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  "${flags[@]}" --require-dolmen-pass --expect-sat --timeout 10 --dolmen-timeout 10 \
  --dolmen-arg=--strict=false --manifest "$out/sat-$profile.txt" --out "$out/models-$profile"
done
