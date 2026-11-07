#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-proof-capture
#SBATCH --array=0-2%3
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${CAPTURE_WORKFLOW:?frozen capture workflow}"
: "${LRA_SWEEP_RESULTS:?original sweep artifacts}"
: "${LRA_SWEEP_FEEDBACK:?independent audit artifacts}"
: "${MICROMATH_LRA_DIR:?pinned checker checkout}"
: "${PRIMO_SAT_SOLVER:?LRAT-capable CaDiCaL}"
cd "$PRIMO_WORKDIR"
python3 "$CAPTURE_WORKFLOW/capture_micromath_lra.py" \
  --results "$LRA_SWEEP_RESULTS" --feedback "$LRA_SWEEP_FEEDBACK" \
  --micromath "$MICROMATH_LRA_DIR" --cadical "$PRIMO_SAT_SOLVER" \
  --case-index "$SLURM_ARRAY_TASK_ID" --stage-seconds 300 \
  --raw-proof-bytes "${CAPTURE_RAW_BYTES:-2147483648}" \
  --gzip-proof-bytes "${CAPTURE_GZIP_BYTES:-268435456}" \
  --out "$PWD/results/lra-proof-capture-$SLURM_ARRAY_JOB_ID/case-$SLURM_ARRAY_TASK_ID"
