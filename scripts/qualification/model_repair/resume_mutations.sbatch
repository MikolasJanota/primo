#!/usr/bin/env bash
# Complete validation of retained mutants after a short answer-timeout cutoff.
#SBATCH --job-name=primo-model-repair-mutation-replay
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
out=${PRIMO_REPAIR_OUT:?}
source_dir=${PRIMO_MUTANT_SOURCE:?}
mkdir -p "$out"
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/binaries.sha256"
flags=(--solver-arg=--quiet --solver-arg=--preprocessing-mode=strong --solver-arg=--check-model)
answer_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$source_dir" --ignore-declared-status \
 --solver build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 --timeout 60 \
 --out "$out/answers.csv" || answer_status=$?
model_status=0
python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
 --require-dolmen-pass --dolmen-arg=--strict=false --timeout 60 --dolmen-timeout 10 \
 --out "$out/models" "$source_dir"/*.smt2 || model_status=$?
python3 - "$out" "$answer_status" "$model_status" <<'PY'
from pathlib import Path
import csv,json,sys
root=Path(sys.argv[1])
with (root/'answers.csv').open(newline='') as stream: answers=list(csv.DictReader(stream))
with (root/'models/summary.csv').open(newline='') as stream: models=list(csv.DictReader(stream))
assert len(answers)==len(models)>=100
for row in answers:
    assert row['outcome']=='PASS' or (row['outcome']=='INCOMPLETE' and row['ours'] in ('timeout','unknown') and row['reference'] in ('sat','unsat')),row
for row in models:
    assert row['outcome'] in ('pass','not-sat','solver-timeout','solver-unknown'),row
by_file={Path(row['file']).name:row for row in models}
for row in answers:
    if row['ours']=='sat':
        model=by_file[Path(row['file']).name]
        assert model['outcome']=='pass' and model['validator']=='dolmen',model
(root/'completion.json').write_text(json.dumps({'answer_command_exit':int(sys.argv[2]),'model_command_exit':int(sys.argv[3]),'rows':len(answers),'coverage_join':'every successful primary SAT has a direct Dolmen pass; resource rows remain separately classified'},indent=2)+'\n')
PY
sha256sum --check "$out/binaries.sha256"
