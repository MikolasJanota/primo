#!/usr/bin/env bash
# Five real seeds per task; private JSON supplies their content identities.
#SBATCH --job-name=primo-arithmetic-overhead-mutations
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
profiles=(lia-default lia-m124 uflia-default uflia-m124 lra-default uflra-default)
profile=${profiles[${SLURM_ARRAY_TASK_ID:?}]}
export PRIMO_MUTATION_PROFILE=$profile
out="${PRIMO_OVERHEAD_OUT:?}/mutations/$profile"
export PRIMO_MUTATION_OUT=$out
mkdir -p "$out"
exec > >(tee "$out/mutations.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - <<'PY'
import hashlib
import json
import os
import shutil
from pathlib import Path

config = json.loads(Path(os.environ['PRIMO_MUTATION_CONFIG']).read_text())
logic = os.environ['PRIMO_MUTATION_PROFILE'].split('-')[0]
entries = config[logic]
assert len(entries) == 5
out = Path(os.environ['PRIMO_MUTATION_OUT'])
(out / 'seeds').mkdir()
for index, entry in enumerate(entries):
    source = Path(entry['path'])
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256']
    shutil.copyfile(source, out / 'seeds' / f'seed-{index:03d}.smt2')
(out / 'seed-provenance.json').write_text(json.dumps(entries, indent=2) + '\n')
PY
flags=()
if [[ $profile == *-m124 ]]; then
 flags=(--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves --lia-cube=uniform)
fi
args=()
for flag in "${flags[@]}"; do args+=(--solver-arg="$flag"); done
python3 scripts/check_solver_seed_corpus.py --seeds "$out/seeds" --solver build-slurm-rel/primo \
 --solver-arg=--quiet "${args[@]}" --ref z3 --timeout 10 --out "$out/seeds-answers.csv"
scripts/yinyang_fuzz.sh --seeds "$out/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
 --config scripts/fuzz_configs/model_linear_ops.txt \
 --solver "$PWD/build-slurm-rel/primo --quiet ${flags[*]}" --ref 'z3 model_validate=true' \
 --seconds 900 --iterations "${PRIMO_MUTATION_ITERATIONS:-20}" --solver-timeout 5 --file-size-limit 200000 \
 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
 --random-seed "$((2026110990 + SLURM_ARRAY_TASK_ID))" --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" --ignore-declared-status \
 --solver build-slurm-rel/primo --solver-arg=--quiet "${args[@]}" --ref z3 --timeout 10 \
 --out "$out/mutant-answers.csv"
for stage in seeds mutant; do
 python3 scripts/select_fuzz_sat_inputs.py "$out/$stage-answers.csv" --out "$out/$stage-sat.txt"
 if [[ -s $out/$stage-sat.txt ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet "${args[@]}" \
   --require-dolmen-pass --expect-sat --dolmen-arg=--strict=false --timeout 20 --dolmen-timeout 20 \
   --manifest "$out/$stage-sat.txt" --out "$out/$stage-models"
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
