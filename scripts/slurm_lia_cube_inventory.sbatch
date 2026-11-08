#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-cube-inventory
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:30:00
set -euo pipefail
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path

root = Path(os.environ['PRIMO_CORPUS_ROOT'])
inventory = Path(os.environ['PRIMO_INVENTORY'])
expected = dict(line.split('\t') for line in inventory.read_text().splitlines())
actual = {str(path.relative_to(root)): path for path in root.rglob('*.smt2') if path.is_file()}
assert actual.keys() == expected.keys(), 'corpus path set changed'
size = 0
for name, path in sorted(actual.items()):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(block)
            digest.update(block)
    assert digest.hexdigest() == expected[name], name
Path(os.environ['PRIMO_AUDIT_OUT']).write_text(json.dumps({
    'files': len(actual), 'bytes': size,
    'inventory_sha256': hashlib.sha256(inventory.read_bytes()).hexdigest(),
    'job': os.environ.get('SLURM_JOB_ID'), 'node': os.uname().nodename,
    'all_content_hashes_match': True
}, indent=2) + '\n')
PY
