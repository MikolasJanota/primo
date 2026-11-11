#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-fuzz
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
count=${PRIMO_HYBRID_FUZZ_COUNT:-250}
out="${PRIMO_HYBRID_OUT:?}/fuzz/slot-$slot"
mkdir -p "$out"
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
# Four 250-case shards per logic and factor: 1,000 fresh cases in each profile.
factor=$((slot / 8))
logic=$(((slot / 4) % 2))
flags=(--solver-arg=--quiet --solver-arg=--lia-branching=hybrid
       --solver-arg=--lia-private-branch-factor="$factor")
if [[ $factor == 0 ]]; then
 flags+=(--solver-arg=--lia-patch-limit=0 --solver-arg=--lia-gomory-cut-limit=0)
fi
printf '%s\n' "${flags[@]}" > "$out/flags.txt"
if [[ $logic == 0 ]]; then
 solver="build-slurm-rel/primo --quiet --lia-branching=hybrid --lia-private-branch-factor=$factor"
 if [[ $factor == 0 ]]; then
  solver+=" --lia-patch-limit=0 --lia-gomory-cut-limit=0"
 fi
 python3 scripts/fuzz_external.py --fuzzer fuzzsmt --solver "$solver" \
  --refs z3 --logic QF_LIA --count "$count" --seed "$((2026111200+slot))" \
  --timeout 5 --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' --out "$out/answers"
else
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" \
  --ref z3 --count "$count" --seed "$((2026111200+slot))" --shape mixed --depth 3 \
  --timeout 5 --out "$out/answers"
fi
python3 scripts/select_fuzz_sat_inputs.py "$out/answers/summary.csv" --out "$out/sat.txt"
if [[ -s $out/sat.txt ]]; then
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
  --solver-arg=--check-model --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false \
  --manifest "$out/sat.txt" --timeout 20 --dolmen-timeout 20 --out "$out/models"
fi
