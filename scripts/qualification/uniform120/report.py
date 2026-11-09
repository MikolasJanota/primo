#!/usr/bin/env python3
"""Audit native measurements and reused references, then draw uniform cactus panels."""
from __future__ import annotations

import argparse
import csv
import html
import importlib.util
import itertools
import os
import sys
from collections import Counter
from pathlib import Path

from campaign import (
    allocation,
    check_rows,
    decisive,
    digest,
    load,
    memory_exhausted,
    recovery,
    resources,
    strict_rows,
    verify_resource_workflow,
    write,
)

# Direct source-tree execution and frozen workflow bundles both expose the helper.
PROJECT = Path(os.environ.get('PRIMO_WORKDIR', str(Path(__file__).resolve().parents[3])))
sys.path.append(str(PROJECT / 'scripts'))
from plot_cactus import svg  # noqa: E402

DISPLAY = {'primo': 'Primo default', 'primo-integer': 'Primo integer profile',
           'z3': 'Z3', 'cvc5': 'cvc5', 'yices': 'Yices', 'opensmt': 'OpenSMT'}
COLOURS = {'Primo default': '#0057b8', 'Primo integer profile': '#6b4c9a',
           'Z3': '#d55e00', 'cvc5': '#009e73', 'Yices': '#a67800', 'OpenSMT': '#cc79a7'}


def rows_at(path: Path) -> list[dict]:
    with path.open() as stream:
        return list(csv.DictReader(stream))


def solved(row: dict, timeout: float) -> bool:
    return decisive(row) and float(row['seconds']) <= timeout


def metrics(rows: list[dict], timeout: float) -> dict:
    success = [r for r in rows if solved(r, timeout)]
    return {'inputs': len(rows), 'solved': len(success),
            'sat': sum(r['answer'] == 'sat' for r in success),
            'unsat': sum(r['answer'] == 'unsat' for r in success),
            'timeouts': sum(r['status'] == 'timeout' or (decisive(r) and float(r['seconds']) > timeout) for r in rows),
            'unknown': sum(r['status'] == 'ok' and r['answer'] == 'unknown' for r in rows),
            'memory_out': sum(memory_exhausted(r) for r in rows),
            'confirmed_cleanup_memory_out': sum(bool(r.get('_confirmed_oom')) for r in rows),
            'raw_error_exits': sum(r['status'] == 'error' for r in rows),
            'unsupported': sum(r['status'] == 'unsupported' for r in rows),
            'errors': sum(r['status'] not in {'ok', 'timeout', 'unsupported'} and not memory_exhausted(r) for r in rows),
            'par2_sum': sum(float(r['seconds']) for r in success) + 2 * timeout * (len(rows) - len(success))}


