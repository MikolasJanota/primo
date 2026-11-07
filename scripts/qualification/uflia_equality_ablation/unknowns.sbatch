#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-unknowns
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
export PRIMO_UNKNOWN_OUT="$out"
python3 - <<'PY'
import hashlib
import json
import os
import subprocess
from pathlib import Path
out = Path(os.environ['PRIMO_UNKNOWN_OUT'])
rows = []
for case in (650,700):
    source = Path('results/ablation-fuzz/off/generated/cases') / ('case-%05d.smt2' % case)
    for name, binary in [('old',os.environ['PRIMO_BASELINE_SOLVER']),('current','build-slurm-rel/primo')]:
        command = [binary,'--quiet','--stats','--arithmetic-model-phase=off',str(source)]
        proc = subprocess.run(command,text=True,capture_output=True,timeout=30)
        (out / ('%d-%s.stdout' % (case,name))).write_text(proc.stdout)
        (out / ('%d-%s.stderr' % (case,name))).write_text(proc.stderr)
        assert proc.returncode == 0 and proc.stdout.splitlines()[0] == 'unknown', (command,proc.stdout,proc.stderr)
        rows.append({'case':case,'profile':name,'command':command,'answer':'unknown','exit_code':proc.returncode,'input_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(Path(binary).read_bytes()).hexdigest(),'version':subprocess.check_output([binary,'--version'],text=True)})
(out / 'rows.json').write_text(json.dumps(rows,indent=2)+'\n')
PY
