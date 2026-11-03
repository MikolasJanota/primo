#!/usr/bin/env bash
# Real-seed mutation campaign with established per-seed coverage.
#
# The gate scripts run typefuzz/opfuzz over a whole seed directory under one
# wall limit. When that wall is reached, yinyang_fuzz.sh cannot say which seeds
# were mutated, so the run is not evidence that every seed was covered. This
# script runs one array task per (fuzzer, seed) pair instead. Each task copies
# its seed into a private directory and records COVERED when the fuzzer
# finished all its iterations, or WALL when the wall limit cut it short.
#
# Task k uses fuzzer k / N and seed k % N, where N is the number of seeds no
# larger than PRIMO_MUTATION_FILE_SIZE_LIMIT, in sorted path order. Submit with
# --array=0-(F*N-1), where F is the number of words in PRIMO_MUTATION_FUZZERS.
# Tasks past the end exit 0 without running.
#
# Each task appends nothing shared; it writes task-K/status.tsv with the
# columns task, fuzzer, seed, status, bug_files, seconds. Concatenate them to
# summarize. Bug files land in task-K/bugs and must be replayed and triaged.
#SBATCH --job-name=primo-seed-mutation
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:45:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
SEEDS=${PRIMO_MUTATION_SEEDS:?PRIMO_MUTATION_SEEDS must name a seed directory}
OUT=${PRIMO_MUTATION_OUT:?PRIMO_MUTATION_OUT must name the output directory}
EXPECTED_GIT=${PRIMO_EXPECTED_GIT:?PRIMO_EXPECTED_GIT must name the binary revision}
SOLVER_BIN=${PRIMO_MUTATION_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
SOLVER_ARGS=${PRIMO_MUTATION_SOLVER_ARGS:-}
REF=${PRIMO_MUTATION_REF:-"$WORKDIR/contrib/local/bin/z3 model_validate=true"}
FUZZERS=${PRIMO_MUTATION_FUZZERS:-"opfuzz typefuzz"}
OPFUZZ_CONFIG=${PRIMO_MUTATION_OPFUZZ_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lia_operator_mutations.txt"}
TYPEFUZZ_CONFIG=${PRIMO_MUTATION_TYPEFUZZ_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lia_typefuzz.txt"}
OPFUZZ_ITERATIONS=${PRIMO_MUTATION_OPFUZZ_ITERATIONS:-200}
TYPEFUZZ_ITERATIONS=${PRIMO_MUTATION_TYPEFUZZ_ITERATIONS:-100}
SOLVER_TIMEOUT=${PRIMO_MUTATION_SOLVER_TIMEOUT:-5}
WALL_SECONDS=${PRIMO_MUTATION_WALL_SECONDS:-5400}
FILE_SIZE_LIMIT=${PRIMO_MUTATION_FILE_SIZE_LIMIT:-200000}
RANDOM_SEED_BASE=${PRIMO_MUTATION_RANDOM_SEED_BASE:-2026110300}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT"
PATH="$WORKDIR/contrib/local/bin:$PATH"
if [[ -d "$WORKDIR/.venv-yinyang/bin" ]]; then
  PATH="$WORKDIR/.venv-yinyang/bin:$PATH"
fi
export PATH

read -r -a fuzzers <<< "$FUZZERS"
for fuzzer in "${fuzzers[@]}"; do
  case $fuzzer in
    opfuzz | typefuzz) ;;
    *)
      echo "unknown fuzzer '$fuzzer'; expected opfuzz or typefuzz" >&2
      exit 2
      ;;
  esac
  if ! command -v "$fuzzer" > /dev/null 2>&1; then
    echo "required tool '$fuzzer' not found on PATH; prepare contrib-yinyang first" >&2
    exit 2
  fi
done
if ! command -v z3 > /dev/null 2>&1; then
  echo "reference z3 not found on PATH" >&2
  exit 2
fi

version=$("$SOLVER_BIN" --version)
if ! grep -Fxq "git: $EXPECTED_GIT" <<< "$version"; then
  echo "solver banner does not report git: $EXPECTED_GIT" >&2
  echo "$version" >&2
  exit 2
fi

mapfile -t seeds < <(find "$SEEDS" -type f -name '*.smt2' -size "-$((FILE_SIZE_LIMIT + 1))c" | sort)
seed_count=${#seeds[@]}
if (( seed_count == 0 )); then
  echo "no seeds of at most $FILE_SIZE_LIMIT bytes under $SEEDS" >&2
  exit 2
fi
total=$((seed_count * ${#fuzzers[@]}))
if (( TASK_ID == 0 )); then
  printf '%s\n' "${seeds[@]}" > "$OUT/seeds.txt"
  echo "$version" > "$OUT/version.txt"
  sha256sum "$SOLVER_BIN" > "$OUT/binary.sha256"
fi
if (( TASK_ID >= total )); then
  echo "task $TASK_ID is past the $total (fuzzer, seed) pairs; nothing to do"
  exit 0
fi

fuzzer=${fuzzers[$((TASK_ID / seed_count))]}
seed=${seeds[$((TASK_ID % seed_count))]}
task_dir="$OUT/task-$TASK_ID"
rm -rf "$task_dir"
mkdir -p "$task_dir/seed"
cp "$seed" "$task_dir/seed/"

solver_command="$SOLVER_BIN --quiet"
if [[ -n $SOLVER_ARGS ]]; then
  solver_command+=" $SOLVER_ARGS"
fi
extra=()
if [[ $fuzzer == opfuzz ]]; then
  iterations=$OPFUZZ_ITERATIONS
  # OpFuzz has no --seed option; seeded_opfuzz.py seeds Python's RNG for it.
  extra+=(--typefuzz "$WORKDIR/scripts/seeded_opfuzz.py" --config "$OPFUZZ_CONFIG")
else
  iterations=$TYPEFUZZ_ITERATIONS
  extra+=(--config "$TYPEFUZZ_CONFIG" --hoist-declarations)
fi

echo "task $TASK_ID: $fuzzer on $seed ($iterations iterations, ${WALL_SECONDS}s wall)"
start=$(date +%s)
status=ERROR
if scripts/yinyang_fuzz.sh \
  --seeds "$task_dir/seed" \
  --solver "$solver_command" \
  --ref "$REF" \
  --seconds "$WALL_SECONDS" \
  --iterations "$iterations" \
  --solver-timeout "$SOLVER_TIMEOUT" \
  --file-size-limit "$FILE_SIZE_LIMIT" \
  --random-seed "$((RANDOM_SEED_BASE + TASK_ID))" \
  --keep-logic \
  "${extra[@]}" \
  --bugs "$task_dir/bugs" \
  --scratch "$task_dir/scratch" \
  --logs "$task_dir/logs" > "$task_dir/run.log" 2>&1; then
  if grep -q '^yinyang_fuzz: completed;' "$task_dir/run.log"; then
    status=COVERED
  elif grep -q '^yinyang_fuzz: reached the' "$task_dir/run.log"; then
    status=WALL
  fi
fi
seconds=$(($(date +%s) - start))
bug_files=0
if [[ -d $task_dir/bugs ]]; then
  bug_files=$(find "$task_dir/bugs" -mindepth 1 -type f | wc -l)
fi
printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$TASK_ID" "$fuzzer" "${seed#"$SEEDS"/}" "$status" \
  "$bug_files" "$seconds" > "$task_dir/status.tsv"
cat "$task_dir/status.tsv"
cat "$task_dir/run.log"
rm -rf "$task_dir/scratch"
if [[ $status == ERROR ]]; then
  exit 1
fi
