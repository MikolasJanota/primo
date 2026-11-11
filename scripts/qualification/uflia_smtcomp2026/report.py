#!/usr/bin/env python3
"""Audit the selection replay and draw qualified plots without executing solvers."""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import json
import math
import statistics
import sys
from collections import Counter
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'scripts/plot_cactus.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.plot_cactus import svg  # noqa: E402
from scripts.qualification.long_limits.campaign import (  # noqa: E402
    decisive,
    memory_exhausted,
)
from scripts.qualification.uniform120 import resource_triage as resources  # noqa: E402

LABELS = {'z3': 'Z3', 'cvc5': 'cvc5', 'yices': 'Yices', 'opensmt': 'OpenSMT'}
COLOURS = {'Primo': '#0057b8', 'Z3': '#d55e00', 'cvc5': '#009e73', 'Yices': '#a67800', 'OpenSMT': '#cc79a7'}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def solved(row: dict) -> bool:
    return decisive(row) and float(row['seconds']) <= 120


def metrics(rows: list[dict]) -> dict:
    ok = [r for r in rows if solved(r)]
    return {'inputs': len(rows), 'solved': len(ok), 'sat': sum(r['answer'] == 'sat' for r in ok),
            'unsat': sum(r['answer'] == 'unsat' for r in ok), 'statuses': dict(Counter(r['status'] for r in rows)),
            'unknown': sum(r['status'] == 'ok' and r['answer'] == 'unknown' for r in rows),
            'memory_out': sum(bool(r.get('memory_out')) or str(r.get('memory_exhausted')).lower() == 'true' for r in rows),
            'unexplained_errors': sum(r['status'] == 'error' and not r.get('memory_out') and str(r.get('memory_exhausted')).lower() != 'true' for r in rows),
            'par2_sum': sum(float(r['seconds']) if solved(r) else 240 for r in rows)}


