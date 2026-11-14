#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-repairs-static
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_REPAIR_STATIC_OUT:?}"
mkdir "$out"
# A separate index supplies the archived snapshot's tracked-file inventory.
# It does not change the source checkout or the native build's revision.
git init --bare "$out/inventory.git"
export GIT_DIR="$out/inventory.git" GIT_WORK_TREE="$PWD"
git add -f --pathspec-from-file="${PRIMO_TRACKED_FILES:?}" --pathspec-file-nul
python3.11 scripts/prepare_static_analysis.py --build build-slurm-rel --out "$out/database"
for scan in tidy includes; do
 args=()
 if [[ $scan == includes ]]; then
  args+=('-checks=-*,clang-diagnostic-*,misc-include-cleaner')
 fi
 status=0
 python3.11 "${PRIMO_RUN_CLANG_TIDY:?}" -p "$out/database" -j "${SLURM_CPUS_PER_TASK:-1}" \
  -clang-tidy-binary "${PRIMO_VERIFY_BIN:?}/clang-tidy" "${args[@]}" \
  -export-fixes "$out/$scan-fixes/" > "$out/$scan.log" 2>&1 || status=$?
 python3.11 scripts/summarize_static_analysis.py --manifest "$out/database/manifest.json" \
  --log "$out/$scan.log" --exit-code "$status" --reject-unused-includes \
  --out "$out/$scan-summary.json" || true
 # Both reports are retained for inspection, including advisory include findings.
done
