#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-mutation-triage
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import json
import os
from pathlib import Path
import subprocess

out = Path(os.environ['PRIMO_HYBRID_TRIAGE_OUT'])
out.mkdir(parents=True, exist_ok=False)
mutations = Path(os.environ['PRIMO_HYBRID_MUTATION_ROOT'])
rows = []
for logic in ('QF_LIA', 'QF_UFLIA'):
    for path in sorted((mutations / logic / 'scratch').glob('*.smt2')):
        item = {'logic': logic, 'file': str(path)}
        commands = {
            'private': [os.environ['PRIMO_BASELINE_SOLVER'], '--quiet'],
            'hybrid': ['build-slurm-rel/primo', '--quiet', '--lia-branching=hybrid',
                       '--lia-private-branch-factor=0', '--lia-patch-limit=0', '--lia-gomory-cut-limit=0'],
            'z3': ['contrib/local/bin/z3'],
        }
        for label, command in commands.items():
            try:
                run = subprocess.run([*command, str(path)], text=True, capture_output=True, timeout=2)
                item[label] = {'exit': run.returncode, 'stdout': run.stdout[:4096], 'stderr': run.stderr[:4096]}
            except subprocess.TimeoutExpired:
                item[label] = {'timeout': True}
        rows.append(item)
        (out / 'progress.json').write_text(json.dumps(rows, indent=2)+'\n')
(out / 'complete').write_text('Diagnostic outputs; classification remains required.\n')
PY