def paired(before: list[dict], after: list[dict]) -> dict:
    a = {r['file']: r for r in before}
    b = {r['file']: r for r in after}
    assert a.keys() == b.keys()
    common = [{'file': f, 'before_seconds': float(a[f]['seconds']), 'after_seconds': float(b[f]['seconds'])} for f in a if solved(a[f]) and solved(b[f])]
    ratios = [p['after_seconds'] / p['before_seconds'] for p in common if max(p['before_seconds'], p['after_seconds']) >= 0.05]
    return {'gains': [f for f in a if solved(b[f]) and not solved(a[f])],
            'losses': [f for f in a if solved(a[f]) and not solved(b[f])], 'common': common,
            'common_at_least_50ms': len(ratios), 'median_ratio': statistics.median(ratios) if ratios else None,
            'geometric_ratio': math.exp(statistics.mean(math.log(r) for r in ratios)) if ratios else None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'manifest', 'references', 'out', 'gallery'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    with args.references.open(newline='') as stream:
        references = list(csv.DictReader(stream))
    assert len(references) == 1200
    original_answers = {}
    for case in manifest['cases']:
        rs = [r for r in references if r['file'] == case['file']]
        assert len(rs) == 4 and {r['profile'] for r in rs} == set(LABELS)
        assert all(r['sha256'] == case['sha256'] for r in rs)
        answers = {r['original_answer'] for r in rs if r['original_status'] == 'ok'
                   and r['original_exit_code'] == '0' and r['original_timeout'] == 'false'
                   and r['original_answer'] in ('sat', 'unsat')}
        assert len(answers) <= 1
        assert not answers or answers == {case['expected']}
        original_answers[case['file']] = answers
    rows = []
    hashes = {}
    selected_sat = 0
    model_pass = 0
    for slot, case in enumerate(manifest['cases']):
        directory = args.root / f'slot-{slot}'
        provenance = json.loads((directory / 'provenance.json').read_text())
        assert provenance['binary_sha256'] == manifest['native_sha256'] and provenance['case'] == case
        assert provenance['manifest_sha256'] == sha(args.manifest)
        batch = [json.loads(line) for line in (directory / 'rows.jsonl').read_text().splitlines()]
        assert len(batch) == 5 and {r['profile'] for r in batch} == set(manifest['profiles'])
        assert len({r['answer'] for r in batch if decisive(r)}) <= 1
        classifications = {r['profile']: r for r in json.loads((directory / 'resources.json').read_text())}
        coverage = json.loads((directory / 'coverage.json').read_text())
        assert coverage['status'] == 'complete' and coverage['selected_sat'] == coverage['direct_dolmen_pass']
        selected = {r['profile'] for r in batch if decisive(r) and r['answer'] == 'sat'}
        validated = {r['profile'] for r in coverage['rows'] if r['outcome'] == 'pass' and r['validator'] == 'dolmen'}
        assert selected == validated
        selected_sat += len(selected)
        for item in coverage['rows']:
            summary = directory / 'models' / item['profile'] / 'summary.csv'
            assert sha(summary) == item['summary_sha256']
            with summary.open(newline='') as stream:
                model = next(csv.DictReader(stream))
            assert model['solver_exit'] == model['dolmen_exit'] == '0' and model['outcome'] == 'pass'
            model_pass += 1
        for row in batch:
            assert row['file'] == case['file'] and not row['wrong'] and row['argv'] == manifest['profile_args'][row['profile']]
            if resources.candidate(row):
                task = {'logic': 'QF_UFLIA', 'label': row['profile'], 'arguments': row['argv']}
                plan = {'tools': {'primo': {'sha256': manifest['native_sha256']}}, 'config': {'memory_mib': 8192}}
                inventory = {'inputs': {case['file']: {'sha256': case['sha256']}}}
                checked = resources.annotate([row], directory, task, plan, inventory)[0]
                assert checked.get('_confirmed_oom')
            else:
                checked = {**row, 'stdout_log': str(directory / f"{row['profile']}.stdout.txt"),
                           'stderr_log': str(directory / f"{row['profile']}.stderr.txt")}
                assert classifications[row['profile']]['memory_out'] == memory_exhausted(checked)
            assert row['status'] in ('ok', 'timeout') or classifications[row['profile']]['memory_out']
            if decisive(row) and case['expected'] in ('sat', 'unsat'):
                assert row['answer'] == case['expected']
            assert not decisive(row) or not original_answers[case['file']] or row['answer'] in original_answers[case['file']]
            portable = {k: v for k, v in row.items() if k not in ('stdout_log', 'stderr_log')}
            rows.append(dict(portable, sha256=case['sha256'], family=case['family'], memory_out=classifications[row['profile']]['memory_out']))
        for name in ('provenance.json', 'rows.jsonl', 'resources.json', 'coverage.json'):
            hashes[str((directory / name).relative_to(args.root))] = sha(directory / name)
    expected_hashes = {c['file']: c['sha256'] for c in manifest['cases']}
    assert len(references) == 1200
    for row in references:
        assert row['sha256'] == expected_hashes[row['file']]
    profiles = {p: [r for r in rows if r['profile'] == p] for p in manifest['profiles']}
    profiles.update({p: [r for r in references if r['profile'] == p] for p in LABELS})
    for rs in profiles.values():
        assert len(rs) == 300 and {r['file'] for r in rs} == set(expected_hashes)
    summary = {p: metrics(rs) for p, rs in profiles.items()}
    best = min(manifest['profiles'], key=lambda p: (-summary[p]['solved'], summary[p]['par2_sum'], p))
    comparisons = {f'{after}/{before}': paired(profiles[before], profiles[after]) for before, after in
                   [('off', 'model-all'), ('legacy-all', 'model-all'), ('legacy-fixed', 'model-fixed'), ('cvc5', best)]}
    displayed = {'Primo': profiles[best], **{LABELS[p]: profiles[p] for p in LABELS}}
    common_easy = set.intersection(*[{r['file'] for r in rs if solved(r) and float(r['seconds']) < 1} for rs in displayed.values()])
    families = {f: {p: metrics([r for r in rs if r['family'] == f]) for p, rs in profiles.items()} for f in sorted({c['family'] for c in manifest['cases']})}
    result = {'status': 'complete and every successful native SAT row direct-Dolmen validated', 'selection_sha256': sha(args.manifest),
              'native_sha256': manifest['native_sha256'], 'native_timing_rows': len(rows), 'selected_sat': selected_sat,
              'selected_sat_direct_dolmen': model_pass, 'profiles': summary, 'families': families,
              'comparisons': comparisons, 'selected_primo_profile': best,
              'selection_rule': 'One fixed profile for all300inputs, most solved then lowestPAR2 thenname; no per-input portfolio',
              'common_easy_removed': len(common_easy), 'reference_executions': 0, 'artifact_sha256': hashes}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    encoded = ''.join(json.dumps(r, sort_keys=True) + '\n' for r in rows).encode()
    (args.out / 'timings.jsonl.gz').write_bytes(gzip.compress(encoded, mtime=0))
    args.gallery.mkdir(parents=True, exist_ok=True)
    for kind, excluded in [('full', set()), ('nontrivial', common_easy)]:
        series = [(label, sorted(float(r['seconds']) for r in rs if r['file'] not in excluded and solved(r))) for label, rs in displayed.items()]
        caption = ['SMT-COMP2026 QF_UFLIA selection; original SMT-LIB inputs, without competition scrambling.',
                   f'Primo: one fixed measured profile ({best}); frozen44eee3c1;120s,8GiB,onephysicalCPU.',
                   'Four byte-matched archived reference series reused; zero reference benchmark executions.',
                   f'{len(excluded)} common easy (<1s for every displayed solver) inputs removed; separate SAT-model validation complete.']
        (args.gallery / f'qf-uflia-{kind}.svg').write_text(svg(series, 120, 300-len(excluded),
            'QF_UFLIA — SMT-COMP2026 selection' + (' (common easy inputs removed)' if excluded else ''), caption,
            highlighted='Primo', colour_by_name=COLOURS, percentage=True))
    print(json.dumps({'profiles': summary, 'selected_primo_profile': best, 'native_SAT_direct_Dolmen': model_pass}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
