#!/usr/bin/env bash
# Four 250-case shards per logic/profile. SLURM supplies independent CPUs.
#SBATCH --job-name=primo-arithmetic-overhead-fuzz
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
profiles=(lia-default lia-m124 uflia-default uflia-m124 lra-default uflra-default)
profile=${profiles[$((slot / 4))]}
shard=$((slot % 4))
logic=${profile%-*}
out="${PRIMO_OVERHEAD_OUT:?}/fuzz/$profile"
mkdir -p "$out"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
printf '%s\t%s\t%s\n' "$SLURM_ARRAY_JOB_ID" "$slot" "$shard" > "$out/scheduler-$shard.tsv"
flags=()
if [[ $profile == *-m124 ]]; then
 flags=(--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves --lia-cube=uniform)
fi
export PRIMO_FUZZ_SOLVER_EXTRA_ARGS="${flags[*]}"
export PRIMO_FUZZ_OUT_ROOT="$out" PRIMO_FUZZ_OUT_DIR="$out"
export PRIMO_FUZZ_SOLVER_ARGS="${flags[*]}"
export PRIMO_FUZZ_FUZZSMT_SHARDS=4 PRIMO_FUZZ_FUZZSMT_COUNT=250
export PRIMO_FUZZ_FUZZSMT_SEED_BASE=${PRIMO_OVERHEAD_FUZZ_SEED_BASE:-202611090} PRIMO_FUZZ_COUNT=250
export PRIMO_FUZZ_SEED_BASE=$PRIMO_FUZZ_FUZZSMT_SEED_BASE PRIMO_FUZZ_OPFUZZ_TASKS=0 PRIMO_FUZZ_YINYANG_SHARDS=0
export PRIMO_FUZZ_MUTATION_REF='z3 model_validate=true'
export PRIMO_FUZZ_OPFUZZ_SEEDS="$PWD/benchmarks/seeds/qf_lra_real"
export PRIMO_FUZZ_YINYANG_SEEDS="$PRIMO_FUZZ_OPFUZZ_SEEDS"
export SLURM_ARRAY_TASK_ID=$shard
case $logic in
 lia) bash scripts/slurm_qf_lia_model_fuzz_gate.sbatch ;;
 lra) bash scripts/slurm_qf_lra_boolean_arithmetic_fuzz_gate.sbatch ;;
 uflia) bash scripts/slurm_qf_uflia_model_fuzz_gate.sbatch ;;
 uflra)
  target="$out/shard-$shard"
  args=()
  for flag in "${flags[@]}"; do args+=(--solver-arg="$flag"); done
  python3 scripts/qf_uflra_smoke_fuzz.py --solver build-slurm-rel/primo "${args[@]}" \
   --ref z3 --count 250 --seed "$((PRIMO_FUZZ_SEED_BASE + shard))" --timeout 5 --out "$target"
  python3 scripts/select_fuzz_sat_inputs.py "$target/summary.csv" --out "$target/sat.txt"
  if [[ -s $target/sat.txt ]]; then
   python3 scripts/validate_model.py --solver build-slurm-rel/primo "${args[@]}" \
    --require-dolmen-pass --expect-sat --dolmen-arg=--strict=false --timeout 10 \
    --dolmen-timeout 10 --manifest "$target/sat.txt" --out "$target/models"
  fi
 ;;
 *) exit 2 ;;
esac
