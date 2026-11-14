#!/usr/bin/env bash
#SBATCH --job-name=primo-rowcache-mutations
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%A_%a.log
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
logics=(QF_LIA QF_UFLIA QF_LRA QF_UFLRA)
slot=${SLURM_ARRAY_TASK_ID:?}
[[ $slot -ge 0 && $slot -lt 8 ]]
logic=${logics[$((slot%4))]}
profile=rows
flags=(--quiet --lra-propagation --lra-row-propagation --lra-bidirectional-row-propagation --check-model)
if [[ $slot -ge 4 ]]; then
 profile=both
 flags+=(--preprocess-bound-tightening)
fi
out="${PRIMO_MUTATION_OUT:?}/$logic-$profile"
test ! -e "$out"
mkdir -p "$out/seeds"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/executables.sha256"
python3 - "$logic" "$PRIMO_MUTATION_CANDIDATES" "$PRIMO_MUTATION_ROOTS" "$out" <<'PY'
import collections,hashlib,json,shutil,sys
from pathlib import Path
logic,candidates,roots,destination=sys.argv[1:]
root=Path(json.loads(Path(roots).read_text())[logic]);out=Path(destination)
families=collections.defaultdict(list)
for case in json.loads(Path(candidates).read_text())[logic]:
 p=root/case['file']
 if p.stat().st_size <= 200000:
  families[case['file'].split('/')[0]].append((p.stat().st_size,case))
for group in families.values():group.sort(key=lambda item:(item[0],item[1]['baseline_seconds'],item[1]['file']))
selected=[]
while len(selected)<12:
 progressed=False
 for family in sorted(families):
  if not families[family] or len(selected)==12:continue
  _,case=families[family].pop(0);p=root/case['file']
  assert hashlib.sha256(p.read_bytes()).hexdigest()==case['sha256']
  shutil.copyfile(p,out/'seeds'/f'{len(selected):03d}.smt2');selected.append(case);progressed=True
 assert progressed,'fewer than12eligible real seeds'
(out/'seeds.json').write_text(json.dumps(selected,indent=2)+'\n')
PY
solver_args=()
for flag in "${flags[@]}"; do solver_args+=("--solver-arg=$flag"); done
seed_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$out/seeds" --solver build-slurm-rel/primo \
 "${solver_args[@]}" --ref contrib/local/bin/z3 --timeout 10 --out "$out/seed-answers.csv" || seed_status=$?
if [[ $seed_status != 0 ]]; then
 python3 scripts/classify_mutation_resources.py answers "$out/seed-answers.csv"
fi
base=${PRIMO_MUTATION_SEED_BASE:-2026111600}
[[ $base =~ ^[0-9]+$ ]]
seed=$((base+slot))
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --solver "$PWD/build-slurm-rel/primo ${flags[*]}" --ref "$PWD/contrib/local/bin/z3 model_validate=true" \
 --seconds 600 --iterations 30 --solver-timeout 5 --file-size-limit 2000000 \
 --config scripts/fuzz_configs/model_linear_ops.txt --bugs "$out/bugs" --scratch "$out/scratch" \
 --logs "$out/logs" --random-seed "$seed" --keep-logic --hoist-declarations --keep-mutants
answer_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo "${solver_args[@]}" --ref contrib/local/bin/z3 --timeout 10 \
 --out "$out/mutant-answers.csv" || answer_status=$?
if [[ $answer_status != 0 ]]; then
 python3 scripts/classify_mutation_resources.py answers "$out/mutant-answers.csv"
fi
python3 - "$out" <<'PY'
import collections,csv,json,sys
from pathlib import Path
out=Path(sys.argv[1]);rows=list(csv.DictReader((out/'mutant-answers.csv').open()))
assert len(rows)>=100,'fewer than100real-seed mutants'
selected=[r['file'] for r in rows if r['ours']=='sat' and r['ours_exit']=='0']
(out/'sat.txt').write_text(''.join(p+'\n' for p in selected))
(out/'audit.json').write_text(json.dumps({'cases':len(rows),'outcomes':dict(collections.Counter(r['outcome'] for r in rows)),
 'native':dict(collections.Counter(r['ours'] for r in rows)),'reference':dict(collections.Counter(r['reference'] for r in rows)),
 'selected_sat':len(selected)},indent=2)+'\n')
PY
if [[ -s $out/sat.txt ]]; then
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${solver_args[@]}" \
  --expect-sat --manifest "$out/sat.txt" --require-dolmen-pass --dolmen-arg=--strict=false \
  --timeout 30 --dolmen-timeout 30 --out "$out/models"
fi
# Resource-only collection is retained as incomplete, never silently a pass.
[[ $seed_status == 0 && $answer_status == 0 ]]
if [[ -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
