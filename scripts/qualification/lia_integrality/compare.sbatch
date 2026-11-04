#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-integrality-review
#SBATCH --cpus-per-task=2
#SBATCH --mem=3G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Paired diagnostic of integer slack obligations, using one verified binary.
# Configure the corpus, manifest, binary and output through the environment.
# Every arm of a sub-shard runs in one allocation; rotate the arm order.
set -euo pipefail
workdir=${PRIMO_WORKDIR:?}
root=${PRIMO_COMPARE_ROOT:?}
manifest=${PRIMO_COMPARE_MANIFEST:?}
solver=${PRIMO_COMPARE_BINARY:?}
out=${PRIMO_COMPARE_OUT_ROOT:?}
shards=${PRIMO_PAIRED_SHARDS:?}
chunks=${PRIMO_PAIRED_CHUNKS:-2}
limit=${PRIMO_COMPARE_TIMEOUT:-10}
task=${SLURM_ARRAY_TASK_ID:-0}
cd "$workdir"
test -d "$root"
test -f "$manifest"
(( task < shards ))
mkdir -p "$out/nodes"
{
  hostname
  "$solver" --version
  sha256sum "$solver" "$manifest"
  lscpu | grep -E 'Model name|^CPU\(s\)|MHz' || true
} > "$out/nodes/task-$task.txt"
names=(off m12 m124 all)
for ((chunk = 0; chunk < chunks; ++chunk)); do
  index=$((task * chunks + chunk))
  for ((offset = 0; offset < ${#names[@]}; ++offset)); do
    name=${names[$(((offset + index) % ${#names[@]}))]}
    case "$name" in
      off) flags=(--no-lia-round-integer-bounds --no-lia-primitive-int-rows
                  --no-lia-integer-slacks --no-lia-patch-integral-moves) ;;
      m12) flags=(--lia-round-integer-bounds --lia-primitive-int-rows
                  --no-lia-integer-slacks --no-lia-patch-integral-moves) ;;
      m124) flags=(--lia-round-integer-bounds --lia-primitive-int-rows
                   --no-lia-integer-slacks --lia-patch-integral-moves) ;;
      all) flags=(--lia-round-integer-bounds --lia-primitive-int-rows
                  --lia-integer-slacks --lia-patch-integral-moves) ;;
    esac
    mkdir -p "$out/$name"
    python3 scripts/bench_qf_uf_full.py --repo "$workdir" --root "$root" \
      --manifest "$manifest" --solver "$solver" --solver-arg=--quiet --solver-arg=--stats \
      "${flags[@]/#/--solver-arg=}" --timeout "$limit" \
      --shards "$((shards * chunks))" --shard-index "$index" \
      --out "$out/$name/shard-$index.csv" \
      --summary "$out/$name/shard-$index-summary.csv"
  done
done
