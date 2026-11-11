#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-mutations
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
logic=${PRIMO_MUTATION_LOGIC:?}
out="${PRIMO_HYBRID_OUT:?}/mutations/$logic"
mkdir -p "$out/seeds"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - "$logic" "$PRIMO_CORPUS_ROOT" "$out" <<'PY'
import hashlib
import json
from pathlib import Path
import shutil
import sys
from collections import defaultdict
logic, corpus, output = sys.argv[1:]
root, out = Path(corpus), Path(output)
if logic == 'QF_LIA':
    selected = json.loads(Path('benchmarks/lia-cubes-20261108/selection.json').read_text())['inputs']
else:
    selected = json.loads(Path('benchmarks/uflia-smtcomp2026-20261111/selection.json').read_text())['cases']
families = defaultdict(list)
for case in selected:
    path = root / case['file']
    if path.stat().st_size > 200000:
        continue
    if logic == 'QF_LIA' and case['bucket'] != 'easy-controls':
        continue
    families[case['file'].split('/')[0]].append((path.stat().st_size, case))
for family in families.values():
    family.sort(key=lambda item: (item[0], item[1]['file']))
assert sum(len(family) for family in families.values()) >= 12
copied = []
while len(copied) < 12:
  for name in sorted(families):
    if not families[name] or len(copied) >= 12:
        continue
    _, case = families[name].pop(0)
    path = root / case['file']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == case['sha256']
    shutil.copyfile(path, out / 'seeds' / f'{len(copied):03d}.smt2')
    copied.append(case)
assert len(copied) == 12
(out / 'seeds.json').write_text(json.dumps(copied, indent=2)+'\n')
PY
flags=(--solver-arg=--quiet --solver-arg=--lia-branching=hybrid
       --solver-arg=--lia-private-branch-factor=0 --solver-arg=--lia-patch-limit=0
       --solver-arg=--lia-gomory-cut-limit=0 --solver-arg=--check-model)
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --solver "$PWD/build-slurm-rel/primo --quiet --lia-branching=hybrid --lia-private-branch-factor=0 --lia-patch-limit=0 --lia-gomory-cut-limit=0 --check-model" \
 --ref "$PWD/contrib/local/bin/z3 model_validate=true" \
 --seconds "${PRIMO_HYBRID_MUTATION_SECONDS:-300}" \
 --iterations "${PRIMO_HYBRID_MUTATION_ITERATIONS:-40}" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver-timeout 5 --file-size-limit 2000000 --bugs "$out/bugs" --scratch "$out/scratch" \
 --logs "$out/logs" --random-seed 2026111217 --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 --timeout 10 \
 --out "$out/answers.csv" || true
python3 - "$out" <<'PY'
from collections import Counter
import csv
import json
from pathlib import Path
import sys
out = Path(sys.argv[1])
with (out / 'answers.csv').open() as stream:
    rows = list(csv.DictReader(stream))
assert rows, 'no real-seed mutants replayed'
assert len(rows) == len(list((out / 'scratch').glob('*.smt2'))), 'incomplete replay'
assert all(row['outcome'] != 'DISAGREEMENT' for row in rows), 'wrong answer'
allowed = {'sat', 'unsat', 'timeout', 'unknown'}
assert all(row['ours'] in allowed and row['reference'] in allowed for row in rows), 'unclassified failure'
selected = [row['file'] for row in rows if row['ours'] == 'sat' and row['ours_exit'] == '0']
(out / 'sat.txt').write_text(''.join(path+'\n' for path in selected))
(out / 'audit.json').write_text(json.dumps({'cases': len(rows),
    'outcomes': dict(Counter(row['outcome'] for row in rows)),
    'native': dict(Counter(row['ours'] for row in rows)),
    'reference': dict(Counter(row['reference'] for row in rows)),
    'selected_sat': len(selected)}, indent=2)+'\n')
PY
if [[ -s $out/sat.txt ]]; then
python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
 --expect-sat --manifest "$out/sat.txt" \
 --require-dolmen-pass --dolmen-arg=--strict=false --timeout 20 --dolmen-timeout 20 \
 --out "$out/models"
fi
if [[ -d $out/bugs && -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
