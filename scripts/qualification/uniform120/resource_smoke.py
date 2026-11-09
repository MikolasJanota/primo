#!/usr/bin/env python3
"""Check that a real masked memory-out is followed by a successful timing row."""
from __future__ import annotations

import csv
import os
import sys
from pathlib import Path

from campaign import allocation, guarded, legacy, load, resources, strict_rows, write
from report import metrics


def main() -> None:
    workspace = Path(os.environ['PRIMO_WORKDIR'])
    out = Path(os.environ['PRIMO_LONG_OUT'])
    plan = load(out / 'plan.json')
    selected = plan['tasks'][1015]
    if (selected['logic'], selected['label'], selected['shard']) != ('QF_LIA', 'primo', 217):
        raise ValueError('unexpected smoke source task')
    source = out / 'timing/QF_LIA/primo/shard-217/answers.csv.jsonl'
    old = legacy.journal(source)
    failed = next(r for r in old if resources.candidate(r))
    good = next(r for r in old if legacy.decisive(r) and float(r['seconds']) < 1)
    corpus = load(out / 'inventory/QF_LIA.json')
    root = out / 'resource-smoke' / os.environ['SLURM_JOB_ID']
    root.mkdir(parents=True)
    write(root / 'execution.json', allocation(plan['config']))
    manifest = root / 'inputs.txt'
    manifest.write_text(f"{failed['file']}\n{good['file']}\n")
    command = [sys.executable, str(workspace / 'scripts/benchmark_model_replay.py'), 'benchmark',
               '--root', corpus['root'], '--manifest', str(manifest),
               '--solver', plan['tools']['primo']['path'], '--solver-arg=--quiet',
               '--expected-git', plan['config']['native_source_git'], '--run-id', 'resource-smoke',
               '--shards', '1', '--shard-index', '0', '--timeout', '120', '--out', str(root)]
    guarded(command, root, selected, plan, root, 0, corpus)
    with (root / 'answers.csv').open() as stream:
        raw = list(csv.DictReader(stream))
    rows = resources.annotate(raw, root, selected, plan, corpus)
    strict_rows(rows, [], False)
    result = metrics(rows, 120)
    if ([r['file'] for r in rows] != [failed['file'], good['file']]
            or rows[0]['exit_code'] != '-6' or rows[0]['status'] != 'error'
            or not legacy.decisive(rows[1])
            or (result['solved'], result['memory_out'], result['confirmed_cleanup_memory_out']) != (1, 1, 1)):
        raise ValueError('resource-failure continuation smoke failed')
    write(root / 'summary.json', {'status': 'pass', 'metrics': result, 'raw_rows': raw})


if __name__ == '__main__':
    main()
