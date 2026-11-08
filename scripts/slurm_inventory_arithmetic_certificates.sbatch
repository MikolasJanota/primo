#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-cert-inventory
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen selection helpers}"
: "${ARITHMETIC_HISTORY_ROOT:?recorded native runs}"
: "${LRA_CORPUS:?existing LRA corpus}"
: "${LIA_CORPUS:?existing LIA corpus}"
cd "$PRIMO_WORKDIR"
export INVENTORY_OUT="$PWD/results/arithmetic-certificate-inventory-$SLURM_JOB_ID"
python3 - <<'PY'
import collections
import json
import os
import platform
import sys
from pathlib import Path
sys.path.insert(0, os.environ['ARITHMETIC_WORKFLOW_DIR'])
from select_micromath_fast_cases import read_history
out = Path(os.environ['INVENTORY_OUT'])
out.mkdir(parents=True, exist_ok=False)
report = {'job_id': os.environ['SLURM_JOB_ID'], 'host': platform.node(), 'logics': {}}
for logic in ('QF_LRA', 'QF_LIA'):
    corpus = Path(os.environ['LRA_CORPUS' if logic == 'QF_LRA' else 'LIA_CORPUS'])
    history = Path(os.environ['ARITHMETIC_HISTORY_ROOT']) / logic / 'primo'
    records = read_history(history, '5efbd02293e15a53fbaad748ceda78bc7d27bbac', None)
    if any(r['historical_solver_args'] != ['--quiet'] for r in records):
        raise ValueError('mixed historical solver profiles')
    sizes = []
    missing = []
    for row in records:
        path = corpus / row['file']
        if path.is_file():
            sizes.append((path.stat().st_size, row['file']))
        else:
            missing.append(row['file'])
    report['logics'][logic] = {
        'recorded_unsat': len(records), 'existing': len(sizes), 'missing': missing,
        'unique_historical_hashes': len({r['sha256'] for r in records}),
        'families': dict(collections.Counter(r['family'] for r in records)),
        'historical_max_seconds': max(r['historical_seconds'] for r in records),
        'historical_over_one_second': sum(r['historical_seconds'] > 1 for r in records),
        'source_bytes_total': sum(s for s, _ in sizes),
        'largest_sources': sorted(sizes, reverse=True)[:5],
    }
    (out / f'{logic}-records.json').write_text(json.dumps(records, indent=2) + '\n')
(out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
PY
