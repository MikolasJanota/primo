#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-probes
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
out=${PRIMO_HYBRID_PROBES_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - "$out" "$PRIMO_HYBRID_PROBES_DIR" <<'PY'
import json
from pathlib import Path
import subprocess
import sys
out, inputs = map(Path, sys.argv[1:])
flags = ['--quiet', '--preprocessing-mode=off', '--lia-branch-limit=1',
         '--lia-patch-limit=0', '--lia-gomory-cut-limit=0', '--no-lia-round-integer-bounds']
rows = []
for mode, expected in [('private', 'unknown'), ('hybrid', 'sat')]:
    args = flags if mode == 'private' else flags + ['--lia-branching=hybrid', '--lia-private-branch-factor=0']
    command = ['build-slurm-rel/primo', *args, str(inputs / 'hybrid_multiple_splits.smt2')]
    run = subprocess.run(command, text=True, capture_output=True, timeout=30)
    assert run.returncode == 0 and run.stdout.strip() == expected, (command, run)
    rows.append({'mode': mode, 'command': command, 'answer': run.stdout.strip()})
(out / 'mechanism.json').write_text(json.dumps(rows, indent=2)+'\n')
subprocess.run(['python3', 'scripts/validate_incremental.py', '--solver', 'build-slurm-rel/primo',
                *(f'--solver-arg={arg}' for arg in flags + ['--lia-branching=hybrid', '--lia-private-branch-factor=0']),
                '--ref', 'contrib/local/bin/z3', '--dolmen', 'contrib/local/bin/dolmen',
                '--timeout', '30', '--dolmen-timeout', '30', '--out', str(out / 'scopes'),
                str(inputs / 'hybrid_scopes.smt2')], check=True)
PY
