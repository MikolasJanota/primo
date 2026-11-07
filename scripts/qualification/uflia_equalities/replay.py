#!/usr/bin/env python3
"""Replay every successful comparison SAT row independently with Dolmen."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
from collections import Counter
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('parent', 'root', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists() or not (args.parent / 'complete').is_file():
        parser.error('fresh output and completed parent required')
    args.out.mkdir(parents=True)
    provenance = json.loads((args.parent / 'provenance.json').read_text())
    all_rows = [json.loads(line) for line in (args.parent / 'rows.jsonl').read_text().splitlines()]
    coverage = {'parent': str(args.parent), 'runner_sha256': digest(Path(__file__)),
                'expected': 0, 'passed': 0, 'groups': [], 'status': 'running'}
    selected = [r for r in all_rows if r['status'] == 'ok' and r['answer'] == 'sat' and r['exit_code'] == 0]
    coverage['expected'] = len(selected)
    target = args.out / 'coverage.json'
    target.write_text(json.dumps(coverage, indent=2) + '\n')
    for profile in provenance['profiles']:
        rows = [r for r in selected if r['profile'] == profile]
        if not rows:
            continue
        identity = provenance['binaries'][profile.split('-')[0]]
        solver = Path(identity['path'])
        if digest(solver) != identity['sha256']:
            raise SystemExit('binary changed after timing')
        flags = rows[0]['command'][1:-1]
        for row in rows:
            if row['command'][1:-1] != flags or digest(args.root / row['file']) != provenance['inputs'][row['file']]:
                raise SystemExit('profile/input mismatch')
        manifest = args.out / (profile + '.txt')
        manifest.write_text(''.join(str(args.root / row['file']) + '\n' for row in rows))
        out = args.out / profile
        command = [sys.executable, 'scripts/validate_model.py', '--solver', str(solver),
                   *['--solver-arg=' + flag for flag in flags], '--require-dolmen-pass', '--expect-sat',
                   '--timeout', '60', '--dolmen-timeout', '30', '--dolmen-arg=--strict=false',
                   '--manifest', str(manifest), '--out', str(out)]
        process = subprocess.run(command, check=False)
        with (out / 'summary.csv').open(newline='') as stream:
            results = list(csv.DictReader(stream))
        counts = Counter(row['outcome'] for row in results)
        passed = sum(row['outcome'] == 'pass' and row['validator'] == 'dolmen' and row['answer'] == 'sat' for row in results)
        coverage['passed'] += passed
        coverage['groups'].append({'profile': profile, 'expected': len(rows), 'passed': passed,
                                   'outcomes': dict(counts), 'command': command, 'exit_code': process.returncode})
        coverage['status'] = 'running' if passed == len(rows) and process.returncode == 0 else 'failed-or-incomplete'
        target.write_text(json.dumps(coverage, indent=2) + '\n')
        if coverage['status'] != 'running':
            raise SystemExit('model replay requires classification')
    coverage['status'] = 'complete' if coverage['expected'] == coverage['passed'] else 'incomplete'
    target.write_text(json.dumps(coverage, indent=2) + '\n')


if __name__ == '__main__':
    main()
