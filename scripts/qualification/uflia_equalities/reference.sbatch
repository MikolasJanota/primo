#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-reference
#SBATCH --partition=compute
#SBATCH --array=0-7%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_EQ_OUT:?}
mkdir -p "$out"
contrib/local/bin/z3 --version > "$out/z3-${SLURM_ARRAY_TASK_ID:?}.version"
sha256sum contrib/local/bin/z3 > "$out/z3-$SLURM_ARRAY_TASK_ID.sha256"
python3 scripts/bench_qf_uf_full.py --repo "${PRIMO_CORPUS_REPO:?}" --root "${PRIMO_CORPUS_ROOT:?}" \
 --solver "$PWD/contrib/local/bin/z3" --solver-arg=-smt2 --timeout 60 \
 --shards 8 --shard-index "$SLURM_ARRAY_TASK_ID" \
 --out "$out/shard-$SLURM_ARRAY_TASK_ID.csv" --summary "$out/shard-$SLURM_ARRAY_TASK_ID-summary.csv"
