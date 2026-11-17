#!/usr/bin/env bash
#SBATCH --job-name=primo-conflict-ack-review
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export DEBUGINFOD_URLS=''
python3 - <<'PY'
import hashlib
import json
import os
import socket
import subprocess
import sys
from pathlib import Path

config_path = Path(os.environ['PRIMO_ACK_REVIEW_CONFIG'])
config = json.loads(config_path.read_text())
slot = int(os.environ['SLURM_ARRAY_TASK_ID'])
case = config['cases'][slot]
source = Path(config['roots'][case['logic']]) / case['file']
assert hashlib.sha256(source.read_bytes()).hexdigest() == case['sha256']
out = Path(config['out']) / f'case-{slot}'
out.mkdir(parents=True, exist_ok=False)
records = []
for repetition in range(2):
    labels = ['OFF', 'ON'] if repetition == 0 else ['ON', 'OFF']
    for label in labels:
        profile = config['profiles'][label]
        assert hashlib.sha256(Path(profile['solver']).read_bytes()).hexdigest() == profile['sha256']
        target = out / f'rep-{repetition}-{label}'
        command = [sys.executable, 'scripts/profile_gdb_samples.py', '--out', str(target),
                   '--timeout', '90', '--interval', '0.05', '--seed', str(2026111710 + slot * 2 + repetition),
                   '--', profile['solver'], *profile['args'], str(source)]
        subprocess.run(command, check=True)
        summary = json.loads((target / 'summary.json').read_text())
        assert summary['status'] == 'complete'
        log = (target / 'gdb-mi.log').read_text()
        assert 'unsat' in log.splitlines() or '@"unsat\\n"' in log
        records.append({'profile': label, 'repetition': repetition, 'summary': summary})
(out / 'metadata.json').write_text(json.dumps({
    'case': case, 'source': config['git'], 'native_sha256': config['sha256'],
    'config_sha256': hashlib.sha256(config_path.read_bytes()).hexdigest(),
    'sampler_sha256': hashlib.sha256(Path('scripts/profile_gdb_samples.py').read_bytes()).hexdigest(),
    'workload_sha256': hashlib.sha256(Path(os.environ['PRIMO_ACK_REVIEW_JOB']).read_bytes()).hexdigest(),
    'job': os.environ['SLURM_JOB_ID'], 'host': socket.gethostname(), 'records': records,
    'scope': 'Full-query GDB wall-stack samples;90s profiler deadline/0.05s jitter;10GiB allocation,no native address-space cap under GDB;not CPU events or benchmark timings.'
}, indent=2) + '\n')
PY
