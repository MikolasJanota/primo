#!/usr/bin/env bash
#SBATCH --job-name=primo-propagation-redundancy-profile
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:12:00
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

config = json.loads(Path(os.environ['PRIMO_AUDIT_PROFILE_CONFIG']).read_text())
case = config['cases'][0]
source = Path(config['roots'][case['logic']]) / case['file']
out = Path(os.environ['PRIMO_AUDIT_PROFILE_OUT'])
out.mkdir(parents=True, exist_ok=False)
records = []
for label in ('control-rows', 'audit-rows'):
    profile = config['profiles'][label]
    assert hashlib.sha256(Path(profile['solver']).read_bytes()).hexdigest() == profile['sha256']
    command = ['valgrind', '--tool=callgrind', '--callgrind-out-file=' + str(out / (label + '.callgrind')), profile['solver'], *profile['args'], '--max-theory-conflicts', '100', str(source)]
    with (out / (label + '.stdout')).open('w') as stdout, (out / (label + '.stderr')).open('w') as stderr:
        result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=300, check=True)
    assert (out / (label + '.stdout')).read_text().splitlines()[0] == 'unknown'
    with (out / (label + '.annotation')).open('w') as stdout:
        subprocess.run(['callgrind_annotate', '--inclusive=yes', '--threshold=95', str(out / (label + '.callgrind'))], stdout=stdout, check=True)
    records.append({'profile': label, 'command': command, 'exit_code': result.returncode, 'source_sha256': case['sha256'], 'binary_sha256': profile['sha256']})
(out / 'metadata.json').write_text(json.dumps({'job': os.environ['SLURM_JOB_ID'], 'host': socket.gethostname(), 'records': records, 'limit': '100 theory conflicts,300s per process;10GiB SLURM allocation; no native address-space cap under Valgrind; instruction counts, not native timing.'}, indent=2) + '\n')
PY
