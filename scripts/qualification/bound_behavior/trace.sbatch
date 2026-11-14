#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-root-trace
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PRIMO_TRACE_FMT1=1
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
from scripts.bench_qf_uf_full import run_solver

source = Path(os.environ['PRIMO_BEHAVIOR_INPUT'])
assert hashlib.sha256(source.read_bytes()).hexdigest() == os.environ['PRIMO_BEHAVIOR_INPUT_SHA']
native = Path('build-slurm-rel/primo').resolve()
out = Path('results/root-bound-trace')
out.mkdir(parents=True, exist_ok=False)
records = []
for label in ('rows', 'both'):
    flags = ['--quiet', '--stats', '--lra-propagation', '--lra-row-propagation',
             '--lra-bidirectional-row-propagation', '--lia-branch-limit=1',
             '--max-theory-conflicts=1']
    if label == 'both':
        flags.append('--preprocess-bound-tightening')
    row = run_solver([str(native), *flags], source, 30)
    for kind in ('stdout', 'stderr'):
        (out / f'{label}.{kind}').write_text(row.pop(kind))
    assert row['status'] == 'ok' and row['exit_code'] == 0
    assert row['answer'] in ('sat', 'unknown')
    row.update(profile=label, args=flags)
    records.append(row)
(out / 'trace.json').write_text(json.dumps({
    'job': os.environ['SLURM_JOB_ID'], 'source_snapshot': '6936bfbe',
    'native_sha256': hashlib.sha256(native.read_bytes()).hexdigest(),
    'input_sha256': os.environ['PRIMO_BEHAVIOR_INPUT_SHA'],
    'scope': 'Read-only root-bound trace; logical work caps; no timing or SAT-model claim',
    'rows': records,
}, indent=2) + '\n')
PY
