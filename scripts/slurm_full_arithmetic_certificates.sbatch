#!/usr/bin/env bash
#SBATCH --job-name=primo-full-arithmetic-certs
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen full-sweep helpers}"
: "${ARITHMETIC_HISTORY_ROOT:?recorded native runs}"
cd "$PRIMO_WORKDIR"
if [[ ${FULL_ARITHMETIC_STAGE:-run} == select ]]; then
  : "${FULL_LRA_CONTROLS:?previous217case manifest}"
  : "${FULL_LIA_CONTROLS:?previous128case manifest}"
  if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then
    logic=QF_LRA; corpus=$LRA_CORPUS; controls=$FULL_LRA_CONTROLS
  else
    logic=QF_LIA; corpus=$LIA_CORPUS; controls=$FULL_LIA_CONTROLS
  fi
  out="$PWD/results/full-arithmetic-selection-$SLURM_ARRAY_JOB_ID/$logic"
  python3 "$ARITHMETIC_WORKFLOW_DIR/select_arithmetic_certificate_sweep.py" \
    --logic "$logic" --history "$ARITHMETIC_HISTORY_ROOT/$logic/primo" --corpus "$corpus" \
    --baseline-selection "$controls" --historical-git 5efbd02293e15a53fbaad748ceda78bc7d27bbac \
    --all-known-unsat --cases-per-shard 25 --out "$out"
  sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{select_arithmetic_certificate_sweep.py,arithmetic_certificate_stress.py,slurm_full_arithmetic_certificates.sbatch} > "$out/selection-workflow.sha256"
  exit 0
fi
[[ ${FULL_ARITHMETIC_STAGE:-run} == run ]] || { echo 'invalid full-sweep stage' >&2; exit 2; }
root="$PWD/results/full-arithmetic-certificates-$SLURM_ARRAY_JOB_ID"
mkdir -p "$root"
# Preserve the first failing worker and cancel only this campaign's array.
stop_failed_worker() {
  local full_exit_code=$?
  trap - EXIT
  if (( full_exit_code != 0 )); then
    if (set -o noclobber; printf 'task=%s exit=%s\n' "$SLURM_ARRAY_TASK_ID" "$full_exit_code" > "$root/STOP") 2>/dev/null; then
      scancel "$SLURM_ARRAY_JOB_ID" || true
    fi
  fi
}
trap stop_failed_worker EXIT
[[ ! -f "$root/STOP" ]] || exit 3
: "${FULL_ARITHMETIC_SELECTION:?frozen full-selection root}"
: "${FULL_LRA_SHARDS:?LRA shard count}"
: "${FULL_LIA_SHARDS:?LIA shard count}"
: "${CHECKER_SWEEP_BUILD_REPORT:?qualified updated-checker build hashes}"
[[ $FULL_LRA_SHARDS =~ ^[1-9][0-9]*$ && $FULL_LIA_SHARDS =~ ^[1-9][0-9]*$ ]]
(( SLURM_ARRAY_TASK_ID < FULL_LRA_SHARDS + FULL_LIA_SHARDS ))
if (( SLURM_ARRAY_TASK_ID < FULL_LRA_SHARDS )); then
  export ARITHMETIC_LOGIC=QF_LRA
  shard=$SLURM_ARRAY_TASK_ID
  checker=$MICROMATH_LRA_DIR
else
  export ARITHMETIC_LOGIC=QF_LIA
  shard=$((SLURM_ARRAY_TASK_ID - FULL_LRA_SHARDS))
  checker=$MICROMATH_LIA_DIR
fi
manifest="$FULL_ARITHMETIC_SELECTION/$ARITHMETIC_LOGIC"
export ARITHMETIC_SELECTION_FILE="$manifest/shard-$shard.json"
export ARITHMETIC_OUTPUT_DIR="$root/$ARITHMETIC_LOGIC/shard-$shard"
mkdir -p "$ARITHMETIC_OUTPUT_DIR"
(cd "$checker" && sha256sum -c "$CHECKER_SWEEP_BUILD_REPORT/binaries.sha256") > "$ARITHMETIC_OUTPUT_DIR/checker-build-check.txt"
python3 - "$manifest" "$shard" "$ARITHMETIC_LOGIC" <<'PY'
import hashlib
import json
import sys
from pathlib import Path
root = Path(sys.argv[1])
name = f'shard-{sys.argv[2]}.json'
expected = json.loads((root / 'manifest-hashes.json').read_text())[name]
blob = (root / name).read_bytes()
saved = json.loads(blob)
if hashlib.sha256(blob).hexdigest() != expected['sha256'] or len(saved['cases']) != expected['cases'] or saved['logic'] != sys.argv[3]:
    raise ValueError('frozen shard identity mismatch')
PY
sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{slurm_full_arithmetic_certificates.sbatch,slurm_arithmetic_certificate_stress.sbatch} \
  "$ARITHMETIC_SELECTION_FILE" "$manifest/selection.json" > "$ARITHMETIC_OUTPUT_DIR/sweep.sha256"
export ARITHMETIC_REFERENCE_SECONDS=60
export ARITHMETIC_EXPORT_SECONDS=120
export ARITHMETIC_CHALLENGE_SECONDS=60
export ARITHMETIC_PROOF_SECONDS=120
bash "$ARITHMETIC_WORKFLOW_DIR/slurm_arithmetic_certificate_stress.sbatch"
