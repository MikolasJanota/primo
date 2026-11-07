#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-full60
#SBATCH --partition=compute
#SBATCH --array=0-47%24
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:35:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_EQ_OUT:?}/shard-$slot"
mkdir -p "$(dirname "$out")"
case ${PRIMO_EQ_PHASE:?} in
 timing)
  python3 scripts/qualification/uflia_equalities/run.py \
   --baseline "${PRIMO_BASELINE_SOLVER:?}" --candidate build-slurm-rel/primo \
   --baseline-git "${PRIMO_BASELINE_GIT:?}" --candidate-git "${PRIMO_EXPECTED_GIT:?}" \
   --root "${PRIMO_CORPUS_ROOT:?}" --out "$out" --shards 48 --shard "$slot" \
   --timeout 60 --include-equalities
  hostname > "$out/hostname.txt"
  lscpu > "$out/lscpu.txt"
  ;;
 models)
  python3 scripts/qualification/uflia_equalities/replay.py \
   --parent "${PRIMO_EQ_PARENT:?}/shard-$slot" --root "${PRIMO_CORPUS_ROOT:?}" --out "$out"
  ;;
 *) exit 2;;
esac
