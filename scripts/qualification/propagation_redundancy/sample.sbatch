#!/usr/bin/env bash
#SBATCH --job-name=primo-propagation-redundancy-samples
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys

config = json.loads(Path(os.environ['PRIMO_AUDIT_PROFILE_CONFIG']).read_text())
case = config['cases'][0]
source = Path(config['roots'][case['logic']]) / case['file']
out = Path(os.environ['PRIMO_AUDIT_PROFILE_OUT'])
out.mkdir(parents=True, exist_ok=False)
records = []
for repetition in range(2):
    labels = ['control-rows', 'audit-rows']
    if repetition:
        labels.reverse()
    for label in labels:
        profile = config['profiles'][label]
        assert hashlib.sha256(Path(profile['solver']).read_bytes()).hexdigest() == profile['sha256']
        target = out / f'rep-{repetition}-{label}'
        command = [sys.executable, 'scripts/profile_gdb_samples.py', '--out', str(target), '--timeout', '180', '--interval', '0.1', '--seed', str(2026112500 + repetition), '--', profile['solver'], *profile['args'], *case.get('extra_args', []), str(source)]
        subprocess.run(command, check=True)
        summary = json.loads((target / 'summary.json').read_text())
        assert summary['status'] == 'complete'
        assert 'unknown' in (target / 'gdb-mi.log').read_text()
        records.append({'profile': label, 'repetition': repetition, 'command': command, 'summary': summary})
(out / 'metadata.json').write_text(json.dumps({'job': os.environ['SLURM_JOB_ID'], 'host': socket.gethostname(), 'case': case, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'sampler_sha256': hashlib.sha256(Path('scripts/profile_gdb_samples.py').read_bytes()).hexdigest(), 'records': records, 'limits': '1000 theory conflicts,180s sampler deadline,0.1s jittered stack observations;10GiB SLURM memory; wall-stack samples, not CPU events or benchmark timing.'}, indent=2) + '\n')
PY
