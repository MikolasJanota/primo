#!/usr/bin/env bash
# Fresh correctness checks: 1,000 cases per logic/profile, in 250-case shards.
#SBATCH --job-name=primo-model-repair-fuzz
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
logics=(uf uflia uflra lra lia)
modes=(strong auto)
logic=${logics[$((slot / 8))]}
mode=${modes[$(((slot % 8) / 4))]}
shard=$((slot % 4))
seed=$((202611510 + slot))
out="${PRIMO_REPAIR_OUT:?}/fuzz/$logic-$mode/shard-$shard"
mkdir -p "$out"
printf '%s\n' "$SLURM_ARRAY_JOB_ID $slot $seed $mode" > "$out/scheduler.txt"
flags=(--quiet --preprocessing-mode="$mode")
args=()
for flag in "${flags[@]}"; do args+=(--solver-arg="$flag"); done
case $logic in
 uf)
  python3 scripts/qf_uf_boolean_fuzz.py --solver build-slurm-rel/primo "${args[@]}" \
   --ref contrib/local/bin/z3 --count 250 --seed "$seed" --timeout 5 --keep-passing \
   --validate-models --dolmen contrib/local/bin/dolmen --out "$out"
  ;;
 lra)
  python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver build-slurm-rel/primo "${args[@]}" \
   --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
   --count 250 --seed "$seed" --timeout 5 --dolmen-timeout 10 --out "$out"
  ;;
 uflia|uflra)
  generator=scripts/qf_uflia_fuzz.py
  extra=(--shape mixed --depth 3)
  if [[ $logic == uflra ]]; then generator=scripts/qf_uflra_smoke_fuzz.py; extra=(); fi
  python3 "$generator" --solver build-slurm-rel/primo "${args[@]}" --ref contrib/local/bin/z3 \
   --count 250 --seed "$seed" --timeout 5 "${extra[@]}" --out "$out/answers"
  python3 scripts/select_fuzz_sat_inputs.py "$out/answers/summary.csv" --out "$out/sat.txt"
  if [[ -s $out/sat.txt ]]; then
   python3 scripts/validate_model.py --solver build-slurm-rel/primo "${args[@]}" \
    --solver-arg=--check-model --require-dolmen-pass --expect-sat \
    --dolmen-arg=--strict=false --timeout 10 --dolmen-timeout 10 --manifest "$out/sat.txt" --out "$out/models"
  fi
  ;;
 lia)
  export PRIMO_FUZZ_SOLVER_EXTRA_ARGS="--preprocessing-mode=$mode"
  export PRIMO_FUZZ_OUT_ROOT="$out/lia" PRIMO_FUZZ_FUZZSMT_SHARDS=1
  export PRIMO_FUZZ_FUZZSMT_COUNT=250 PRIMO_FUZZ_FUZZSMT_SEED_BASE=$seed
  export PRIMO_FUZZ_OPFUZZ_TASKS=0 PRIMO_FUZZ_YINYANG_SHARDS=0
  export PRIMO_FUZZ_MUTATION_REF='z3 model_validate=true'
  export SLURM_ARRAY_TASK_ID=0
  bash scripts/slurm_qf_lia_model_fuzz_gate.sbatch
  ;;
 *) exit 2 ;;
esac
