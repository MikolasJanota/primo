#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-certificate-sweep
#SBATCH --array=0-7%4
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${PRIMO_EXPECTED_GIT:?native binary revision}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen sweep workflow}"
: "${ARITHMETIC_HISTORY_ROOT:?completed native timing records}"
: "${LRA_CORPUS:?QF_LRA corpus}"
cd "$PRIMO_WORKDIR"
stage=${LRA_SWEEP_STAGE:-run}
if [[ $stage == select ]]; then
  : "${LRA_SWEEP_BASELINE_SELECTION:?previous 24-case selection}"
  out="$PWD/results/lra-certificate-selection-$SLURM_ARRAY_JOB_ID"
  python3 "$ARITHMETIC_WORKFLOW_DIR/select_arithmetic_certificate_sweep.py" \
    --history "$ARITHMETIC_HISTORY_ROOT/QF_LRA/primo" --corpus "$LRA_CORPUS" \
    --historical-git 5efbd02293e15a53fbaad748ceda78bc7d27bbac \
    --baseline-selection "$LRA_SWEEP_BASELINE_SELECTION" --count "${LRA_SWEEP_COUNT:-256}" \
    --max-per-family "${LRA_SWEEP_MAX_PER_FAMILY:-256}" --shards 8 --out "$out"
  sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{select_arithmetic_certificate_sweep.py,arithmetic_certificate_stress.py,slurm_lra_certificate_sweep.sbatch} > "$out/selection-workflow.sha256"
  exit 0
fi
[[ $stage == run ]] || { echo "unknown sweep stage: $stage" >&2; exit 2; }
: "${LRA_SWEEP_MANIFEST_ROOT:?frozen selection and shard hashes}"
export ARITHMETIC_LOGIC=QF_LRA
export ARITHMETIC_SELECTION_FILE="$LRA_SWEEP_MANIFEST_ROOT/shard-$SLURM_ARRAY_TASK_ID.json"
export ARITHMETIC_OUTPUT_DIR="$PWD/results/lra-certificate-sweep-$SLURM_ARRAY_JOB_ID/shard-$SLURM_ARRAY_TASK_ID"
mkdir -p "$ARITHMETIC_OUTPUT_DIR"
sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{select_arithmetic_certificate_sweep.py,slurm_lra_certificate_sweep.sbatch} "$ARITHMETIC_SELECTION_FILE" "$LRA_SWEEP_MANIFEST_ROOT/selection.json" > "$ARITHMETIC_OUTPUT_DIR/sweep.sha256"
python3 - "$LRA_SWEEP_MANIFEST_ROOT" "$SLURM_ARRAY_TASK_ID" <<'PY'
import hashlib
import json
import sys
from pathlib import Path
root = Path(sys.argv[1])
name = f"shard-{sys.argv[2]}.json"
expected = json.loads((root / "manifest-hashes.json").read_text())[name]
assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected["sha256"]
assert len(json.loads((root / name).read_text())["cases"]) == expected["cases"]
PY
bash "$ARITHMETIC_WORKFLOW_DIR/slurm_arithmetic_certificate_stress.sbatch"
