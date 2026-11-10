#!/usr/bin/env bash
# Real-seed mutation correctness checks under the repaired strong profile.
#SBATCH --job-name=primo-model-repair-mutations
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:25:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
out=${PRIMO_REPAIR_OUT:?}
mkdir -p "$out"
export PRIMO_MUTATION_OUT="$out"
python3 - <<'PY'
import hashlib
import json
import os
import shutil
from pathlib import Path
out = Path(os.environ['PRIMO_MUTATION_OUT'])
root = Path(os.environ['PRIMO_CORPUS_ROOT'])
rows = json.loads(Path(os.environ['PRIMO_MUTATION_MANIFEST']).read_text())
(out / 'seeds').mkdir(exist_ok=False)
for index, row in enumerate(rows):
    source = root / row['file']
    assert hashlib.sha256(source.read_bytes()).hexdigest() == row['sha256']
    shutil.copyfile(source, out / 'seeds' / f'seed-{index}.smt2')
(out / 'seeds.json').write_text(json.dumps(rows, indent=2) + '\n')
PY
flags=(--solver-arg=--quiet --solver-arg=--preprocessing-mode=strong --solver-arg=--check-model)
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
python3 -m pip show yinyang z3-solver > "$out/mutator-version.txt"
python3 scripts/check_solver_seed_corpus.py --seeds "$out/seeds" --solver build-slurm-rel/primo \
 "${flags[@]}" --ref contrib/local/bin/z3 --timeout 30 --out "$out/seed-answers.csv"
python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
 --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false --timeout 30 \
 --dolmen-timeout 30 --out "$out/seed-models" "$out"/seeds/*.smt2
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver "$PWD/build-slurm-rel/primo --quiet --preprocessing-mode=strong --check-model" \
 --ref 'z3 model_validate=true' --seconds 300 --iterations 40 --solver-timeout 5 \
 --file-size-limit 2000000 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
 --random-seed 202611550 --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 --timeout 10 --out "$out/mutant-answers.csv"
python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
 --require-dolmen-pass --dolmen-arg=--strict=false --timeout 10 --dolmen-timeout 10 \
 --out "$out/mutant-models" "$out"/scratch/*.smt2
python3 - "$out/scratch" <<'PY'
from pathlib import Path
import sys
count = len(list(Path(sys.argv[1]).glob('*.smt2')))
assert count >= 100, count
print('retained mutations:', count)
PY
if [[ -d $out/bugs && -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
sha256sum --check "$out/binaries.sha256"
