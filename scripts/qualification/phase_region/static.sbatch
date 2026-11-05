#!/usr/bin/env bash
#SBATCH --job-name=primo-phase-region-static
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PATH"
out=${PRIMO_PROBE_STATIC_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - <<'PY' > "$out/translation-units.txt"
from pathlib import Path
for root in ('src', 'tests'):
    for path in sorted(Path(root).rglob('*.cpp')):
        print(path)
PY
xargs -a "$out/translation-units.txt" -n1 -P"${SLURM_CPUS_PER_TASK:-4}" clang-tidy -p build-slurm-rel \
  -checks='clang-analyzer-*,misc-include-cleaner' > "$out/clang-tidy.log" 2>&1
headers=()
while IFS= read -r header; do headers+=(-isystem "$header"); done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
for source in docs/examples/*.cpp; do
  clang-tidy "$source" -checks='clang-analyzer-*,misc-include-cleaner' \
    -- -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}" >> "$out/clang-tidy.log" 2>&1
done
