#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-cube-mutations
#SBATCH --ntasks=1
#SBATCH --array=0-3%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
profiles=(uniform difference m124-uniform m124-difference)
profile=${profiles[$slot]}
mode=${profile##*-}
flags=("--lia-cube=$mode")
if [[ $profile == m124-* ]]; then
 flags+=(--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves)
fi
out="${PRIMO_CUBE_OUT:?}/$profile"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/mutations.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
export PRIMO_MUTATION_OUT="$out"
python3 - <<'PY'
import hashlib
import json
import os
import shutil
from pathlib import Path
out = Path(os.environ['PRIMO_MUTATION_OUT'])
root = Path(os.environ['PRIMO_CORPUS_ROOT'])
entries = json.loads(Path('benchmarks/lia-cubes-20261108/selection.json').read_text())['inputs']
selected, families = [], set()
for entry in entries:
    family = entry['file'].split('/')[0]
    source = root / entry['file']
    if entry['bucket'] != 'easy-controls' or family in families or source.stat().st_size > 200000:
        continue
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256']
    families.add(family)
    selected.append(entry)
    if len(selected) == 10:
        break
assert len(selected) == 10, 'need ten distinct real-corpus families'
(out / 'seeds').mkdir()
for index, entry in enumerate(selected):
    shutil.copyfile(root / entry['file'], out / 'seeds' / f'seed-{index:03d}.smt2')
(out / 'seed-provenance.json').write_text(json.dumps(selected, indent=2) + '\n')
(out / 'regressions').mkdir()
for source in [*Path('benchmarks/smoke/qf_lia').glob('*.smt2'),
               Path('benchmarks/regressions/qf_lia_scaled_ite_arena_growth.smt2')]:
    shutil.copyfile(source, out / 'regressions' / source.name)
PY
solver_flags=()
for flag in "${flags[@]}"; do solver_flags+=(--solver-arg="$flag"); done
for stage in regressions seeds; do
 python3 scripts/check_solver_seed_corpus.py --seeds "$out/$stage" --solver build-slurm-rel/primo \
  --solver-arg=--quiet "${solver_flags[@]}" --ref contrib/local/bin/z3 --timeout 30 \
  --out "$out/$stage-answers.csv"
done
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver "$PWD/build-slurm-rel/primo --quiet ${flags[*]}" --ref 'z3 model_validate=true' \
 --seconds 900 --iterations 20 --solver-timeout 5 --file-size-limit 200000 \
 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
 --random-seed "$((2026110890 + slot))" --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo --solver-arg=--quiet "${solver_flags[@]}" \
 --ref contrib/local/bin/z3 --timeout 10 --out "$out/mutant-answers.csv"
for stage in regressions seeds mutant; do
 python3 scripts/select_fuzz_sat_inputs.py "$out/$stage-answers.csv" --out "$out/$stage-sat.txt"
 if [[ -s $out/$stage-sat.txt ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet "${solver_flags[@]}" \
   --require-dolmen-pass --expect-sat --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
   --manifest "$out/$stage-sat.txt" --out "$out/$stage-models"
 fi
done
python3 - "$out/scratch" <<'PY'
import sys
from pathlib import Path
count = len(list(Path(sys.argv[1]).rglob('*.smt2')))
assert count >= 200, count
print('retained mutations:', count)
PY
if [[ -d $out/bugs ]] && [[ -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
