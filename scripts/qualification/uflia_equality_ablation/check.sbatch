#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-check
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:20:00
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
cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for r in ("src", "tests", "docs/examples") for p in Path(r).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
make -o contrib-required check BUILD_DIR=build-slurm-rel RUFF="$PWD/tmp/qualification-tools/bin/python -m ruff" CPP_FORMAT_FILES="$cpp_files"
