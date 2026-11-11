#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-check
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_HYBRID_CHECK_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/check.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
cpp_files=$(python3 - <<'PY'
from pathlib import Path
print(' '.join(str(p) for root in ('src', 'tests', 'docs/examples')
               for p in Path(root).rglob('*') if p.suffix in ('.cpp', '.hpp', '.h')))
PY
)
make -o contrib-required check BUILD_DIR=build-slurm-rel \
 RUFF="${PRIMO_RUFF:?}" CPP_FORMAT_FILES="$cpp_files"
build-slurm-rel/primo --help > "$out/help.txt"
sha256sum build-slurm-rel/primo > "$out/binary.sha256"
