#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-diagnostic-models
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=00:55:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PYCODE'
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

slot = os.environ['SLURM_ARRAY_TASK_ID']
parent = Path(os.environ['PRIMO_EQ_PARENT']) / ('case-' + slot)
assert (parent / 'complete').is_file()
identity = json.loads((parent / 'provenance.json').read_text())
rows = [json.loads(line) for line in (parent / 'rows.jsonl').read_text().splitlines()]
selected = [r for r in rows if r['status'] == 'ok' and r['answer'] == 'sat' and r['exit_code'] == 0]
out = Path(os.environ['PRIMO_EQ_OUT']) / ('case-' + slot)
out.mkdir(parents=True, exist_ok=False)
coverage = {'expected': len(selected), 'passed': 0, 'rows': [], 'status': 'running'}
for row in selected:
    solver, *flags, source = row['command']
    assert hashlib.sha256(Path(solver).read_bytes()).hexdigest() == identity['binary_sha256']
    assert hashlib.sha256(Path(source).read_bytes()).hexdigest() == identity['input_sha256']
    label = row['variant'] + '-' + row['role']
    manifest = out / (label + '.txt')
    manifest.write_text(source + '\n')
    target = out / label
    command = [sys.executable, 'scripts/validate_model.py', '--solver', solver,
               *['--solver-arg=' + arg for arg in flags], '--manifest', str(manifest),
               '--out', str(target), '--expect-sat', '--require-dolmen-pass',
               '--timeout', '180', '--dolmen-timeout', '30', '--dolmen-arg=--strict=false']
    process = subprocess.run(command, check=False)
    with (target / 'summary.csv').open(newline='') as stream:
        results = list(csv.DictReader(stream))
    passed = process.returncode == 0 and len(results) == 1 and results[0]['outcome'] == 'pass' and results[0]['validator'] == 'dolmen' and results[0]['answer'] == 'sat'
    coverage['rows'].append({'label': label, 'command': command, 'exit_code': process.returncode,
                             'results': results, 'passed': passed})
    coverage['passed'] += int(passed)
    coverage['status'] = 'running' if passed else 'failed-or-incomplete'
    (out / 'coverage.json').write_text(json.dumps(coverage, indent=2) + '\n')
    if not passed:
        raise SystemExit('diagnostic model replay needs classification')
coverage['status'] = 'complete'
(out / 'coverage.json').write_text(json.dumps(coverage, indent=2) + '\n')
PYCODE