def legacy_report(workspace: Path):
    path = workspace / 'scripts/qualification/long_limits/report.py'
    spec = importlib.util.spec_from_file_location('legacy_timing_audit', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def gate(args) -> None:
    # The generator/model joins are checked on the execution host, where relative
    # input names can be resolved against the exact workspace.
    root = args.out / 'gates/fuzz'
    result = {}
    for profile in ['lia-default', 'lia-m124', 'uflia-default', 'uflia-m124', 'lra-default', 'uflra-default', 'uf-default']:
        base = root / profile
        if profile.startswith('lia-'):
            primaries = sorted(base.glob('fuzzsmt/shard-*/summary.csv'))
            models = sorted(base.glob('model-validation/shard-*/summary.csv'))
        elif profile.startswith('uflia-'):
            primaries = sorted(base.glob('task-*/answer/summary.csv'))
            models = sorted(base.glob('task-*/model-validation/summary.csv'))
        elif profile == 'lra-default':
            primaries = sorted(base.glob('task-*/summary.csv'))
            models = []
        elif profile == 'uf-default':
            primaries = sorted(base.glob('task-*/summary.csv'))
            models = sorted(base.glob('task-*/model-validation/summary.csv'))
        else:
            primaries = sorted(base.glob('shard-*/summary.csv'))
            models = sorted(base.glob('shard-*/models/summary.csv'))
        if len(primaries) != 4:
            raise ValueError('incomplete gate: ' + profile)
        answers = [r for p in primaries for r in rows_at(p)]
        model_rows = [r for p in models for r in rows_at(p)]
        outcomes = Counter(r['outcome'] for r in answers)
        if len(answers) != 1000 or set(outcomes) - {'PASS', 'TIMEOUT', 'UNKNOWN_INVOLVED', 'REFERENCE_UNUSABLE'}:
            raise ValueError('blocking gate answer outcome')
        passed = {str(Path(r['file']).resolve()) for r in model_rows
                  if r['outcome'] == 'pass' and r['validator'] == 'dolmen' and r['solver_exit'] == r['dolmen_exit'] == '0'}
        if any(r['outcome'] not in {'pass', 'not-sat', 'solver-timeout', 'solver-unknown'} for r in model_rows):
            raise ValueError('blocking gate model outcome')
        primary_sat = {str(Path(r.get('case') or r['file']).resolve()) for r in answers
                       if r['outcome'] == 'PASS' and (r.get('ours') or r.get('answer')) == 'sat'}
        passed |= {str(Path(r['case']).resolve()) for r in answers if r['outcome'] == 'PASS' and r.get('dolmen') == 'PASS'}
        if primary_sat - passed:
            raise ValueError('uncovered primary SAT in gate: ' + profile)
        result[profile] = {'cases': len(answers), 'outcomes': dict(outcomes), 'primary_SAT_direct_Dolmen': len(primary_sat)}
    write(args.out / 'gate-audit.json', {'status': 'complete', 'profiles': result,
                                        'native_binary_sha256': digest(args.workspace / 'build-slurm-rel/primo'),
                                        'native_revision': '7dd8874c'})


def reference_answers(rows: list[dict]) -> list[dict]:
    return [{**r, **{key: r['original_' + key] for key in
                     ['status', 'answer', 'seconds', 'exit_code', 'timeout']}} for r in rows]


def collect(args, final: bool) -> dict:
    plan = load(args.out / 'plan.json')
    resumed = recovery(args.out, plan)
    verify_resource_workflow((resumed or plan).get('resource_workflow_sha256'))
    gate_proof = load(args.out / 'gate-audit.json')
    if gate_proof['status'] != 'complete' or gate_proof['native_binary_sha256'] != plan['tools']['primo']['sha256']:
        raise ValueError('fresh correctness gate incomplete')
    if digest(args.out / 'reference-reuse.json') != plan['reference_reuse_sha256']:
        raise ValueError('reference certificate changed')
    certificates = load(args.out / 'reference-reuse.json')
    auditor = legacy_report(args.workspace)
    sys.path.insert(0, str(args.workspace / 'scripts/qualification/lra_ablation'))
    from summarize import audit_models
    reports, data = {}, {}
    for run in plan['runs']:
        logic, label = run['logic'], run['label']
        corpus = load(args.out / 'inventory' / f'{logic}.json')
        expected = {name: value['sha256'] for name, value in corpus['inputs'].items()}
        merged, models = {}, 0
        for shard in range(run['shards']):
            root = args.out / 'timing' / logic / label / f'shard-{shard}'
            if run['solver'] == 'primo':
                root, parent, rows = auditor.timing_shard(args.out, plan, run, shard, expected)
                rows = resources.annotate(rows, root, run, plan, corpus)
                strict_rows(rows, [], False)
                if final:
                    execution = load(root / 'execution-models.json')
                    if (execution['plan_sha256'] != digest(args.out / 'plan.json')
                            or execution['parent_timing_audit_sha256'] != digest(args.out / 'timing-audit.json')
                            or execution['memory_mib'] != plan['config']['memory_mib']):
                        raise ValueError('model barrier/resource identity mismatch')
                    models += audit_models(root, parent, rows)
            else:
                certificate = certificates[f'{logic}/{label}']
                if digest(root / 'answers.csv') != certificate['derived_csv_sha256'][str(shard)]:
                    raise ValueError('reference view changed')
                rows = rows_at(root / 'answers.csv')
            names = [r['file'] for r in rows]
            wanted = list(expected)[shard::run['shards']]
            if len(names) != len(set(names)) or set(names) != set(wanted) or set(merged) & set(names):
                raise ValueError('incomplete or duplicate plot cohort')
            merged.update((r['file'], r) for r in rows)
        if set(merged) != set(expected):
            raise ValueError('incomplete full corpus')
        for row in merged.values():
            if row['wrong'] != 'false':
                raise ValueError('wrong answer in comparison')
        result = metrics(list(merged.values()), plan['config']['timeout_seconds'])
        result.update(arguments=run['arguments'], historical_reference=run['solver'] != 'primo',
                      direct_Dolmen_models=models if final and run['solver'] == 'primo' else None)
        result['families'] = {family: metrics([r for r in merged.values() if r['family'] == family], plan['config']['timeout_seconds'])
                              for family in sorted({r['family'] for r in merged.values()})}
        reports.setdefault(logic, {})[label] = result
        data.setdefault(logic, {})[label] = merged
    for logic, series in data.items():
        correctness = [reference_answers(list(rows.values())) if not label.startswith('primo') else list(rows.values())
                       for label, rows in series.items()]
        for left, right in itertools.combinations(correctness, 2):
            check_rows(left, right, reference=True)
    report = {'status': 'complete' if final else 'timing-complete', 'plan_sha256': digest(args.out / 'plan.json'),
              'timeout_seconds': plan['config']['timeout_seconds'], 'memory_mib': plan['config']['memory_mib'],
              'runs': reports, 'reference_reuse_sha256': plan['reference_reuse_sha256'], 'reference_benchmark_executions': 0,
              'ground_models_complete': final, 'presentation': 'Defaults plus declared integer profile; full and common-easy-filtered cohorts; no portfolio.'}
    if resumed:
        report['recovery_sha256'] = digest(args.out / 'recovery.json')
    if final:
        plots(args.out, plan, data, report)
    return report


def plots(out: Path, plan: dict, data: dict, report: dict) -> None:
    directory = out / 'plots'
    directory.mkdir(exist_ok=True)
    sections = []
    for logic, series in data.items():
        timeout = plan['config']['timeout_seconds']
        easy_limit = plan['config']['easy_cutoff_seconds']
        names = set(next(iter(series.values())))
        easy = {name for name in names if all(solved(rows[name], timeout) and float(rows[name]['seconds']) < easy_limit for rows in series.values())}
        report.setdefault('filtered', {})[logic] = {
            'removed_common_easy': len(easy), 'retained': len(names - easy),
            'series': {label: metrics([rows[name] for name in sorted(names - easy)], timeout)
                       for label, rows in series.items()}}
        for view, cohort in [('full', names), ('nontrivial', names - easy)]:
            curves = [(DISPLAY[label], sorted(float(rows[name]['seconds']) for name in cohort if solved(rows[name], timeout)))
                      for label, rows in series.items()]
            notes = [f'{len(cohort)} inputs; 120s cutoff; one physical CPU; enforced 8 GiB address space per process.',
                     'Current Primo 7dd8874c (cache source cab1ebc4); references archived 24 Oct, input SHA256 matched.',
                     'Reference 1200s timings censored at 120s. All successful native SAT timing rows direct Dolmen.',
                     f'Removed {len(easy) if view == "nontrivial" else 0} inputs solved by every plotted profile in under {easy_limit}s; no portfolio.']
            filename = f'{logic.lower()}-{view}.svg'
            (directory / filename).write_text(svg(curves, timeout, len(cohort), f'{logic}: {view}', notes,
                                                   highlighted='Primo default', colour_by_name=COLOURS, percentage=True))
        table = ''.join('<tr>'+''.join(f'<td>{html.escape(str(value))}</td>' for value in [DISPLAY[label], stats['solved'], stats['sat'], stats['unsat'], stats['timeouts'], stats['unknown'], stats['memory_out'], stats['errors'], round(stats['par2_sum'], 2)])+'</tr>' for label, stats in report['runs'][logic].items())
        sections.append(f'<section><h2>{logic}</h2><div class="panels"><img src="{logic.lower()}-full.svg" alt="{logic} full cohort"><img src="{logic.lower()}-nontrivial.svg" alt="{logic} shared easy inputs removed"></div><table><tr><th>Solver</th><th>Solved</th><th>SAT</th><th>UNSAT</th><th>Timeout</th><th>Unknown</th><th>OOM</th><th>Error</th><th>PAR2 sum</th></tr>{table}</table></section>')
    (directory / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Uniform120s solver comparison</title><style>body{font:16px system-ui;margin:24px;color:#1f2937}section{margin:40px 0}.panels{display:flex;flex-wrap:wrap}.panels img{width:900px;max-width:100%}table{border-collapse:collapse}th,td{padding:8px 14px;border-bottom:1px solid #ccd}</style><h1>Primo and archived references ·120 seconds</h1><p>Blue: current defaults. Purple: optional fixed integer profile. Shared colours, log time axis and 0–100% solved axis. Full and filtered denominators differ. Historical references are reused with matching input hashes and 8 GiB limits; dates differ. No per-input portfolio. Quantified UF is outside this report.</p>'+''.join(sections))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['gate', 'timing', 'final'])
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    allocation(load(Path(__file__).with_name('config.json')))
    if args.stage == 'gate':
        gate(args)
    else:
        value = collect(args, args.stage == 'final')
        write(args.out / ('qualified-audit.json' if args.stage == 'final' else 'timing-audit.json'), value)


if __name__ == '__main__':
    main()
