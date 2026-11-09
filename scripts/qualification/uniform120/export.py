#!/usr/bin/env python3
"""Export qualified plot data and timing rows without private execution paths."""
from __future__ import annotations

import argparse
import copy
import csv
import gzip
import hashlib
import io
from pathlib import Path, PurePosixPath

from campaign import digest, load, recovery, resources, verify_resource_workflow, write
from report import COLOURS, DISPLAY, memory_exhausted, metrics, solved
from report import plots as draw_plots


def export(out: Path, destination: Path) -> None:
    plan = load(out / 'plan.json')
    audit = load(out / 'qualified-audit.json')
    resumed = recovery(out, plan)
    verify_resource_workflow((resumed or plan).get('resource_workflow_sha256'))
    if audit.get('recovery_sha256') != (digest(out / 'recovery.json') if resumed else None):
        raise ValueError('qualified audit belongs to a different recovery certificate')
    if (audit['status'] != 'complete' or not audit['ground_models_complete']
            or audit['plan_sha256'] != digest(out / 'plan.json')
            or audit['reference_reuse_sha256'] != digest(out / 'reference-reuse.json')):
        raise ValueError('complete, identity-matched qualification is required')
    references = load(out / 'reference-reuse.json')
    destination.mkdir(parents=True, exist_ok=False)
    fields = ['logic', 'profile', 'file', 'sha256', 'family', 'status', 'answer',
              'expected', 'wrong', 'seconds', 'timeout', 'exit_code', 'memory_exhausted',
              'resource_proof_sha256',
              'original_status', 'original_answer', 'original_seconds',
              'original_exit_code', 'original_timeout']
    cohorts, plots, data = {}, {}, {}
    # A deterministic compressed table keeps the full input/profile pairing.
    with (destination / 'timing.csv.gz').open('wb') as raw, gzip.GzipFile(
            filename='', mode='wb', fileobj=raw, mtime=0) as compressed, io.TextIOWrapper(
                compressed, encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for run in plan['runs']:
            logic, label = run['logic'], run['label']
            inventory_path = out / 'inventory' / f'{logic}.json'
            bound = {task['inventory_sha256'] for task in plan['tasks'] if task['logic'] == logic}
            if bound != {digest(inventory_path)}:
                raise ValueError('input inventory changed after planning')
            corpus = load(inventory_path)
            inventory = corpus['inputs']
            rows = []
            proofs = {}
            for shard in range(run['shards']):
                path = out / 'timing' / logic / label / f'shard-{shard}' / 'answers.csv'
                expected_digest = (
                    load(path.parent / 'model-validation/coverage.json')['provenance']['parent_answers_sha256']
                    if run['solver'] == 'primo' else
                    references[f'{logic}/{label}']['derived_csv_sha256'][str(shard)])
                if digest(path) != expected_digest:
                    raise ValueError('timing rows changed after qualification')
                with path.open() as source:
                    raw_rows = list(csv.DictReader(source))
                classified = resources.annotate(raw_rows, path.parent, run, plan, corpus) if run['solver'] == 'primo' else raw_rows
                for raw_row, classified_row in zip(raw_rows, classified):
                    if classified_row.get('_confirmed_oom'):
                        identity = resources.identity(raw_row, run, plan, corpus)
                        proofs[raw_row['file']] = digest(path.parent / 'resource-triage' / resources.key(identity) / 'proof.json')
                rows.extend(classified)
            if len(rows) != len(inventory) or {r['file'] for r in rows} != set(inventory):
                raise ValueError('export coverage mismatch')
            measured = metrics(rows, 120)
            if any(audit['runs'][logic][label][name] != value for name, value in measured.items()):
                raise ValueError('export metrics differ from qualified audit')
            selected = {}
            for row in sorted(rows, key=lambda r: r['file']):
                name = PurePosixPath(row['file'])
                if name.is_absolute() or '..' in name.parts:
                    raise ValueError('input identifier is not corpus-relative')
                public = {key: row.get(key, '') for key in fields}
                public.update(logic=logic, profile=label, sha256=inventory[row['file']]['sha256'],
                              memory_exhausted=str(memory_exhausted(row)).lower())
                public['resource_proof_sha256'] = proofs.get(row['file'], '')
                writer.writerow(public)
                selected[row['file']] = float(row['seconds']) if solved(row, 120) else None
            cohorts.setdefault(logic, {})[label] = selected
            data.setdefault(logic, {})[label] = {r['file']: r for r in rows}
    for logic, series in cohorts.items():
        names = set(next(iter(series.values())))
        easy = {name for name in names if all(rows[name] is not None and rows[name] < 1
                                             for rows in series.values())}
        plots[logic] = {}
        for view, cohort in [('full', names), ('nontrivial', names - easy)]:
            plots[logic][view] = {'inputs': len(cohort), 'series': {
                DISPLAY[label]: sorted(rows[name] for name in cohort if rows[name] is not None)
                for label, rows in series.items()}}
    write(destination / 'plot-data.json', {'timeout_seconds': 120, 'colours': COLOURS,
                                         'percentage': True, 'plots': plots})
    regenerated = copy.deepcopy(audit)
    draw_plots(destination, plan, data, regenerated)
    if regenerated['filtered'] != audit.get('filtered'):
        raise ValueError('filtered metrics differ from qualified audit')
    write(destination / 'summary.json', audit)
    write(destination / 'provenance.json', {
        'plan_sha256': digest(out / 'plan.json'),
        'qualified_audit_sha256': digest(out / 'qualified-audit.json'),
        'reference_reuse_sha256': digest(out / 'reference-reuse.json'),
        'tools': {label: {'sha256': tool['sha256'], 'version': tool['version'].splitlines()[0],
                          'full_version_text_sha256': hashlib.sha256(tool['version'].encode()).hexdigest()}
                  for label, tool in plan['tools'].items()},
        'runs': plan['runs'], 'configuration': plan['config'],
        'exporter_sha256': digest(Path(__file__)),
        'timing_table_sha256': digest(destination / 'timing.csv.gz'),
        'plot_data_sha256': digest(destination / 'plot-data.json'),
        'plots_sha256': {path.name: digest(path) for path in sorted((destination / 'plots').iterdir()) if path.is_file()},
        'reference_benchmark_executions': 0})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    export(args.out, args.destination)


if __name__ == '__main__':
    main()
