#!/usr/bin/env python3
"""Rank current QF_UFLIA gaps using qualified paired timing rows, without solver runs."""
from __future__ import annotations

import argparse
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def solved(row: dict) -> bool:
    return (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
            and row['exit_code'] == '0' and row['timeout'] == 'false' and float(row['seconds']) <= 120)


def rank(report: Path) -> dict:
    proof = json.loads((report / 'provenance.json').read_text())
    summary = json.loads((report / 'summary.json').read_text())
    if (digest(report / 'summary.json') != proof['qualified_audit_sha256']
            or digest(report / 'timing.csv.gz') != proof['timing_table_sha256']
            or summary['status'] != 'complete' or not summary['ground_models_complete']):
        raise ValueError('qualified input identity mismatch')
    data = collections.defaultdict(dict)
    with gzip.open(report / 'timing.csv.gz', 'rt') as stream:
        for row in csv.DictReader(stream):
            if row['logic'] != 'QF_UFLIA':
                continue
            if row['profile'] in data[row['file']] or row['wrong'] != 'false':
                raise ValueError('duplicate or wrong row')
            data[row['file']][row['profile']] = row
    families = collections.defaultdict(collections.Counter)
    gaps = []
    for name, profiles in sorted(data.items()):
        if len(profiles) != len(summary['runs']['QF_UFLIA']) or len({r['sha256'] for r in profiles.values()}) != 1:
            raise ValueError('unmatched profile inputs')
        concrete = {r['answer'] for r in profiles.values() if solved(r)}
        if len(concrete) > 1:
            raise ValueError('reference disagreement')
        a, b = profiles['primo'], profiles['cvc5']
        ca, cb = solved(a), solved(b)
        family = families[a['family']]
        family.update(inputs=1, primo_solved=int(ca), cvc5_solved=int(cb),
                      cvc5_only=int(cb and not ca), primo_only=int(ca and not cb),
                      primo_par2=float(a['seconds']) if ca else 240,
                      cvc5_par2=float(b['seconds']) if cb else 240)
        if cb and (not ca or float(a['seconds']) >= 10 * float(b['seconds'])):
            gaps.append({'file': name, 'sha256': a['sha256'], 'family': a['family'],
                         'answer': b['answer'], 'primo_status': a['status'],
                         'primo_seconds': float(a['seconds']), 'cvc5_seconds': float(b['seconds']),
                         'integer_status': profiles['primo-integer']['status'],
                         'integer_seconds': float(profiles['primo-integer']['seconds']),
                         'primo_solved': ca,
                         'runtime_ratio': float(a['seconds']) / float(b['seconds']) if ca else None,
                         'runtime_ratio_lower_bound': 120 / float(b['seconds']) if not ca else None,
                         'par2_gap': (float(a['seconds']) if ca else 240) - float(b['seconds'])})
    if len(data) != summary['runs']['QF_UFLIA']['primo']['inputs']:
        raise ValueError('incomplete corpus')
    return {'logic': 'QF_UFLIA', 'inputs': len(data), 'timeout_seconds': 120,
            'source_provenance_sha256': digest(report / 'provenance.json'),
            'source_timing_sha256': proof['timing_table_sha256'], 'ranker_sha256': digest(Path(__file__)),
            'criterion': 'cvc5 successful and native unsolved or >=10x slower; ordered by PAR2 gap',
            'limitation': 'Archived references measured on a different date; no controlled A/B speedup claim.',
            'families': dict(families),
            'gaps': sorted(gaps, key=lambda r: (-r['par2_gap'], r['file']))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.write_text(json.dumps(rank(args.report), indent=2) + '\n')


if __name__ == '__main__':
    main()
