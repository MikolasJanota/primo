#!/usr/bin/env bash
# Focused existing-option ablation motivated by the native preprocessing profiles.
#SBATCH --job-name=primo-arithmetic-flattening
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
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
from pathlib import Path

config = json.loads(Path(os.environ['PRIMO_REVIEW_CONFIG']).read_text())
index = [0, 1, 6, 11][int(os.environ['SLURM_ARRAY_TASK_ID'])]
case = config['cases'][index]
source = Path(case['root']) / case['file']
out = Path(os.environ['PRIMO_REVIEW_OUT']) / f'case-{index}'
out.mkdir(parents=True, exist_ok=False)
manifest = out / 'inputs.txt'
manifest.write_text(case['file'] + '\n')
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
assert digest(config['binary']) == config['binary_sha256']
input_hash = digest(source)
metadata = {'case_index': index, 'case': case, 'input_sha256': input_hash,
            'binary_sha256': config['binary_sha256'],
            'method_sha256': digest(os.environ['PRIMO_REVIEW_SCRIPT']),
            'job': os.environ['SLURM_ARRAY_JOB_ID'], 'task': os.environ['SLURM_ARRAY_TASK_ID'],
            'node': os.uname().nodename,
            'cpu_model': next(x.split(':',1)[1].strip() for x in Path('/proc/cpuinfo').read_text().splitlines() if x.startswith('model name'))}
(out / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
known = set()
replays = []
for round_id in range(2):
    order = [0, 256, 2048]
    offset = (index + round_id) % len(order)
    order = order[offset:] + order[:offset]
    for cap in order:
        target = out / f'round-{round_id}' / f'post-{cap}'
        flags = ['--quiet', '--stats', '--boolean-flatten-post-threshold=' + str(cap)]
        subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
                        '--root', case['root'], '--manifest', str(manifest), '--out', str(target),
                        '--timeout', '60', '--run-id', f'flatten-review-{index}-{round_id}-{cap}',
                        '--solver', config['binary'], '--expected-git', config['source_commit'],
                        *['--solver-arg=' + arg for arg in flags]], check=True)
        with (target / 'answers.csv').open() as stream:
            row, = csv.DictReader(stream)
        if row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'}:
            raise SystemExit('blocking solver outcome')
        if row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}:
            known.add(row['answer'])
        if row['status'] == 'ok' and row['answer'] == 'sat':
            replays.append(target)
reference = out / 'z3'
subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
                '--root', case['root'], '--manifest', str(manifest), '--out', str(reference),
                '--timeout', '60', '--run-id', f'flatten-review-{index}-z3',
                '--solver', config['z3']], check=True)
with (reference / 'answers.csv').open() as stream:
    row, = csv.DictReader(stream)
if row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'}:
    raise SystemExit('blocking reference outcome')
if row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}:
    known.add(row['answer'])
assert len(known) <= 1, 'reference disagreement'
for target in replays:
    subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'replay', '--out', str(target),
                    '--model-timeout', '90', '--dolmen-timeout', '30',
                    '--dolmen-arg=--strict=false'], check=True)
assert digest(source) == input_hash and digest(config['binary']) == config['binary_sha256']
(out / 'complete').write_text('all repeated timings complete; SAT models separately replayed\n')
PY
