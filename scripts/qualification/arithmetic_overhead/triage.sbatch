#!/usr/bin/env bash
# Replay exceptional generated rows against both pristine and candidate solvers.
#SBATCH --job-name=primo-arithmetic-overhead-triage
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PY'
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd()))
from scripts.bench_qf_uf_full import run_solver

config = json.loads(Path(os.environ['PRIMO_TRIAGE_CONFIG']).read_text())
root = Path(os.environ['PRIMO_OVERHEAD_OUT'])
exceptions = json.loads((root / 'exceptions.json').read_text())
out = root / 'triage'
out.mkdir(exist_ok=False)
results = []
seen = set()
for entry in exceptions:
    row = entry['row']
    path = Path(row.get('case') or row['file']).resolve()
    profile = 'm124' if '/lia-m124/' in str(path) else 'default'
    identity = hashlib.sha256(path.read_bytes()).hexdigest()
    if (identity, profile) in seen:
        continue
    seen.add((identity, profile))
    flags = ([] if profile == 'default' else ['--lia-round-integer-bounds',
             '--lia-primitive-int-rows', '--no-lia-integer-slacks',
             '--lia-patch-integral-moves', '--lia-cube=uniform'])
    case = dict(file=str(path), sha256=identity, profile=profile, results={})
    answers = set()
    for arm in ('baseline', 'candidate', 'z3'):
        command = [config[arm]] + ([] if arm == 'z3' else ['--quiet', '--stats', *flags])
        result = run_solver(command, path, 60)
        case['results'][arm] = result
        assert result['status'] in {'ok', 'timeout'}, result
        if result['status'] == 'ok' and result['answer'] in {'sat', 'unsat'}:
            assert result['exit_code'] == 0
            answers.add(result['answer'])
    results.append(case)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    assert len(answers) <= 1, 'contradictory successful answers'
    for arm in ('baseline', 'candidate'):
        result = case['results'][arm]
        if result['answer'] == 'unknown':
            assert 'node limit' in result['stderr'], result
    assert case['results']['baseline']['answer'] == case['results']['candidate']['answer']
# Two initial LIA answer shards stopped on a reference timeout before model
# validation. Validate their successful SAT rows without replacing timing data.
for profile in ('lia-default', 'lia-m124'):
    source = root / 'fuzz' / profile / 'fuzzsmt/shard-2/summary.csv'
    rows = list(csv.DictReader(source.open()))
    manifest = out / (profile + '-sat.txt')
    manifest.write_text(''.join(str(Path(r['file']).resolve()) + '\n' for r in rows
                              if r['outcome'] == 'PASS' and r['ours'] == 'sat'))
    flags = ([] if profile == 'lia-default' else ['--lia-round-integer-bounds',
             '--lia-primitive-int-rows', '--no-lia-integer-slacks',
             '--lia-patch-integral-moves', '--lia-cube=uniform'])
    subprocess.run(['python3', 'scripts/validate_model.py', '--solver', config['candidate'],
                    '--solver-arg=--quiet', *['--solver-arg=' + flag for flag in flags],
                    '--require-dolmen-pass', '--expect-sat', '--dolmen-arg=--strict=false',
                    '--timeout', '30', '--dolmen-timeout', '30', '--manifest', str(manifest),
                    '--out', str(out / (profile + '-models'))], check=True)
(out / 'complete').write_text('exceptional answers triaged; missing SAT stages replayed\n')
PY
