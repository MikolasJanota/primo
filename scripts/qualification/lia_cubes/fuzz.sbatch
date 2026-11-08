#!/usr/bin/env bash
# Four profiles, three 1000-case shards each; the shared gate independently
# validates SAT models. Keep profile and original scheduler identity durable.
#SBATCH --job-name=primo-lia-cube-fuzz
#SBATCH --ntasks=1
#SBATCH --array=0-11%12
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
profiles=(uniform difference m124-uniform m124-difference)
profile=${profiles[$((slot / 3))]}
shard=$((slot % 3))
mode=${profile##*-}
out="${PRIMO_CUBE_OUT:?}/$profile"
mkdir -p "$out"
printf '%s\t%s\t%s\n' "$SLURM_ARRAY_JOB_ID" "$slot" "$shard" > "$out/scheduler-$shard.tsv"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
export PRIMO_FUZZ_SOLVER_EXTRA_ARGS="--lia-cube=$mode"
if [[ $profile == m124-* ]]; then
 export PRIMO_FUZZ_SOLVER_EXTRA_ARGS="$PRIMO_FUZZ_SOLVER_EXTRA_ARGS --lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves"
fi
export PRIMO_FUZZ_OUT_ROOT="$out"
export PRIMO_FUZZ_FUZZSMT_SHARDS=3 PRIMO_FUZZ_FUZZSMT_COUNT=1000
export PRIMO_FUZZ_FUZZSMT_SEED_BASE=202611080
export PRIMO_FUZZ_OPFUZZ_TASKS=0 PRIMO_FUZZ_YINYANG_SHARDS=0
# The inner gate treats this as its within-profile shard; scheduler identity
# above retains the actual task for provenance and failure replay.
export SLURM_ARRAY_TASK_ID=$shard
bash scripts/slurm_qf_lia_model_fuzz_gate.sbatch
