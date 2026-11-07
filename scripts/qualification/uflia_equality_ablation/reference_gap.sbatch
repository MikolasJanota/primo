#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-reference-gap
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:04:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_EQ_OUT:?}
test ! -e "$out"
mkdir -p "$out"
name=20230314-Jaroslav-Bendik-Certora/41958_45c688a4814eb926c254_59_QF_UFLIA.smt2
printf '%s\n' "$name" > "$out/manifest.txt"
"${PRIMO_CVC5:?}" --version > "$out/cvc5-version.txt"
sha256sum "$PRIMO_CVC5" "$PRIMO_CORPUS_ROOT/$name" > "$out/identities.sha256"
python3 scripts/bench_qf_uf_full.py --repo "${PRIMO_CORPUS_ROOT:?}" --root "$PRIMO_CORPUS_ROOT" \
 --manifest "$out/manifest.txt" --solver "$PRIMO_CVC5" --solver-arg=--lang=smt2 \
 --timeout 120 --shards 1 --shard-index 0 --out "$out/cvc5.csv" \
 --summary "$out/cvc5-summary.csv" --log-dir "$out/logs"
python3 - "$out/cvc5.csv" <<'PY'
import csv
import sys
rows = list(csv.DictReader(open(sys.argv[1])))
assert len(rows) == 1
r = rows[0]
assert r['status'] == 'ok' and r['exit_code'] == '0' and r['answer'] == 'sat' and r['wrong'] == 'false', r
PY
sha256sum --check "$out/identities.sha256"
