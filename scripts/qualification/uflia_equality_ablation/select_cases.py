#!/usr/bin/env python3
"""Select every native solved gain/loss plus deterministic slow and fast controls."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--analysis', type=Path, required=True)
    parser.add_argument('--timing', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.analysis.read_text())
    comparison = data['comparisons']['native']
    reasons = {n: 'native 60s solved gain' for n in comparison['gains']}
    reasons.update({n: 'native 60s solved loss' for n in comparison['losses']})
    rows = {}
    for path in sorted(args.timing.glob('shard-*/rows.jsonl')):
        for line in path.read_text().splitlines():
            row = json.loads(line)
            if row['profile'] in ('candidate-native', 'enabled-native'):
                assert row['profile'] not in rows.setdefault(row['file'], {})
                rows[row['file']][row['profile']] = row
    common = [n for n, group in rows.items() if all(
        p in group and group[p]['status'] == 'ok' and group[p]['exit_code'] == 0
        and group[p]['answer'] in ('sat', 'unsat') for p in ('candidate-native', 'enabled-native'))]
    slow = sorted(common, key=lambda n: (
        -float(rows[n]['enabled-native']['seconds']) / max(float(rows[n]['candidate-native']['seconds']), .000001), n))[:10]
    for name in slow:
        reasons.setdefault(name, 'top 10 common-solved enabled/off runtime ratio')
    for family in sorted({n.split('/')[0] for n in rows}):
        controls = sorted((n for n in common if n.split('/')[0] == family),
                          key=lambda n: (float(rows[n]['candidate-native']['seconds']), n))
        for name in controls[:2]:
            reasons.setdefault(name, 'two fastest off common-solved family controls')
    for name in ['wisas/xs_37_57.smt2', 'mathsat/EufLaArithmetic/medium/medium13.smt2',
                 'wisas/xs_25_35.smt2',
                 '20230314-Jaroslav-Bendik-Certora/39657_2866defdd1f2434b69ab_47_QF_UFLIA.smt2']:
        reasons.setdefault(name, 'previous four-case profiling control')
    args.out.write_text('# Native gains/losses, 10 slow common cases, two fast controls/family, four profiled cases.\n'
                        + '\n'.join(sorted(reasons)) + '\n')
    metadata = {'source_commit': '8d84fdcab67f7ba0aaf097378062e71c6971be8c',
                'analysis_sha256': hashlib.sha256(args.analysis.read_bytes()).hexdigest(),
                'inputs': len(reasons), 'selection': {
                    n: {'reason': reasons[n], 'sha256': data['input_sha256'][n]} for n in sorted(reasons)}}
    args.out.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('selected', len(reasons))


if __name__ == '__main__':
    main()
