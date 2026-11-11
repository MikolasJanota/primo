#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-static
#SBATCH --cpus-per-task=8
#SBATCH --mem=24G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - <<'PY'
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

out = Path(os.environ['PRIMO_HYBRID_STATIC_OUT'])
out.mkdir(parents=True, exist_ok=False)
# The preparation helper sends a Git archive. A local index over only these
# source directories supplies the tracked-file inventory for the canonical
# analysis database builder; it creates no remote commit or publication.
subprocess.run(['git', 'init', '--quiet'], check=True)
subprocess.run(['git', 'add', 'src', 'tests', 'docs/examples'], check=True)
subprocess.run([sys.executable, 'scripts/prepare_static_analysis.py', '--build',
                'build-slurm-rel', '--out', str(out / 'database')], check=True)
manifest = json.loads((out / 'database/manifest.json').read_text())
sources = sorted(Path(p) for p in manifest['source_sha256'] if p.endswith('.cpp'))
headers = Path(os.environ['PRIMO_CXX_HEADER_MANIFEST']).read_text().splitlines()
extra = [a for root in headers for a in ('--extra-arg=-isystem', '--extra-arg='+root)]
def scan(item):
    index, source = item
    row = {'file': str(source), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
    for label, checks in [('tidy', []), ('includes', ['-checks=-*,clang-diagnostic-*,misc-include-cleaner'])]:
        command = ['clang-tidy', '-p', str(out / 'database'), *checks, *extra, str(source)]
        with (out / f'{index:03d}-{label}.log').open('w') as stream:
            row[label] = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=900).returncode
    return row
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ['SLURM_CPUS_PER_TASK'])) as pool:
    rows = list(pool.map(scan, enumerate(sources)))
(out / 'summary.json').write_text(json.dumps(rows, indent=2)+'\n')
assert all(row['tidy'] == 0 for row in rows), 'inspect static findings'
PY
