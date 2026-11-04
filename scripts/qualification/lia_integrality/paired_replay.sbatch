#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-integrality-paired
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Compare selected integrality profiles, then validate SAT
# models in a separately scheduled replay phase. Reuse the common provenance
# and replay workflow; each phase owns its own results and resource limits.
set -euo pipefail
workdir=${PRIMO_WORKDIR:?}
root=${PRIMO_COMPARE_ROOT:?}
solver=${PRIMO_COMPARE_BINARY:?}
out=${PRIMO_COMPARE_OUT_ROOT:?}
phase=${PRIMO_PHASE:?benchmark or replay}
shards=${PRIMO_PAIRED_SHARDS:?}
chunks=${PRIMO_PAIRED_CHUNKS:-4}
limit=${PRIMO_COMPARE_TIMEOUT:-60}
task=${SLURM_ARRAY_TASK_ID:-0}
cd "$workdir"
export PATH="$workdir/contrib/local/bin:$PATH"
(( task < shards ))
mkdir -p "$out/nodes"
{
  hostname
  "$solver" --version
  sha256sum "$solver"
  lscpu | grep -E 'Model name|^CPU\(s\)|MHz' || true
} > "$out/nodes/$phase-task-$task.txt"
read -r -a names <<< "${PRIMO_PAIRED_ARMS:-all m124}"
declare -A seen=()
for name in "${names[@]}"; do
  case "$name" in
    off|m12|m124|all) ;;
    *) echo "invalid integrality profile: $name" >&2; exit 2 ;;
  esac
  if [[ -v seen[$name] ]]; then echo "duplicate profile: $name" >&2; exit 2; fi
  seen[$name]=1
done
(( ${#names[@]} >= 2 ))
status=0
for ((chunk = 0; chunk < chunks; ++chunk)); do
  index=$((task * chunks + chunk))
  for ((offset = 0; offset < ${#names[@]}; ++offset)); do
    name=${names[$(((offset + index) % ${#names[@]}))]}
    directory="$out/$name/shard-$index"
    args=("$phase" --out "$directory")
    case "$phase" in
      benchmark)
        flags=(--quiet --stats)
        if [[ $name == off ]]; then
          flags+=(--no-lia-round-integer-bounds --no-lia-primitive-int-rows)
        else
          flags+=(--lia-round-integer-bounds --lia-primitive-int-rows)
        fi
        if [[ $name == all || $name == m124 ]]; then flags+=(--lia-patch-integral-moves)
        else flags+=(--no-lia-patch-integral-moves); fi
        if [[ $name == all ]]; then flags+=(--lia-integer-slacks)
        else flags+=(--no-lia-integer-slacks); fi
        args+=(--root "$root" --solver "$solver" --expected-git "${PRIMO_EXPECTED_GIT:?}"
               --run-id "${PRIMO_CAMPAIGN_ID:?}-$name" --shards "$((shards * chunks))"
               --shard-index "$index" --timeout "$limit")
        for flag in "${flags[@]}"; do args+=(--solver-arg="$flag"); done
        ;;
      replay)
        args+=(--model-timeout "${PRIMO_MODEL_TIMEOUT:-180}"
               --dolmen "$workdir/contrib/local/bin/dolmen"
               --dolmen-timeout "${PRIMO_DOLMEN_TIMEOUT:-300}"
               --dolmen-size 8G --dolmen-arg=--strict=false
               --attempt "${PRIMO_REPLAY_ATTEMPT:-model-validation}")
        ;;
      *) echo "invalid phase: $phase" >&2; exit 2 ;;
    esac
    python3 scripts/benchmark_model_replay.py "${args[@]}" || status=1
  done
done
exit "$status"
