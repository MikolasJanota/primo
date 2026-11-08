#!/usr/bin/env python3
"""Audit the paired cube development timings and their separate model replays."""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import itertools
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def solved(row: dict[str, str], timeout: float) -> bool:
    return (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
            and row['exit_code'] == '0' and row['timeout'] == 'false'
            and float(row['seconds']) <= timeout)


def cost(rows: list[dict[str, str]], timeout: float) -> dict[str, object]:
    successful = [row for row in rows if solved(row, timeout)]
    return {'total': len(rows), 'solved': len(successful),
            'answers': dict(Counter(row['answer'] for row in successful)),
            'statuses': dict(Counter(row['status'] for row in rows)),
            'clean_unknown': sum(row['status'] == 'ok' and row['answer'] == 'unknown'
                                 and row['exit_code'] == '0' for row in rows),
            'over_limit_success': sum(row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
                                      and float(row['seconds']) > timeout for row in rows),
            'par2': sum(float(row['seconds']) if solved(row, timeout) else 2 * timeout
                        for row in rows)}


def paired(left: dict[str, dict[str, str]], right: dict[str, dict[str, str]],
           timeout: float) -> dict[str, object]:
    common = [name for name in left if solved(left[name], timeout) and solved(right[name], timeout)]
    measurable = [name for name in common if min(float(left[name]['seconds']),
                                                float(right[name]['seconds'])) >= .05]
    ratios = [float(right[name]['seconds']) / float(left[name]['seconds']) for name in measurable]
    return {'baseline': 'left', 'candidate': 'right', 'common': len(common),
            'common_both_at_least_50ms': len(measurable),
            'geomean_candidate_over_baseline': math.exp(sum(map(math.log, ratios)) / len(ratios))
            if ratios else None,
            'median_candidate_over_baseline': statistics.median(ratios) if ratios else None,
            'gains': sorted(name for name in left if not solved(left[name], timeout)
                            and solved(right[name], timeout)),
            'losses': sorted(name for name in left if solved(left[name], timeout)
                             and not solved(right[name], timeout))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--experiment', type=Path,
                        default=Path('benchmarks/lia-cubes-20261108/experiment.json'))
    parser.add_argument('--selection', type=Path,
                        default=Path('benchmarks/lia-cubes-20261108/selection.json'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--require-models', action='store_true')
    parser.add_argument('--gzip-rows', action='store_true')
    args = parser.parse_args()
    experiment = json.loads(args.experiment.read_text())
    if args.selection.suffix == '.tsv':
        entries = {name: {'file': name, 'sha256': digest, 'bucket': 'full-corpus'}
                   for name, digest in (line.split('\t') for line in args.selection.read_text().splitlines())}
    else:
        entries = {entry['file']: entry for entry in json.loads(args.selection.read_text())['inputs']}
    timeout = float(experiment['timeout'])
    profiles = {}
    provenance = {}
    model_coverage = {}
    for profile in experiment['profiles']:
        label = profile['name']
        rows = {}
        summaries = []
        source = args.root / label
        shards = sorted(source.glob('shard-*'))
        if not shards:
            raise ValueError('missing timing profile ' + label)
        for shard in shards:
            with (shard / 'answers.csv').open() as stream:
                timing = list(csv.DictReader(stream))
            identity = json.loads((shard / 'provenance.json').read_text())
            if (identity['status'] != 'complete' or not identity['inputs_unchanged']
                    or identity['solver_args'] != profile['arguments']
                    or float(identity['timeout']) != timeout):
                raise ValueError('incomplete or mismatched timing provenance: ' + str(shard))
            expected_revision = experiment['source_commit'] if profile['binary'] == 'candidate' else experiment['baseline_commit']
            if profile['binary'] != 'z3' and not any(
                    line.startswith('git: ') and expected_revision.startswith(line[5:])
                    for line in identity['solver']['version'].splitlines()):
                raise ValueError('unexpected solver revision: ' + label)
            for name, digest in identity['inputs'].items():
                if name not in entries or digest != entries[name]['sha256']:
                    raise ValueError('input hash mismatch: ' + name)
            if identity['solver']['sha256'] != experiment['binaries'][profile['binary']]['sha256']:
                raise ValueError('unexpected binary hash: ' + label)
            summaries.append(identity)
            for row in timing:
                name = row['file']
                if name in rows or name not in entries:
                    raise ValueError('duplicate/unexpected file: ' + name)
                if (row['status'] not in {'ok', 'timeout'} or
                        (row['status'] == 'ok' and row['exit_code'] != '0')):
                    raise ValueError('solver error: ' + label + ' ' + name)
                if row.get('wrong') == 'true' or int(row.get(':lia.cube-rejected') or 0):
                    raise ValueError('wrong answer or rejected candidate: ' + label + ' ' + name)
                rows[name] = row
            if profile['binary'] != 'z3':
                coverage = shard / 'model-validation/coverage.json'
                key = label + '/' + shard.name
                model_coverage[key] = json.loads(coverage.read_text()) if coverage.is_file() else {
                    'status': 'pending', 'expected_sat': sum(solved(row, timeout)
                                                           and row['answer'] == 'sat' for row in timing)}
        if rows.keys() != entries.keys():
            raise ValueError('incomplete file set: ' + label)
        profiles[label] = rows
        provenance[label] = summaries
    if any(row['status'] == 'failed' for row in model_coverage.values()):
        raise ValueError('failed model replay')
    if args.require_models and any(row['status'] != 'complete' for row in model_coverage.values()):
        raise ValueError('incomplete model replay')
    for key, cover in model_coverage.items():
        if cover['status'] == 'pending':
            continue
        label, shard = key.split('/')
        parent = args.root / label / shard
        with (parent / 'answers.csv').open() as stream:
            expected_sat = sum(row['status'] == 'ok' and row['answer'] == 'sat'
                               and row['exit_code'] == '0' and row['timeout'] == 'false'
                               for row in csv.DictReader(stream))
        if cover['expected_sat'] != expected_sat or (cover['status'] == 'complete'
                                                    and cover['dolmen_pass'] != expected_sat):
            raise ValueError('incorrect SAT replay coverage: ' + key)
        proof = cover['provenance']
        if (proof['parent_answers_sha256'] != hashlib.sha256((parent / 'answers.csv').read_bytes()).hexdigest()
                or proof['parent_provenance_sha256'] != hashlib.sha256((parent / 'provenance.json').read_bytes()).hexdigest()):
            raise ValueError('model replay linked to different timing rows: ' + key)
    for left, right in itertools.combinations(profiles, 2):
        for name, row in profiles[left].items():
            other = profiles[right][name]
            if solved(row, timeout) and solved(other, timeout) and row['answer'] != other['answer']:
                raise ValueError('reference disagreement: ' + name)
    result = {'timeout': timeout, 'files': len(entries), 'profiles': {}, 'comparisons': {},
              'model_replays': {'statuses': dict(Counter(row['status'] for row in model_coverage.values())),
                                'expected_sat': sum(row.get('expected_sat', 0) for row in model_coverage.values()),
                                'dolmen_pass': sum(row.get('dolmen_pass', 0) for row in model_coverage.values())},
              'correctness': 'no wrong flags, cross-profile/reference disagreement, or rejected candidate'}
    counter_keys = sorted({key for rows in profiles.values() for row in rows.values()
                           for key in row if key.startswith((':lia.cube-', ':lia.branch-nodes'))})
    for label, rows in profiles.items():
        families = defaultdict(list)
        buckets = defaultdict(list)
        for name, row in rows.items():
            families[name.split('/')[0]].append(row)
            buckets[entries[name]['bucket']].append(row)
        result['profiles'][label] = {**cost(list(rows.values()), timeout),
            'families': {name: cost(group, timeout) for name, group in sorted(families.items())},
            'buckets': {name: cost(group, timeout) for name, group in sorted(buckets.items())},
            'counters_lower_bounds': {key: sum(int(row.get(key) or 0) for row in rows.values())
                                      for key in counter_keys},
            'inputs_with_cube_models': sum(int(row.get(':lia.cube-models') or 0) > 0
                                          for row in rows.values()),
            'note': 'counters are lower bounds: unfinished completions/timeouts can omit work'}
    for shape in ('default', 'm124'):
        baseline = 'off-' + shape
        if baseline not in profiles:
            continue
        for mode in ('uniform', 'difference'):
            label = mode + '-' + shape
            if label not in profiles:
                continue
            result['comparisons'][baseline + '_vs_' + label] = paired(profiles[baseline], profiles[label], timeout)
        if 'baseline-' + shape in profiles:
            result['comparisons']['baseline-' + shape + '_vs_' + baseline] = paired(
                profiles['baseline-' + shape], profiles[baseline], timeout)
    preservation = {}
    for shape in ('default', 'm124'):
        if 'baseline-' + shape not in profiles or 'off-' + shape not in profiles:
            continue
        old, new = profiles['baseline-' + shape], profiles['off-' + shape]
        checked = 0
        differences = {}
        for name, row in old.items():
            other = new[name]
            if row['status'] != 'ok' or other['status'] != 'ok' or row['answer'] != other['answer']:
                continue
            checked += 1
            keys = {key for key in row.keys() | other.keys() if key.startswith(':')
                    and not key.startswith((':time.', ':lia.cube-', ':lia.branch-nodes'))}
            changed = {key: [row.get(key) or '0', other.get(key) or '0'] for key in sorted(keys)
                       if (row.get(key) or '0') != (other.get(key) or '0')}
            if changed:
                differences[name] = changed
        preservation[shape] = {'completed_matching_answers': checked, 'counter_differences': differences}
    result['off_preservation'] = preservation
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    # A public CSV deliberately excludes private command paths and output logs.
    fields = ['profile', 'file', 'input_sha256', 'family', 'bucket', 'status', 'answer',
              'expected', 'wrong', 'seconds', 'exit_code', 'timeout', *counter_keys]
    row_path = args.out / ('rows.csv.gz' if args.gzip_rows else 'rows.csv')
    raw_stream = gzip.GzipFile(filename=str(row_path), mode='wb', mtime=0) if args.gzip_rows else row_path.open('wb')
    with io.TextIOWrapper(raw_stream, encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for label, rows in profiles.items():
            for name, row in sorted(rows.items()):
                public = {key: row.get(key, '') for key in fields}
                public.update(profile=label, input_sha256=entries[name]['sha256'],
                              bucket=entries[name]['bucket'])
                writer.writerow(public)
    # Raw provenance remains local: it includes environment-specific paths.
    (args.root / 'audited-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps({label: {key: value for key, value in summary.items()
                              if key in {'solved', 'answers', 'par2', 'inputs_with_cube_models'}}
                      for label, summary in result['profiles'].items()}, indent=2))


if __name__ == '__main__':
    main()
