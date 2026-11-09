#!/usr/bin/env python3
"""Summarize qualified paired inputs without rerunning any solver."""
from __future__ import annotations

import argparse
import csv
import gzip
import itertools
import math
import statistics
from pathlib import Path

from campaign import digest, load, write
from report import solved


def matched(left: dict, right: dict, cohort: set[str], timeout: float) -> dict:
    a = {name for name in cohort if solved(left[name], timeout)}
    b = {name for name in cohort if solved(right[name], timeout)}
    common = sorted(a & b)
    if any(left[name]['answer'] != right[name]['answer'] for name in common):
        raise ValueError('different concrete answers in matched inputs')
    positive = [name for name in common if float(left[name]['seconds']) > 0 and float(right[name]['seconds']) > 0]
    ratios = [float(left[name]['seconds']) / float(right[name]['seconds']) for name in positive]
    return {'inputs': len(cohort), 'common_solved': len(common),
            'left_only_solved': len(a - b), 'right_only_solved': len(b - a),
            'both_unsolved': len(cohort - (a | b)), 'positive_runtime_pairs': len(ratios),
            'geomean_left_over_right': math.exp(statistics.mean(math.log(x) for x in ratios)) if ratios else None,
            'median_left_over_right': statistics.median(ratios) if ratios else None,
            'median_left_seconds': statistics.median(float(left[n]['seconds']) for n in common) if common else None,
            'median_right_seconds': statistics.median(float(right[n]['seconds']) for n in common) if common else None}


def analyze(directory: Path) -> dict:
    proof = load(directory / 'provenance.json')
    summary = load(directory / 'summary.json')
    if (summary['status'] != 'complete' or not summary['ground_models_complete']
            or digest(directory / 'summary.json') != proof['qualified_audit_sha256']
            or digest(directory / 'timing.csv.gz') != proof['timing_table_sha256']):
        raise ValueError('qualified exported data identity mismatch')
    data = {}
    with gzip.open(directory / 'timing.csv.gz', 'rt') as stream:
        for row in csv.DictReader(stream):
            series = data.setdefault(row['logic'], {}).setdefault(row['profile'], {})
            if row['file'] in series or row['wrong'] != 'false':
                raise ValueError('duplicate input or wrong answer')
            series[row['file']] = row
    output = {}
    timeout = summary['timeout_seconds']
    for logic, series in data.items():
        names = set(next(iter(series.values())))
        hashes = {name: next(iter(series.values()))[name]['sha256'] for name in names}
        for label, rows in series.items():
            if {name: row['sha256'] for name, row in rows.items()} != hashes:
                raise ValueError('different paired input bytes')
            times = [float(r['seconds']) for r in rows.values() if solved(r, timeout)]
            expected = summary['runs'][logic][label]
            par2 = math.fsum(times) + 2 * timeout * (len(rows) - len(times))
            if (len(rows) != expected['inputs'] or len(times) != expected['solved']
                    or not math.isclose(par2, expected['par2_sum'], rel_tol=1e-12, abs_tol=1e-6)):
                raise ValueError('exported scores differ from qualification')
        easy = {name for name in names if all(solved(rows[name], timeout) and float(rows[name]['seconds']) < 1 for rows in series.values())}
        if (len(easy) != summary['filtered'][logic]['removed_common_easy']
                or len(names - easy) != summary['filtered'][logic]['retained']):
            raise ValueError('different filtered cohort')
        comparisons = {}
        for left, right in itertools.permutations(series, 2):
            if not left.startswith('primo'):
                continue
            comparisons[f'{left}/{right}'] = {view: matched(series[left], series[right], cohort, timeout)
                                             for view, cohort in [('full', names), ('nontrivial', names - easy)]}
        output[logic] = {'inputs': len(names), 'common_easy_removed': len(easy),
                         'cutoff_solved': {label: {str(limit): sum(solved(r, limit) for r in rows.values())
                                                   for limit in [1, 10, 60, 120]}
                                           for label, rows in series.items()},
                         'matched_inputs': comparisons}
    return {'status': 'qualified-descriptive-comparison',
            'interpretation': 'Runtime ratios are left/right on common successful inputs; below1means left faster. Different measurement dates: descriptive matched-input comparison, not a controlled A/B speedup.',
            'parent_provenance_sha256': digest(directory / 'provenance.json'),
            'analyzer_sha256': digest(Path(__file__)), 'logics': output}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    write(args.out, analyze(args.report))


if __name__ == '__main__':
    main()
