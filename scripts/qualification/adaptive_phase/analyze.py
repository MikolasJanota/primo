#!/usr/bin/env python3
"""Summarize a complete or partial phase diagnostic without changing raw timing rows."""
from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def solved(row: dict, limit: float) -> bool:
    return (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
            and row['exit_code'] == 0 and float(row['seconds']) <= limit)


def describe(rows: list[dict], limit: float) -> dict:
    runtimes = sorted(float(r['seconds']) for r in rows if solved(r, limit))
    probes: Counter = Counter()
    summaries = 0
    for row in rows:
        if row['probe']:
            summaries += 1
        for counts in row['probe']:
            probes.update(counts)
    result = {'rows': len(rows), 'solved': len(runtimes),
              'statuses': dict(Counter(r['status'] for r in rows)),
              'answers': dict(Counter(r['answer'] for r in rows if r['status'] == 'ok')),
              'wrong': sum(bool(r['wrong']) for r in rows),
              'par2_sum_seconds': sum(float(r['seconds']) if solved(r, limit) else 2*limit for r in rows),
              'rows_with_probe_summary': summaries, 'probe_totals': dict(probes)}
    result['solved_runtime_seconds'] = ({'min': runtimes[0], 'median': statistics.median(runtimes),
                                          'p90': runtimes[min(len(runtimes)-1, int(.9*len(runtimes)))],
                                          'max': runtimes[-1], 'sum': sum(runtimes)} if runtimes else None)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    selection = json.loads(args.selection.read_text())
    losses = set(selection['losses'])
    development = set(selection['development_losses'])
    groups: dict[str, list[dict]] = defaultdict(list)
    sources = {}
    for path in sorted(args.root.glob('task-*/rows.jsonl')):
        sources[str(path.relative_to(args.root))] = hashlib.sha256(path.read_bytes()).hexdigest()
        for line in path.read_text().splitlines():
            row = json.loads(line)
            groups[row['profile']].append(row)
    result: dict = {'timeout': args.timeout, 'source_rows_sha256': sources, 'profiles': {}}
    for profile, rows in sorted(groups.items()):
        keys = [(r['file'], r['round']) for r in rows]
        if len(set(keys)) != len(keys):
            raise ValueError(f'duplicate row {profile}')
        by_family: dict[str, list[dict]] = defaultdict(list)
        for row in rows:
            by_family[row['file'].split('/')[0]].append(row)
        record = describe(rows, args.timeout)
        record.update(losses=describe([r for r in rows if r['file'] in losses], args.timeout),
                      controls=describe([r for r in rows if r['file'] not in losses], args.timeout),
                      held_out_losses=describe([r for r in rows if r['file'] in losses-development], args.timeout),
                      families={f: describe(rs,args.timeout) for f,rs in sorted(by_family.items())},
                      solved_files=[r['file'] for r in rows if solved(r,args.timeout)])
        result['profiles'][profile] = record
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    for name, record in result['profiles'].items():
        print(name, 'solved', record['solved'], '/', record['rows'],
              'losses',record['losses']['solved'],'/',record['losses']['rows'],
              'controls',record['controls']['solved'],'/',record['controls']['rows'],
              'activations',record['probe_totals'].get('activations',0),
              'overridden',record['probe_totals'].get('overridden',0))


if __name__ == '__main__':
    main()
