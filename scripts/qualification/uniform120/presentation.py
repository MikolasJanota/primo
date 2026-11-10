#!/usr/bin/env python3
"""Redraw qualified timings with one strongest measured Primo profile per logic."""
from __future__ import annotations

import argparse
import csv
import gzip
import html
import shutil
import tempfile
from pathlib import Path

from analyze import analyze
from campaign import digest, load, write
from report import COLOURS, DISPLAY, solved, svg


def present(report: Path, gallery: Path) -> None:
    # Verify original qualification, paired bytes, scores and filtering before
    # deriving a presentation. Measurement and original export files stay intact.
    analyze(report)
    parent = load(report / 'provenance.json')
    summary = load(report / 'summary.json')
    archive = gallery / 'all-profiles'
    archive.mkdir(parents=True, exist_ok=True)
    for name, expected in parent['plots_sha256'].items():
        target = archive / name
        if not target.exists():
            if digest(gallery / name) != expected:
                raise ValueError('original gallery changed before archival')
            shutil.copyfile(gallery / name, target)
        if digest(target) != expected:
            raise ValueError('archived original gallery identity mismatch')
    data = {}
    with gzip.open(report / 'timing.csv.gz', 'rt') as stream:
        for row in csv.DictReader(stream):
            data.setdefault(row['logic'], {}).setdefault(row['profile'], {})[row['file']] = row
    colours = {'Primo': COLOURS['Primo default'],
               **{name: colour for name, colour in COLOURS.items() if not name.startswith('Primo')}}
    plots, choices, sections = {}, {}, []
    timeout = summary['timeout_seconds']
    with tempfile.TemporaryDirectory(prefix='.presentation-', dir=gallery) as temporary:
        stage = Path(temporary)
        for logic, all_series in data.items():
            scores = summary['runs'][logic]
            # One static profile for the whole logic, selected on full-corpus
            # solved count, then PAR2; never pick a profile per input or view.
            best = min((label for label in scores if label.startswith('primo')),
                       key=lambda label: (-scores[label]['solved'], scores[label]['par2_sum'], label))
            series = {label: rows for label, rows in all_series.items()
                      if not label.startswith('primo') or label == best}
            choices[logic] = {'profile': best, 'arguments': scores[best]['arguments'],
                              'solved': scores[best]['solved'], 'par2_sum': scores[best]['par2_sum']}
            names = set(next(iter(series.values())))
            easy = {name for name in names if all(solved(rows[name], timeout) and float(rows[name]['seconds']) < 1
                                                  for rows in series.values())}
            plots[logic] = {}
            for view, cohort in [('full', names), ('nontrivial', names - easy)]:
                curves = [('Primo' if label == best else DISPLAY[label],
                           sorted(float(rows[name]['seconds']) for name in cohort if solved(rows[name], timeout)))
                          for label, rows in series.items()]
                plots[logic][view] = {'inputs': len(cohort), 'series': dict(curves)}
                notes = [f'{len(cohort)} inputs; 120s cutoff; one physical CPU; enforced 8 GiB address space per process.',
                         'Primo 7dd8874c (cache source cab1ebc4); references archived 24 Oct, input SHA256 matched.',
                         'Reference 1200s timings censored at 120s. All successful native SAT timing rows direct Dolmen.',
                         f'Removed {len(easy) if view == "nontrivial" else 0} inputs solved by every displayed solver in under 1s.']
                (stage / f'{logic.lower()}-{view}.svg').write_text(svg(
                    curves, timeout, len(cohort), f'{logic}: {view}', notes,
                    highlighted='Primo', colour_by_name=colours, percentage=True))
            table = ''.join('<tr>' + ''.join(f'<td>{html.escape(str(value))}</td>' for value in
                                           ['Primo' if label == best else DISPLAY[label], scores[label]['solved'],
                                            round(scores[label]['par2_sum'], 2)]) + '</tr>' for label in series)
            sections.append(f'<section><h2>{logic}</h2><div class="panels"><img src="{logic.lower()}-full.svg" alt="{logic} full cohort"><img src="{logic.lower()}-nontrivial.svg" alt="{logic} common easy inputs removed"></div><table><tr><th>Solver</th><th>Solved</th><th>PAR2 sum</th></tr>{table}</table></section>')
        (stage / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Uniform120s solver comparison</title><style>body{font:16px system-ui;margin:24px;color:#1f2937}section{margin:40px 0}.panels{display:flex;flex-wrap:wrap}.panels img{width:900px;max-width:100%}table{border-collapse:collapse}th,td{padding:8px 14px;border-bottom:1px solid #ccd}</style><h1>Primo and archived references ·120 seconds</h1><p>Blue: Primo. One measured configuration per theory, chosen by full-corpus solved count, then PAR2. Shared colours, log time axis and 0–100% solved axis. Filtered views remove inputs every displayed solver finishes in under one second. Reference timings are reused; no new solver runs.</p>' + ''.join(sections))
        for path in stage.iterdir():
            shutil.copyfile(path, gallery / path.name)
    write(report / 'presentation-plot-data.json', {'timeout_seconds': timeout, 'colours': colours,
                                                'percentage': True, 'plots': plots})
    write(report / 'presentation.json', {
        'kind': 'single-Primo-profile presentation of qualified measurements',
        'selection': 'Per logic: most full-corpus solves, then lowest full-corpus PAR2, then profile ID; one fixed profile for every input and both views.',
        'selected_profiles': choices, 'display_name': 'Primo',
        'filter': 'Remove inputs every displayed solver solves successfully in under1s; recomputed after profile selection.',
        'source_provenance_sha256': digest(report / 'provenance.json'),
        'source_timing_sha256': parent['timing_table_sha256'],
        'source_qualification_sha256': parent['qualified_audit_sha256'],
        'generator_sha256': digest(Path(__file__)),
        'plot_data_sha256': digest(report / 'presentation-plot-data.json'),
        'plots_sha256': {name: digest(gallery / name) for name in parent['plots_sha256']},
        'original_gallery': 'docs/_static/uniform120-20261109/all-profiles',
        'solver_executions': 0})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--gallery', type=Path, required=True)
    args = parser.parse_args()
    present(args.report, args.gallery)


if __name__ == '__main__':
    main()
