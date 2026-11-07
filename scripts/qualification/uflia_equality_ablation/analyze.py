#!/usr/bin/env python3
"""Audit ablation completeness, counter controls, answer agreement and SAT replay."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / 'scripts/bench_qf_uf_full.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.qualification.uflia_equalities.full_analysis import work  # noqa: E402
from scripts.qualification.uflia_equalities.summarize import (  # noqa: E402
    metrics,
    paired,
    solved,
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('timing', 'out', 'reference_analysis', 'references'):
        parser.add_argument('--' + name.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--models', type=Path)
    parser.add_argument('--inputs', type=int, required=True)
    parser.add_argument('--shards', type=int, required=True)
    args = parser.parse_args()
    old = json.loads(args.reference_analysis.read_text())
    profiles = defaultdict(dict)
    inputs = {}
    binaries = None
    timeout = None
    profile_names = None
    model_counts = Counter()
    for shard in range(args.shards):
        path = args.timing / f'shard-{shard}'
        assert (path / 'complete').is_file(), path
        provenance = json.loads((path / 'provenance.json').read_text())
        assert binaries is None or binaries == provenance['binaries'], 'binary changed across shards'
        binaries = provenance['binaries']
        assert timeout is None or timeout == provenance['timeout']
        timeout = provenance['timeout']
        assert profile_names is None or profile_names == provenance['profiles']
        profile_names = provenance['profiles']
        for name, sha in provenance['inputs'].items():
            assert name not in inputs and old['input_sha256'][name] == sha
            inputs[name] = sha
        shard_rows = [json.loads(line) for line in (path / 'rows.jsonl').read_text().splitlines()]
        for row in shard_rows:
            assert row['profile'] in profile_names and row['file'] in provenance['inputs']
            assert not row['wrong'] and row['status'] not in {'error', 'unsupported'}, row
            assert row['file'] not in profiles[row['profile']]
            profiles[row['profile']][row['file']] = row
        if args.models:
            coverage = json.loads((args.models / f'shard-{shard}' / 'coverage.json').read_text())
            expected = sum(solved(r) and r['answer'] == 'sat' for r in shard_rows)
            assert coverage['status'] == 'complete' and coverage['expected'] == coverage['passed'] == expected
            model_counts['expected'] += expected
            model_counts['passed'] += coverage['passed']
    assert len(inputs) == args.inputs and set(profiles) == set(profile_names)
    assert all(set(rows) == set(inputs) for rows in profiles.values())
    references = {}
    for path in sorted(args.references.glob('shard-*.csv')):
        if path.stem.endswith('-summary'):
            continue
        for row in csv.DictReader(path.open(newline='')):
            prefix = 'tmp/QF_UFLIA/'
            assert row['file'].startswith(prefix)
            name = row['file'][len(prefix):]
            assert name not in references
            row['exit_code'] = int(row['exit_code'])
            assert row['wrong'] == 'false' and row['status'] not in {'error', 'unsupported'}
            references[name] = row
    checked = Counter()
    reference_unavailable = defaultdict(list)
    for name in inputs:
        answers = {rows[name]['answer'] for rows in profiles.values() if solved(rows[name])}
        if solved(references[name]):
            answers.add(references[name]['answer'])
        assert len(answers) <= 1, ('answer disagreement', name)
        for profile, rows in profiles.items():
            if solved(rows[name]):
                if solved(references[name]):
                    checked[profile] += 1
                else:
                    reference_unavailable[profile].append(name)
    controls = {}
    for before, after in [('old-off', 'off'), ('off', 'scan'), ('old-all', 'all')]:
        if before not in profiles or after not in profiles:
            continue
        differences = {}
        common = [n for n in inputs if solved(profiles[before][n]) and solved(profiles[after][n])]
        for name in common:
            a, b = profiles[before][name]['stats'], profiles[after][name]['stats']
            assert a and b, ('missing completed-run statistics', before, after, name)
            changed = {k: [a.get(k, '0'), b.get(k, '0')] for k in a.keys() | b.keys()
                       if not k.startswith((':time.', ':lia.equality.')) and a.get(k, '0') != b.get(k, '0')}
            if changed:
                differences[name] = changed
        controls[before + '/' + after] = {'common_solved': len(common), 'non_time_search_counter_differences': differences}
    if 'names' in profiles:
        for name, row in profiles['names'].items():
            assert int(row['stats'].get(':combination.equality.clauses', 0)) == 0, name
    output = {'inputs': len(inputs), 'timeout': timeout, 'binaries': binaries,
              'input_identity_sha256': hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest(),
              'references': {'analysis_sha256': hashlib.sha256(args.reference_analysis.read_bytes()).hexdigest(),
                             'policy': 'Reuse the prior fresh Z3 run only for SHA-identical inputs; unchanged reference binary recorded in parent evidence.',
                             'checked_solved_rows': dict(checked), 'unavailable_solved_rows': dict(reference_unavailable)},
              'profiles': {p: metrics(list(rows.values()), timeout) for p, rows in profiles.items()},
              'vs_off': {p: paired(profiles['off'], rows, timeout) for p, rows in profiles.items() if p != 'off'},
              'controls': controls,
              'models': {'status': 'complete' if args.models else 'pending', **dict(model_counts)},
              'cases': {name: {p: work(rows[name]) for p, rows in profiles.items()} for name in sorted(inputs)}}
    args.out.write_text(json.dumps(output, indent=2) + '\n')
    print(json.dumps({k: output[k] for k in ('inputs', 'profiles', 'controls', 'models')}, indent=2))


if __name__ == '__main__':
    main()
