#!/usr/bin/env python3
"""Audit selected diagnostic/model artifacts and export portable profile evidence."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path

PATTERNS = {'simplex_check': 'LraTableau::check_impl', 'pivot': 'LraTableau::pivot',
            'online_LIA_assign': 'LiaTheoryComponent::assign',
            'integer_search': 'branch_and_bound', 'patching': 'patch_integer_variables',
            'shared_regrouping': 'group_by_model_value', 'equality_scan': 'entailed_equalities',
            'care_pair_construction': 'euf_care_pair_diagnostics', 'GMP': '__gmp'}


def load(path: Path) -> dict:
    return json.loads(path.read_text())


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(root: Path, config_path: Path) -> dict:
    config = load(config_path)
    records = []
    rejected = []
    for index, case in enumerate(config['cases']):
        directory = root / f'case-{index}'
        provenance = load(directory / 'provenance.json')
        if provenance['case'] != case or provenance['binary_sha256'] != config['binary_sha256']:
            raise ValueError('profile identity mismatch')
        if not (directory / 'models-complete').exists():
            raise ValueError('primary model stage incomplete')
        for arm in config['profiles']:
            records.append(read_row(directory / arm, case, arm, config, True, rejected))
    for index, (case_index, arm) in enumerate((i, a) for i in [2, 3, 5] for a in ['no-propagation', 'strong']):
        directory = root / f'control-{index}'
        identity = load(directory / 'identity.json')
        case = config['cases'][case_index]
        if identity['case'] != case or identity['binary_sha256'] != config['binary_sha256']:
            raise ValueError('control identity mismatch')
        records.append(read_row(directory, case, arm, config, False, rejected))
    return {'status': 'diagnosis complete; strong preprocessing control rejected on one model replay',
            'source_commit': config['source_commit'], 'binary_sha256': config['binary_sha256'],
            'cases_sha256': digest(config_path), 'summarizer_sha256': digest(Path(__file__)),
            'rows': records, 'rejected_model_replays': rejected,
            'model_counts': dict(collections.Counter(r['model_status'] for r in records)),
            'limits': 'Single diagnostic timings, not repeated acceptance measurements. GDB samples are inclusive wall-clock observations; categories overlap. Callgrind is a35s instrumented prefix, not native timing.',
            'reference_corpus_executions': 0}


def read_row(directory: Path, case: dict, arm: str, config: dict, profiles: bool, rejected: list) -> dict:
    normal = directory / 'normal'
    parent = load(normal / 'provenance.json')
    if (parent['status'] != 'complete' or parent['solver']['sha256'] != config['binary_sha256']
            or parent['inputs'] != {case['file']: case['sha256']}):
        raise ValueError('native timing identity mismatch')
    with (normal / 'answers.csv').open() as stream:
        row, = csv.DictReader(stream)
    if row['status'] not in {'ok', 'timeout'} or row['wrong'] != 'false':
        raise ValueError('blocking timing outcome')
    successful = (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
                  and row['exit_code'] == '0' and row['timeout'] == 'false' and float(row['seconds']) <= 120)
    if successful and row['answer'] != case['answer']:
        raise ValueError('archived reference disagreement')
    coverage_path = normal / 'model-validation/coverage.json'
    coverage = load(coverage_path)
    if (coverage['provenance']['parent_answers_sha256'] != digest(normal / 'answers.csv')
            or coverage['provenance']['parent_provenance_sha256'] != digest(normal / 'provenance.json')
            or not coverage['inputs_and_binary_unchanged']
            or coverage['expected_sat'] != int(successful and row['answer'] == 'sat')):
        raise ValueError('model-parent identity mismatch')
    for relative, checksum in coverage['artifacts'].items():
        if checksum and digest(normal / 'model-validation' / relative) != checksum:
            raise ValueError('model artifacts changed')
    model_status = 'not-selected-SAT'
    if coverage['status'] == 'complete':
        if coverage['expected_sat']:
            if coverage['dolmen_pass'] != 1:
                raise ValueError('missing direct Dolmen validation')
            model_status = 'direct-Dolmen-pass'
    else:
        # Preserve the exact failed strong-control scope; never count it as a
        # validated speed result or conceal it in the successful rows.
        messages = list((normal / 'model-validation/models').glob('*.solver.stderr'))
        if (profiles or arm != 'strong' or case != config['cases'][3]
                or coverage['outcomes'] != {'solver-error': 1} or len(messages) != 1
                or messages[0].read_text().strip() != 'model: assembled component model falsifies a final assertion'):
            raise ValueError('untriaged correctness failure')
        model_status = 'rejected-internal-model-assembly-error'
        rejected.append({'file': case['file'], 'sha256': case['sha256'], 'profile': arm,
                         'diagnostic': messages[0].read_text().strip(),
                         'stderr_sha256': digest(messages[0]), 'coverage_sha256': digest(coverage_path),
                         'scope': 'SAT agrees with archived references; strong-preprocessing model replay exits2 before external validation. Equality-sharing model passes on this input; default times out and supplies no SAT model row. No invalid model was accepted.'})
    result = {'file': case['file'], 'sha256': case['sha256'], 'profile': arm,
              'arguments': parent['solver_args'], 'status': row['status'], 'answer': row['answer'],
              'exit_code': row['exit_code'], 'seconds': float(row['seconds']), 'successful_timing': successful,
              'cvc5_archived_seconds': case['cvc5_seconds'], 'model_status': model_status,
              'coverage_sha256': digest(coverage_path), 'timing_sha256': digest(normal / 'answers.csv'),
              'counters': {k: v for k, v in row.items() if k.startswith(':') and re.fullmatch(r'-?\d+(?:\.\d+)?', v)}}
    if profiles:
        sampled = load(directory / 'gdb/summary.json')
        if sampled['executable_sha256'] != config['binary_sha256'] or (
                sampled['status'] != 'complete' and sampled['diagnostic'] != 'profile deadline exceeded'):
            raise ValueError('unexpected sampler outcome')
        samples = [json.loads(line) for line in (directory / 'gdb/samples.jsonl').read_text().splitlines()]
        if len(samples) != sampled['samples']:
            raise ValueError('incomplete samples')
        result['samples'] = {'total': len(samples),
                             'inclusive': {name: sum(any(needle in f for f in s['frames']) for s in samples)
                                           for name, needle in PATTERNS.items()},
                             'sha256': digest(directory / 'gdb/samples.jsonl')}
        if arm == 'default':
            text = (directory / 'callgrind-self.txt').read_text()
            totals, functions = None, []
            for line in text.splitlines():
                match = re.match(r'\s*([\d,]+)\s+\(\s*([\d.]+)%\)\s+(.+)', line)
                if not match:
                    continue
                instructions = int(match[1].replace(',', ''))
                if match[3] == 'PROGRAM TOTALS':
                    totals = instructions
                else:
                    functions.append({'instructions': instructions, 'percent': float(match[2]),
                                      'function': match[3].split(' [/')[0]})
            result['callgrind_default_prefix'] = {'total_instructions': totals, 'top_self': functions[:15],
                                                  'exit_code': int((directory / 'callgrind.exit-code').read_text()),
                                                  'raw_profile_sha256': digest(directory / 'callgrind.out')}
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.write_text(json.dumps(summarize(args.root, args.cases), indent=2) + '\n')


if __name__ == '__main__':
    main()
