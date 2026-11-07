#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-cert-sweep
#SBATCH --array=0-11%4
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen sweep helpers}"
: "${ARITHMETIC_HISTORY_ROOT:?recorded native timing root}"
cd "$PRIMO_WORKDIR"
if [[ ${ARITHMETIC_SWEEP_STAGE:-run} == select ]]; then
  : "${LIA_CORPUS:?existing QF_LIA corpus}"
  : "${LIA_SWEEP_CONTROLS:?previous24case selection}"
  out="$PWD/results/arithmetic-certificate-selection-$SLURM_ARRAY_JOB_ID/QF_LIA"
  python3 "$ARITHMETIC_WORKFLOW_DIR/select_arithmetic_certificate_sweep.py" \
    --logic QF_LIA --history "$ARITHMETIC_HISTORY_ROOT/QF_LIA/primo" \
    --corpus "$LIA_CORPUS" --baseline-selection "$LIA_SWEEP_CONTROLS" \
    --historical-git 5efbd02293e15a53fbaad748ceda78bc7d27bbac \
    --count 128 --max-per-family 128 --shards 4 --out "$out"
  sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{select_arithmetic_certificate_sweep.py,arithmetic_certificate_stress.py,slurm_arithmetic_certificate_sweep.sbatch} > "$out/selection-workflow.sha256"
  exit 0
fi
[[ ${ARITHMETIC_SWEEP_STAGE:-run} == run ]] || { echo 'invalid sweep stage' >&2; exit 2; }
: "${LRA_SWEEP_SELECTION:?frozen217case manifest root}"
: "${LIA_SWEEP_SELECTION:?new LIA manifest root}"
: "${CHECKER_SWEEP_BUILD_REPORT:?qualified updated-checker build hashes}"
if (( SLURM_ARRAY_TASK_ID < 8 )); then
  export ARITHMETIC_LOGIC=QF_LRA
  manifest=$LRA_SWEEP_SELECTION
  shard=$SLURM_ARRAY_TASK_ID
else
  export ARITHMETIC_LOGIC=QF_LIA
  manifest=$LIA_SWEEP_SELECTION
  shard=$((SLURM_ARRAY_TASK_ID - 8))
fi
export ARITHMETIC_SELECTION_FILE="$manifest/shard-$shard.json"
export ARITHMETIC_OUTPUT_DIR="$PWD/results/arithmetic-certificate-sweep-$SLURM_ARRAY_JOB_ID/$ARITHMETIC_LOGIC/shard-$shard"
mkdir -p "$ARITHMETIC_OUTPUT_DIR"
if [[ $ARITHMETIC_LOGIC == QF_LRA ]]; then checker=$MICROMATH_LRA_DIR; else checker=$MICROMATH_LIA_DIR; fi
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
assert hashlib.sha256(blob).hexdigest() == expected['sha256']
assert len(saved['cases']) == expected['cases']
assert saved['logic'] == sys.argv[3]
PY
sha256sum "$ARITHMETIC_WORKFLOW_DIR/slurm_arithmetic_certificate_sweep.sbatch" \
  "$ARITHMETIC_SELECTION_FILE" "$manifest/selection.json" > "$ARITHMETIC_OUTPUT_DIR/sweep.sha256"
bash "$ARITHMETIC_WORKFLOW_DIR/slurm_arithmetic_certificate_stress.sbatch"
