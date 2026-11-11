#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-model-unit-fuzz
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
export PRIMO_UNIT_FUZZ_SLOT=$slot
methods=${PRIMO_UNIT_METHODS:?}
mapfile -t flags < <(python3 "$methods/profile_args.py" "$((slot / 4))")
out="${PRIMO_UNIT_OUT:?}/fuzz/slot-$slot"
mkdir -p "$out"
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
args=()
for flag in "${flags[@]}"; do args+=(--solver-arg="$flag"); done
python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo "${args[@]}" \
 --ref contrib/local/bin/z3 --count 250 --seed "$((2026111100+slot))" \
 --shape mixed --depth 3 --timeout 5 --out "$out/answers"
python3 scripts/select_fuzz_sat_inputs.py "$out/answers/summary.csv" --out "$out/sat.txt"
if [[ -s $out/sat.txt ]]; then
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${args[@]}" \
  --solver-arg=--check-model --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false \
  --manifest "$out/sat.txt" --timeout 20 --dolmen-timeout 20 --out "$out/models"
fi
sha256sum --check "$out/binaries.sha256"
