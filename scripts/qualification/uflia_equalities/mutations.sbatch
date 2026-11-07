#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-mutations
#SBATCH --partition=compute
#SBATCH --array=0-1
#SBATCH --cpus-per-task=1
#SBATCH --mem=6G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_EQ_OUT:?}/shard-$slot"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/mutations.log") 2>&1
flags=()
if (( slot == 1 )); then
 flags+=(--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves)
fi
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
python3 -m pip show yinyang z3-solver > "$out/mutator-version.txt"
export PRIMO_MUTATION_OUT="$out"
python3 - <<'PY'
import hashlib
import json
import os
import shutil
from pathlib import Path
out = Path(os.environ['PRIMO_MUTATION_OUT'])
root = Path(os.environ['PRIMO_CORPUS_ROOT'])
(out / 'regressions').mkdir()
for source in Path('benchmarks').rglob('*.smt2'):
    if '(set-logic QF_UFLIA)' in source.read_text():
        shutil.copyfile(source, out / 'regressions' / source.name)
by_family = {}
manifest = Path('benchmarks/qf_uflia-smtlib-representative-20.txt')
for line in manifest.read_text().splitlines():
    name = line.split('#', 1)[0].strip()
    if name:
        by_family.setdefault(Path(name).parts[0], []).append(root / name)
selected = [min(paths, key=lambda p: (p.stat().st_size, str(p))) for paths in by_family.values()]
remaining = sorted((p for paths in by_family.values() for p in paths if p not in selected),
                   key=lambda p: (p.stat().st_size, str(p)))
selected = sorted(selected, key=lambda p: (p.stat().st_size, str(p)))[:5]
selected += remaining[:5 - len(selected)]
assert len(selected) == 5
(out / 'seeds').mkdir()
rows = []
for index, source in enumerate(selected):
    target = out / 'seeds' / ('seed-%03d.smt2' % index)
    shutil.copyfile(source, target)
    rows.append({'file': str(source.relative_to(root)), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'copy': target.name})
(out / 'seed-provenance.json').write_text(json.dumps(rows, indent=2) + '\n')
PY
solver_flags=()
for flag in "${flags[@]}"; do solver_flags+=(--solver-arg="$flag"); done
for stage in regressions seeds; do
 python3 scripts/check_solver_seed_corpus.py --seeds "$out/$stage" --solver build-slurm-rel/primo \
  --solver-arg=--quiet "${solver_flags[@]}" --ref contrib/local/bin/z3 --timeout 30 --out "$out/$stage-answers.csv"
done
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver "$PWD/build-slurm-rel/primo --quiet ${flags[*]}" --ref 'z3 model_validate=true' \
 --seconds 900 --iterations 20 --solver-timeout 5 --file-size-limit 2000000 \
 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
 --random-seed "$((2026110720 + slot))" --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo --solver-arg=--quiet "${solver_flags[@]}" \
 --ref contrib/local/bin/z3 --timeout 10 --out "$out/mutant-answers.csv"
for stage in regressions seeds mutant; do
 python3 scripts/select_fuzz_sat_inputs.py "$out/$stage-answers.csv" --out "$out/$stage-sat.txt"
 if [[ -s "$out/$stage-sat.txt" ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet "${solver_flags[@]}" \
   --dolmen contrib/local/bin/dolmen --require-dolmen-pass --expect-sat --dolmen-arg=--strict=false \
   --timeout 30 --dolmen-timeout 30 --manifest "$out/$stage-sat.txt" --out "$out/$stage-models"
 fi
done
python3 - "$out/scratch" <<'PY'
import sys
from pathlib import Path
count = len(list(Path(sys.argv[1]).rglob('*.smt2')))
assert count >= 100, count
print('retained mutations:', count)
PY
if [[ -d $out/bugs ]] && [[ -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
