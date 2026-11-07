#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-compare
#SBATCH --partition=compute
#SBATCH --array=0-11%12
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
methods=${PRIMO_EQ_METHODS:-scripts/qualification/uflia_equalities}
slot=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_EQ_OUT:?}/shard-$slot"
mkdir -p "$(dirname "$out")"
case ${PRIMO_EQ_PHASE:?} in
 timing)
  selection=()
  if [[ -n ${PRIMO_EQ_MANIFEST:-} ]]; then selection+=(--manifest "$PRIMO_EQ_MANIFEST"); fi
  python3 "$methods/run.py" --baseline "${PRIMO_BASELINE_SOLVER:?}" \
   --candidate build-slurm-rel/primo --baseline-git "${PRIMO_BASELINE_GIT:?}" \
   --candidate-git "${PRIMO_EXPECTED_GIT:?}" --root "${PRIMO_CORPUS_ROOT:?}" \
   --out "$out" --shards 12 --shard "$slot" --timeout 15 "${selection[@]}"
  hostname > "$out/hostname.txt"
  lscpu > "$out/lscpu.txt"
  ;;
 models)
  python3 "$methods/replay.py" \
   --parent "${PRIMO_EQ_PARENT:?}/shard-$slot" --root "${PRIMO_CORPUS_ROOT:?}" --out "$out"
  ;;
 *) exit 2;;
esac
