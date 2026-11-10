#!/usr/bin/env bash
# Temporary diagnostic snapshots emit failing model terms; no timing claim.
#SBATCH --job-name=primo-model-diagnose
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_REPAIR_OUT:?}
mkdir -p "$out"
sha256sum build-slurm-rel/primo "${PRIMO_REPAIR_INPUT:?}" > "$out/identities.sha256"
set +e
timeout --signal=TERM --kill-after=5s 180s build-slurm-rel/primo \
 --quiet --check-model --preprocessing-mode=strong "$PRIMO_REPAIR_INPUT" \
 > "$out/solver.stdout" 2> "$out/solver.stderr"
status=$?
set -e
printf '%s\n' "$status" > "$out/exit-code"
sha256sum --check "$out/identities.sha256"
