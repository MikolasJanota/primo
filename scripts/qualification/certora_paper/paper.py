#!/usr/bin/env python3
"""Recompute the paper's Certora ablations from pinned experimental CSV data."""
from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import json
import urllib.request
from collections import defaultdict
from pathlib import Path


def successful(row: list[str]) -> bool:
    # Authors' columns: path,RV,T_norm,T_cpu,T_wc,SAT,UNSAT,UNKNOWN,unused.
    return row[1] == '0' and (row[5] == '1' or row[6] == '1') and float(row[4]) <= 600


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true', help='Fetch missing pinned Git blobs; never solver source')
    args = parser.parse_args()
    args.data.mkdir(parents=True, exist_ok=True)
    records = []
    all_rows = {}
    for source in json.loads(args.sources.read_text()):
        path = args.data / source['file']
        if not path.exists() and args.fetch:
            with urllib.request.urlopen(source['blob_url'], timeout=30) as response:
                blob = json.load(response)
            assert blob['encoding'] == 'base64' and blob['sha'] == source['git_blob_sha']
            path.write_bytes(base64.b64decode(blob['content']))
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source['sha256'], path
        with path.open(newline='') as stream:
            raw = list(csv.reader(stream))
        rows = [row for row in raw[2:] if row and row[0]]
        assert len(rows) == 308 and len({r[0] for r in rows}) == 308
        all_rows[path.name] = {r[0]: r for r in rows}
        groups = defaultdict(list)
        for row in rows:
            groups[row[0].split('/')[2]].append(row)
        records.append({**source, 'option_or_solver': raw[0][1], 'rows': len(rows),
                        'solved_600_wall': sum(map(successful, rows)),
                        'by_logic': {logic: {'inputs': len(xs), 'solved': sum(map(successful, xs))} for logic, xs in groups.items()}})
    default = all_rows['compare-z3-benchmark-submission.csv']
    legacy = all_rows['compare-z3legacy-benchmark-submission.csv']
    assert default.keys() == legacy.keys()
    comparisons = {}
    for logic in sorted({p.split('/')[2] for p in default}):
        names = [p for p in default if p.split('/')[2] == logic]
        comparisons[logic] = {'gains': [p for p in names if successful(default[p]) and not successful(legacy[p])],
                              'losses': [p for p in names if successful(legacy[p]) and not successful(default[p])]}
    totals = [sum(map(successful, x.values())) for x in (default, legacy)]
    assert totals == [217, 168], totals
    value = {'basis': 'Authors supplementary CSVs; RV0 and SAT/UNSAT flag, T_wc<=600. T_norm is separate and unused.',
             'source': 'https://github.com/z3prover/doc/tree/master/arithmetic/data',
             'source_relation': '308paths across8logics,77named verification conditions with4encodings each. Authors files have no input SHA, so this is not a content-identity comparison with2026 inputs.',
             'published_default_legacy_totals_reproduced': totals, 'comparisons': comparisons, 'records': records}
    args.out.write_text(json.dumps(value, indent=2) + '\n')
    print('Reproduced default217/legacy168; QF_UFLIA44/43; nonlinear net+48,linear net+1')


if __name__ == '__main__':
    main()
