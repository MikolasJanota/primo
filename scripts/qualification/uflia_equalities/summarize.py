#!/usr/bin/env python3
"""Check comparison completeness and summarize paired outcomes and mechanism use."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import Counter, defaultdict
from pathlib import Path


def solved(row: dict) -> bool:
    return row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'} and row['exit_code'] == 0


def metrics(rows: list[dict], timeout: float) -> dict:
    counts = Counter('solved' if solved(row) else row['status'] + ':' + row['answer'] for row in rows)
    return {'inputs': len(rows), 'outcomes': dict(counts),
            'solved': counts['solved'], 'sat': sum(solved(r) and r['answer'] == 'sat' for r in rows),
            'unsat': sum(solved(r) and r['answer'] == 'unsat' for r in rows),
            'par2_seconds': sum(float(r['seconds']) if solved(r) else 2 * timeout for r in rows)}


def paired(before: dict, after: dict, timeout: float) -> dict:
    assert before.keys() == after.keys(), 'unpaired scope'
    common = [name for name in before if solved(before[name]) and solved(after[name])]
    assert all(before[name]['answer'] == after[name]['answer'] for name in common), 'answer disagreement'
    stable = [name for name in common if max(float(before[name]['seconds']), float(after[name]['seconds'])) >= .05]
    ratios = [float(after[name]['seconds']) / float(before[name]['seconds']) for name in stable]
    gains = sorted(name for name in before if not solved(before[name]) and solved(after[name]))
    losses = sorted(name for name in before if solved(before[name]) and not solved(after[name]))
    families = {}
    for family in sorted({Path(name).parts[0] for name in before}):
        names = [name for name in before if Path(name).parts[0] == family]
        families[family] = {'baseline': metrics([before[n] for n in names], timeout),
                            'candidate': metrics([after[n] for n in names], timeout)}
    counters = [':lia.equality.scans', ':lia.equality.candidates', ':lia.equality.proposed',
                ':lia.equality.already-true', ':lia.equality.reason-lits', ':lia.equality.fixed-eq',
                ':lia.equality.eq-row', ':lia.equality.eq-offset-rows', ':lia.equality.eq-rows',
                ':combination.equality.batches', ':combination.equality.fresh-pairs',
                ':combination.equality.clauses', ':combination.equality.conflicts', ':time.lia.equality-scan-us']
    totals = {key: sum(int(row['stats'].get(key, 0)) for row in after.values()) for key in counters}
    active = sorted(name for name, row in after.items() if int(row['stats'].get(':lia.equality.proposed', 0)) > 0)
    return {'baseline': metrics(list(before.values()), timeout), 'candidate': metrics(list(after.values()), timeout),
            'gains': gains, 'losses': losses, 'common_solved': len(common),
            'geomean_candidate_over_baseline_50ms': math.exp(sum(map(math.log, ratios)) / len(ratios)) if ratios else None,
            'geomean_inputs': len(stable), 'families': families, 'mechanism_totals': totals,
            'mechanism_active_files': active}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timing', type=Path, required=True)
    parser.add_argument('--models', type=Path)
    parser.add_argument('--inputs', type=int, required=True)
    parser.add_argument('--candidate-git', required=True)
    parser.add_argument('--baseline-git', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    profiles: dict[str, dict] = defaultdict(dict)
    identities = {}
    timeout = None
    sources = {}
    model_expected = model_passed = 0
    for shard in range(12):
        path = args.timing / f'shard-{shard}'
        assert (path / 'complete').is_file(), path
        provenance = json.loads((path / 'provenance.json').read_text())
        if timeout is not None:
            assert timeout == provenance['timeout']
        timeout = provenance['timeout']
        for kind in ('baseline', 'candidate'):
            identity = provenance['binaries'][kind]
            assert f'git: {getattr(args, kind + "_git")}' in identity['version'].splitlines()
            if kind in identities:
                assert identities[kind] == identity['sha256']
            identities[kind] = identity['sha256']
        for name, checksum in provenance['inputs'].items():
            assert name not in sources, 'duplicate shard input'
            sources[name] = checksum
        for line in (path / 'rows.jsonl').read_text().splitlines():
            row = json.loads(line)
            assert row['file'] in provenance['inputs']
            assert not row['wrong'] and row['status'] not in {'error', 'unsupported'}, row
            arm = profiles[row['profile']]
            assert row['file'] not in arm
            arm[row['file']] = row
        if args.models:
            coverage = json.loads((args.models / f'shard-{shard}' / 'coverage.json').read_text())
            assert coverage['status'] == 'complete' and coverage['expected'] == coverage['passed']
            expected = sum(r['profile'] in provenance['profiles'] and solved(r) and r['answer'] == 'sat'
                           for arm in profiles.values() for r in arm.values() if r['file'] in provenance['inputs'])
            assert expected == coverage['expected']
            model_expected += expected
            model_passed += coverage['passed']
    assert len(sources) == args.inputs
    expected_profiles = {'baseline-native', 'candidate-native', 'baseline-m124', 'candidate-m124'}
    enabled = any(name.startswith('enabled-') for name in profiles)
    if enabled:
        expected_profiles |= {'enabled-native', 'enabled-m124'}
    assert set(profiles) == expected_profiles
    assert all(set(arm) == set(sources) for arm in profiles.values())
    assert timeout is not None
    result = {'inputs': len(sources), 'timeout': timeout, 'shards': 12,
              'baseline_git': args.baseline_git, 'candidate_git': args.candidate_git,
              'binary_sha256': identities,
              'input_identity_sha256': hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest(),
              'profiles': {mode: paired(profiles['baseline-' + mode], profiles['candidate-' + mode], timeout)
                           for mode in ('native', 'm124')},
              'models': {'status': 'complete' if args.models else 'pending',
                         'expected': model_expected, 'passed': model_passed}}
    if enabled:
        result['enabled_profiles'] = {
            mode: paired(profiles['candidate-' + mode], profiles['enabled-' + mode], timeout)
            for mode in ('native', 'm124')}
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({mode: {key: data[key] for key in ('baseline', 'candidate', 'gains', 'losses')}
                      for mode, data in result['profiles'].items()}, indent=2))


if __name__ == '__main__':
    main()
