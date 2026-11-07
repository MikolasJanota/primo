#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-fuzz
#SBATCH --partition=compute
#SBATCH --array=0-4%5
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
modes=(off scan names fixed all)
slot=${SLURM_ARRAY_TASK_ID:?}
mode=${modes[$slot]}
out="${PRIMO_EQ_OUT:?}/$mode"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/gate.log") 2>&1
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
flags=(--solver-arg=--quiet --solver-arg=--arithmetic-model-phase=off "--solver-arg=--qf-uflia-tableau-equalities=$mode")
python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" --ref z3 \
 --count 1000 --seed "$((2026110780+slot))" --shape mixed --depth 3 --timeout 5 --out "$out/generated"
python3 scripts/select_fuzz_sat_inputs.py "$out/generated/summary.csv" --out "$out/sat.txt"
if [[ -s $out/sat.txt ]]; then
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
  --require-dolmen-pass --expect-sat --timeout 15 --dolmen-timeout 15 --dolmen-arg=--strict=false \
  --manifest "$out/sat.txt" --out "$out/models"
fi
