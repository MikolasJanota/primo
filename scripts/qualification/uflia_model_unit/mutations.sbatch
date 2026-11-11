#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-model-unit-mutations
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
out=${PRIMO_UNIT_MUTATION_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
python3 - "$out" "$PRIMO_CORPUS_ROOT" <<'PY'
from pathlib import Path
import hashlib,json,shutil,sys
out=Path(sys.argv[1]);root=Path(sys.argv[2]);(out/'seeds').mkdir()
rows=json.loads(Path('benchmarks/model-repair-20261110/mutation-seeds.json').read_text())
for i,row in enumerate(rows):
 source=root/row['file'];assert hashlib.sha256(source.read_bytes()).hexdigest()==row['sha256']
 shutil.copyfile(source,out/'seeds'/f'seed-{i}.smt2')
(out/'seeds.json').write_text(json.dumps(rows,indent=2)+'\n')
PY
python3 -m pip show yinyang z3-solver > "$out/mutator-version.txt"
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver "$PWD/build-slurm-rel/primo --quiet --qf-uflia-tableau-equalities --qf-uflia-lia-equality-producer=model-unit --check-model" \
 --ref "$PWD/contrib/local/bin/z3 model_validate=true" --seconds 300 --iterations 40 --solver-timeout 5 \
 --file-size-limit 2000000 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
 --random-seed 2026111199 --keep-logic --hoist-declarations --keep-mutants
for policy in all fixed; do
 flags=(--solver-arg=--quiet --solver-arg=--qf-uflia-lia-equality-producer=model-unit \
        --solver-arg=--qf-uflia-tableau-equalities="$policy" --solver-arg=--check-model)
 python3 scripts/check_solver_seed_corpus.py --seeds "$out/seeds" --solver build-slurm-rel/primo \
  "${flags[@]}" --ref contrib/local/bin/z3 --timeout 30 --out "$out/seed-answers-$policy.csv"
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
  --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
  --out "$out/seed-models-$policy" "$out"/seeds/*.smt2
 python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
  --solver build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 --timeout 10 \
  --out "$out/answers-$policy.csv" || true
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
  --require-dolmen-pass --dolmen-arg=--strict=false --timeout 20 --dolmen-timeout 20 \
  --out "$out/models-$policy" "$out"/scratch/*.smt2 || true
 python3 "${PRIMO_UNIT_METHODS:?}/check_mutations.py" "$out" "$policy"
done
if [[ -d $out/bugs && -n $(find "$out/bugs" -type f -print -quit) ]]; then
 echo 'saved mutation failures require classification' >&2
 exit 1
fi
sha256sum --check "$out/binaries.sha256"
