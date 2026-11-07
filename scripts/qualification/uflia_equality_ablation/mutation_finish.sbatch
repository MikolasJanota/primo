#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-mutation-finish
#SBATCH --partition=compute
#SBATCH --array=0,3
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
modes=(scan names fixed all)
parent="${PRIMO_EQ_PARENT:?}/shard-$slot"
out="${PRIMO_EQ_OUT:?}/shard-$slot"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/finish.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - "$parent" "$out" <<'PY'
import csv
import json
import sys
from pathlib import Path
parent, out = map(Path, sys.argv[1:])
rows = list(csv.DictReader((parent / 'mutant-answers.csv').open()))
assert len(rows) == 100
incomplete = []
for row in rows:
    if row['outcome'] == 'PASS':
        continue
    assert row['outcome'] == 'INCOMPLETE' and row['ours'] == 'timeout' and row['ours_exit'] == '124'
    assert row['reference'] in ('sat', 'unsat') and row['reference_exit'] == '0'
    incomplete.append(row)
assert len(incomplete) == 1
(out / 'timeout-classification.json').write_text(json.dumps(incomplete, indent=2) + '\n')
(out / 'retry-seed').mkdir()
source = Path(incomplete[0]['file'])
(out / 'retry-seed' / source.name).write_bytes(source.read_bytes())
assert not (parent / 'bugs').is_dir() or not any(p.is_file() for p in (parent / 'bugs').rglob('*'))
PY
flags=(--solver-arg=--quiet --solver-arg=--arithmetic-model-phase=off "--solver-arg=--qf-uflia-tableau-equalities=${modes[$slot]}")
status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$out/retry-seed" --ignore-declared-status \
 --solver build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 \
 --timeout 60 --out "$out/retry-answers.csv" || status=$?
python3 - "$out/retry-answers.csv" "$status" <<'PY'
import csv
import sys
rows = list(csv.DictReader(open(sys.argv[1])))
assert len(rows) == 1
r = rows[0]
assert r['outcome'] == 'PASS' or (r['outcome'] == 'INCOMPLETE' and r['ours'] == 'timeout' and r['ours_exit'] == '124' and r['reference'] in ('sat', 'unsat') and r['reference_exit'] == '0'), r
assert int(sys.argv[2]) in (0, 1)
PY
for stage in regressions seeds mutant retry; do
 csv="$parent/$stage-answers.csv"
 if [[ $stage == retry ]]; then csv="$out/retry-answers.csv"; fi
 python3 scripts/select_fuzz_sat_inputs.py "$csv" --out "$out/$stage-sat.txt"
 if [[ -s $out/$stage-sat.txt ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
   --dolmen contrib/local/bin/dolmen --require-dolmen-pass --expect-sat --dolmen-arg=--strict=false \
   --timeout 120 --dolmen-timeout 30 --manifest "$out/$stage-sat.txt" --out "$out/$stage-models"
 fi
done
