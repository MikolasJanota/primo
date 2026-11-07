#!/usr/bin/env python3
"""Join selected option diagnostics to their direct SAT-model replay artifacts."""
from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'scripts/bench_qf_uf_full.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.qualification.uflia_equalities.full_analysis import work  # noqa: E402
from scripts.qualification.uflia_equalities.summarize import solved  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = {'rows': 0, 'cases': [], 'outcomes': {},
              'models': {'expected': 0, 'passed': 0, 'raw_csv_rows': 0},
              'scope': 'Selected cases and existing-option interactions, not a full sweep or isolated mechanism ablation.'}
    counts = Counter()
    identity = None
    for index in range(4):
        parent = args.root / 'diagnostics' / f'case-{index}'
        assert (parent / 'complete').is_file()
        provenance = json.loads((parent / 'provenance.json').read_text())
        assert provenance['timeout'] == 120
        if identity is not None:
            assert identity == provenance['binary_sha256']
        identity = provenance['binary_sha256']
        rows = [json.loads(line) for line in (parent / 'rows.jsonl').read_text().splitlines()]
        variants = {name for name, _ in provenance['variants']}
        assert len(variants) == 7 and len(rows) == 14
        assert {(r['variant'], r['role']) for r in rows} == {(v, role) for v in variants for role in ['off', 'on']}
        assert all(not r['wrong'] and r['status'] not in {'error', 'unsupported'} for r in rows)
        assert {r['answer'] for r in rows if solved(r)} == {provenance['case']['expected_answer']}
        selected = {r['variant'] + '-' + r['role'] for r in rows if solved(r) and r['answer'] == 'sat'}
        model_root = args.root / 'diagnostic-models' / f'case-{index}'
        coverage = json.loads((model_root / 'coverage.json').read_text())
        assert coverage['status'] == 'complete'
        assert coverage['expected'] == coverage['passed'] == len(selected)
        assert len(coverage['rows']) == len(selected)
        assert {r['label'] for r in coverage['rows']} == selected
        raw_count = 0
        for group in coverage['rows']:
            assert group['passed'] and group['exit_code'] == 0
            with (model_root / group['label'] / 'summary.csv').open(newline='') as stream:
                models = list(csv.DictReader(stream))
            assert len(models) == 1
            assert models[0]['outcome'] == 'pass' and models[0]['validator'] == 'dolmen' and models[0]['answer'] == 'sat'
            raw_count += 1
        result['models']['expected'] += len(selected)
        result['models']['passed'] += coverage['passed']
        result['models']['raw_csv_rows'] += raw_count
        case = {'index': index, 'selection': provenance['case'],
                'input_sha256': provenance['input_sha256'], 'binary_sha256': identity,
                'timeout': provenance['timeout'], 'models': {'expected': len(selected), 'passed': raw_count},
                'rows': [{**work(r), 'variant': r['variant'], 'role': r['role'],
                          'exit_code': r['exit_code'], 'solver_args': r['command'][1:-1]}
                         for r in rows]}
        result['cases'].append(case)
        result['rows'] += len(rows)
        counts.update(('solved:' + r['answer']) if solved(r) else r['status'] + ':' + r['answer'] for r in rows)
    result['outcomes'] = dict(counts)
    result['models']['status'] = 'complete'
    assert result['rows'] == 56
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ['rows', 'outcomes', 'models']}, indent=2))


if __name__ == '__main__':
    main()
