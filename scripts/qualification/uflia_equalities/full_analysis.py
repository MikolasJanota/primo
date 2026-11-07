#!/usr/bin/env python3
"""Join full equality-comparison rows to fresh references and inspect arithmetic work."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'scripts/bench_qf_uf_full.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.qualification.uflia_equalities.summarize import (  # noqa: E402
    metrics,
    paired,
    solved,
)

COUNTERS = [':lia.equality.scans', ':lia.equality.proposed', ':lia.equality.reason-lits',
            ':combination.equality.fresh-pairs', ':combination.equality.batches',
            ':lia.equality.fixed-eq', ':lia.equality.eq-row', ':lia.equality.eq-offset-rows',
            ':lia.equality.eq-rows', ':combination.model.complete-fallbacks',
            ':combination.model.fallback-budget', ':combination.model.decisions',
            ':combination.model.materialized-pairs', ':combination.arrangement-pairs',
            ':combination.arrangement-bridge-clauses', ':combination.components.incremental-growths',
            ':no.mixed.lia-atoms', ':no.mixed.euf-atoms', ':lra.pivots', ':lra.elimination-pivots',
            ':lra.pivot-row-updates', ':lra.pivot-term-ops', ':sat.decisions', ':sat.conflicts',
            ':sat.propagations', ':sat.vars', ':time.lia.equality-scan-us',
            ':time.qf-uflia.component-growth-us', ':time.qf-uflia.lia-integer-us',
            ':time.qf-uflia.sat-us']


def work(row: dict) -> dict:
    values = {key: int(row['stats'][key]) if key in row['stats'] else None for key in COUNTERS}
    pivots = values[':lra.pivots']
    proposed = values[':lia.equality.proposed']
    return {'status': row['status'], 'answer': row['answer'], 'seconds': float(row['seconds']),
            'counters': values, 'statistics_present': bool(row['stats']),
            'row_updates_per_reported_pivot': values[':lra.pivot-row-updates'] / pivots if pivots and values[':lra.pivot-row-updates'] is not None else None,
            'merged_terms_per_reported_pivot': values[':lra.pivot-term-ops'] / pivots if pivots and values[':lra.pivot-term-ops'] is not None else None,
            'mean_reason_literals_per_proposal': values[':lia.equality.reason-lits'] / proposed if proposed and values[':lia.equality.reason-lits'] is not None else None,
            'limitation': 'Timeout counters cover a partial prefix; per-pivot ratios describe that run, not matched search states.'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timing', type=Path, required=True)
    parser.add_argument('--references', type=Path, required=True)
    parser.add_argument('--reference-prefix', default='tmp/QF_UFLIA/')
    parser.add_argument('--inputs', type=int, required=True)
    parser.add_argument('--shards', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--export-csv', type=Path)
    args = parser.parse_args()
    profiles = defaultdict(dict)
    inputs = {}
    timeout = None
    for shard in range(args.shards):
        path = args.timing / f'shard-{shard}'
        assert (path / 'complete').is_file(), path
        provenance = json.loads((path / 'provenance.json').read_text())
        assert timeout is None or timeout == provenance['timeout']
        timeout = provenance['timeout']
        assert not inputs.keys() & provenance['inputs'].keys()
        inputs.update(provenance['inputs'])
        for line in (path / 'rows.jsonl').read_text().splitlines():
            row = json.loads(line)
            assert row['file'] not in profiles[row['profile']]
            assert row['file'] in provenance['inputs']
            assert not row['wrong'] and row['status'] not in {'error', 'unsupported'}, row
            profiles[row['profile']][row['file']] = row
    assert len(inputs) == args.inputs and timeout is not None
    expected_profiles = {f'{role}-{mode}' for role in ['baseline', 'candidate', 'enabled']
                         for mode in ['native', 'm124']}
    assert set(profiles) == expected_profiles
    assert all(set(rows) == set(inputs) for rows in profiles.values())
    references = {}
    for path in sorted(args.references.glob('shard-*.csv')):
        if path.stem.endswith('-summary'):
            continue
        with path.open(newline='') as stream:
            for row in csv.DictReader(stream):
                assert row['file'].startswith(args.reference_prefix), row['file']
                name = row['file'][len(args.reference_prefix):]
                assert name not in references
                assert row['wrong'] == 'false' and row['status'] not in {'error', 'unsupported'}, row
                row['file'] = name
                row['exit_code'] = int(row['exit_code'])
                references[name] = row
    assert set(references) == set(inputs)
    checked = Counter()
    declared = Counter()
    for name in inputs:
        answers = {rows[name]['answer'] for rows in profiles.values() if solved(rows[name])}
        if solved(references[name]):
            answers.add(references[name]['answer'])
        assert len(answers) <= 1, ('answer disagreement', name)
        for profile, rows in profiles.items():
            if rows[name]['expected'] in {'sat', 'unsat'}:
                declared[profile] += int(solved(rows[name]))
            if solved(rows[name]) and solved(references[name]):
                checked[profile] += 1
    comparisons = {}
    default_differences = {}
    for mode in ['native', 'm124']:
        before, after = profiles['candidate-' + mode], profiles['enabled-' + mode]
        comparison = paired(before, after, timeout)
        names = comparison['losses'] + comparison['gains']
        common = [name for name in inputs if solved(before[name]) and solved(after[name])]
        common.sort(key=lambda n: float(after[n]['seconds']) / max(float(before[n]['seconds']), .000001), reverse=True)
        names += common[:20]
        comparison['inspected_cases'] = {name: {'off': work(before[name]), 'on': work(after[name])}
                                        for name in dict.fromkeys(names)}
        comparisons[mode] = comparison
        old = profiles['baseline-' + mode]
        differences = {}
        for name in inputs:
            if not (solved(old[name]) and solved(before[name])):
                continue
            keys = old[name]['stats'].keys() | before[name]['stats'].keys()
            changed = {key: [old[name]['stats'].get(key, '0'), before[name]['stats'].get(key, '0')]
                       for key in keys if not key.startswith(':time.') and
                       old[name]['stats'].get(key, '0') != before[name]['stats'].get(key, '0')}
            if changed:
                differences[name] = changed
        default_differences[mode] = differences
    result = {'inputs': args.inputs, 'timeout': timeout,
              'input_sha256': dict(sorted(inputs.items())),
              'input_identity_sha256': hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest(),
              'profiles': {name: {**metrics(list(rows.values()), timeout),
                                  'rows_with_statistics': sum(bool(r['stats']) for r in rows.values())}
                           for name, rows in profiles.items()},
              'reference': metrics(list(references.values()), timeout),
              'fresh_reference_checked_solved_rows': dict(checked),
              'declared_status_checked_solved_rows': dict(declared),
              'comparisons': comparisons, 'default_non_time_counter_differences_common_solved': default_differences}
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    if args.export_csv:
        for name, rows in {**profiles, 'z3': references}.items():
            path = args.export_csv / name / 'shard-0.csv'
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=['file', 'status', 'answer', 'seconds', 'wrong', 'exit_code'])
                writer.writeheader()
                for row in rows.values():
                    writer.writerow({key: ('false' if key == 'wrong' else row[key]) for key in writer.fieldnames})
    print(json.dumps({'profiles': result['profiles'], 'reference': result['reference'],
                      'native_gains_losses': [len(comparisons['native']['gains']), len(comparisons['native']['losses'])],
                      'm124_gains_losses': [len(comparisons['m124']['gains']), len(comparisons['m124']['losses'])]}, indent=2))


if __name__ == '__main__':
    main()
