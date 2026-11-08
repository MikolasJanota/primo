#!/usr/bin/env bash
# Rotate configurations on identical manifest shards. Commands are supplied in
# JSON as argument lists; no shell parsing of solver commands is performed.
#SBATCH --job-name=primo-lia-cubes
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path

root = Path(os.environ['PRIMO_CORPUS_ROOT'])
out = Path(os.environ['PRIMO_COMPARE_OUT'])
config = json.loads(Path(os.environ['PRIMO_COMPARE_CONFIG']).read_text())
task = int(os.environ['SLURM_ARRAY_TASK_ID'])
shards = int(os.environ['PRIMO_SHARDS'])
selected = config['inputs'][task::shards]
shard = out / f'shard-{task}'
shard.mkdir(parents=True, exist_ok=True)
manifest = shard / 'inputs.txt'
manifest.write_text(''.join(entry['file'] + '\n' for entry in selected))
for entry in selected:
    assert hashlib.sha256((root / entry['file']).read_bytes()).hexdigest() == entry['sha256']
profiles = config['profiles']
order = profiles[task % len(profiles):] + profiles[:task % len(profiles)]
provenance = {'job': os.environ.get('SLURM_ARRAY_JOB_ID'), 'task': task,
              'node': os.uname().nodename, 'profiles': order, 'inputs': selected,
              'cpu_model': next(line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),
              'cpus_requested': os.environ.get('SLURM_CPUS_PER_TASK'),
              'memory_requested_mb': os.environ.get('SLURM_MEM_PER_NODE'),
              'config_sha256': hashlib.sha256(Path(os.environ['PRIMO_COMPARE_CONFIG']).read_bytes()).hexdigest()}
known_answers = {}
for profile in order:
    command = profile['command']
    identity = hashlib.sha256(Path(command[0]).read_bytes()).hexdigest()
    assert identity == profile['binary_sha256'], profile['name']
    subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
                    '--root', str(root), '--manifest', str(manifest),
                    '--out', str(out / profile['name'] / f'shard-{task}'),
                    '--solver', command[0], '--run-id', config['run_id'],
                    '--timeout', str(config['timeout']),
                    *['--solver-arg=' + arg for arg in command[1:]]], check=True)
    answers = out / profile['name'] / f'shard-{task}' / 'answers.csv'
    with answers.open() as stream:
        for row in csv.DictReader(stream):
            if (row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'}
                    or int(row.get(':lia.cube-rejected') or 0)):
                raise SystemExit('correctness failure: ' + profile['name'] + ' ' + row['file'])
            if row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}:
                if row['exit_code'] != '0':
                    raise SystemExit('post-answer process failure')
                previous = known_answers.setdefault(row['file'], row['answer'])
                if previous != row['answer']:
                    raise SystemExit('cross-profile/reference disagreement: ' + row['file'])
(shard / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
PY
