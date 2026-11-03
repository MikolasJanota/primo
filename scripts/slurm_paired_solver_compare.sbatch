#!/usr/bin/env bash
#SBATCH --job-name=primo-paired-compare
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Paired timing comparison of several solver configurations.
#
# slurm_qf_lia_full_solver_compare.sbatch gives every (solver, shard) pair
# its own array task, so two configurations meet a given file on different
# nodes and at different times. Node and load differences then appear as
# solver differences, mostly for files close to the time limit. Here one
# array task runs every configuration on the same files, on the same node.
#
# Task t owns PRIMO_PAIRED_CHUNKS consecutive sub-shards of the corpus:
# indices t*C .. t*C+C-1 out of SHARDS*C, partitioned by
# bench_qf_uf_full.py's own --shards/--shard-index. For each sub-shard it
# runs all configurations, in an order rotated by the sub-shard index, so a
# load change inside the task is spread over all of them. Output follows the
# layout of the other compare scripts, one CSV per configuration and
# sub-shard: OUT_ROOT/<name>/shard-<index>.csv. Each task also records its
# node and CPU model in OUT_ROOT/nodes/task-<t>.txt.
#
# PRIMO_COMPARE_SOLVERS: semicolon-separated "name=command" pairs, as in the
# other compare scripts. Submit with --array=0-(PRIMO_PAIRED_SHARDS-1).
set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
ROOT=${PRIMO_COMPARE_ROOT:?PRIMO_COMPARE_ROOT must name the corpus directory}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:?PRIMO_COMPARE_OUT_ROOT must name the output directory}
SOLVERS=${PRIMO_COMPARE_SOLVERS:?PRIMO_COMPARE_SOLVERS must list name=command pairs}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-60}
SHARDS=${PRIMO_PAIRED_SHARDS:?PRIMO_PAIRED_SHARDS must give the number of array tasks}
CHUNKS=${PRIMO_PAIRED_CHUNKS:-4}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT/nodes"
if [[ ! -d $ROOT ]]; then
  echo "corpus root '$ROOT' not found; stage it first" >&2
  exit 2
fi
if (( TASK_ID >= SHARDS )); then
  echo "task $TASK_ID is past the $SHARDS shards; nothing to do"
  exit 0
fi

IFS=';' read -r -a specs <<< "$SOLVERS"
count=${#specs[@]}
names=()
for spec in "${specs[@]}"; do
  name=${spec%%=*}
  if [[ -z $name || $name == "$spec" ]]; then
    echo "invalid solver spec '$spec'; expected name=command" >&2
    exit 2
  fi
  names+=("$name")
  mkdir -p "$OUT_ROOT/$name"
done

{
  echo "task $TASK_ID host $(hostname) job ${SLURM_ARRAY_JOB_ID:-manual}"
  lscpu | grep -E 'Model name|^CPU\(s\)|MHz' || true
  for spec in "${specs[@]}"; do
    read -r -a words <<< "${spec#*=}"
    echo "${spec%%=*}: $("${words[0]}" --version | tr '\n' ' ')"
  done
} > "$OUT_ROOT/nodes/task-$TASK_ID.txt"

total=$((SHARDS * CHUNKS))
for ((chunk = 0; chunk < CHUNKS; chunk++)); do
  index=$((TASK_ID * CHUNKS + chunk))
  for ((k = 0; k < count; k++)); do
    i=$(((k + index) % count))
    spec=${specs[$i]}
    name=${names[$i]}
    read -r -a words <<< "${spec#*=}"
    extra=("${words[@]:1}")
    echo "task $TASK_ID sub-shard $index/$total: $name"
    # --solver takes only the binary; extra words go through --solver-arg
    # because argparse would stop at the first token starting with '-'.
    python3 scripts/bench_qf_uf_full.py \
      --repo "$WORKDIR" \
      --root "$ROOT" \
      --solver "${words[0]}" \
      "${extra[@]/#/--solver-arg=}" \
      --timeout "$TIMEOUT" \
      --shards "$total" \
      --shard-index "$index" \
      --out "$OUT_ROOT/$name/shard-$index.csv" \
      --summary "$OUT_ROOT/$name/shard-$index-summary.csv"
  done
done
