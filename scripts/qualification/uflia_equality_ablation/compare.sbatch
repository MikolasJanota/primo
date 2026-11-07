#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-compare
#SBATCH --partition=compute
#SBATCH --array=0-11%12
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:50:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_EQ_OUT:?}/shard-$slot"
export PATH="$PWD/contrib/local/bin:$PATH"
case ${PRIMO_EQ_PHASE:?} in
 timing)
  extra=()
  if [[ -n ${PRIMO_EQ_MANIFEST:-} ]]; then extra+=(--manifest "$PRIMO_EQ_MANIFEST"); fi
  # PRIMO_EQ_PROFILES is a whitespace-separated list from the enumerated runner choices.
  read -r -a profiles <<< "${PRIMO_EQ_PROFILES:-old-off off scan names fixed all}"
  python3 "${PRIMO_EQ_METHODS:-scripts/qualification/uflia_equality_ablation}/run.py" \
   --baseline "${PRIMO_BASELINE_SOLVER:?}" --baseline-git "${PRIMO_BASELINE_GIT:?}" \
   --candidate build-slurm-rel/primo --candidate-git "${PRIMO_EXPECTED_GIT:?}" \
   --root "${PRIMO_CORPUS_ROOT:?}" --out "$out" --shards "${PRIMO_EQ_SHARDS:?}" \
   --shard "$slot" --timeout "${PRIMO_EQ_TIMEOUT:-60}" --profiles "${profiles[@]}" "${extra[@]}"
  hostname > "$out/hostname.txt"
  lscpu > "$out/lscpu.txt"
  ;;
 models)
  python3 scripts/qualification/uflia_equalities/replay.py \
   --parent "${PRIMO_EQ_PARENT:?}/shard-$slot" --root "${PRIMO_CORPUS_ROOT:?}" --out "$out"
  ;;
 *) exit 2;;
esac
