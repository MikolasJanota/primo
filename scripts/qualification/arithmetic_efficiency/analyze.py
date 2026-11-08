#!/usr/bin/env python3
"""Audit focused arithmetic profiles; instrumented prefixes are not native timings."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path

PATTERNS = {
    'boolean_flatten': 'flatten_associative_booleans(',
    'cite': 'simplify_constant_ites(',
    'clausifier': 'BooleanClausifier::',
    'cadical': 'CaDiCaL::',
    'pivot': 'LraTableau::pivot(',
    'update': 'LraTableau::update(',
    'freedom_interval': 'LraTableau::freedom_interval_impl(',
    'patching': 'patch_integer_variables(',
    'branching': 'branch_and_bound(',
    'gomory': 'apply_gomory_cuts(',
    'integer_completion': 'complete_integer_assignment(',
    'pop': 'LraTableau::pop(',
    'violation_rebuild': 'LraTableau::rebuild_violated_vars(',
    'check': 'LraTableau::check_impl(',
    'rational': 'primo::util::Rational::',
    'gmp': '__gmp',
    'model_diversify': 'LraTableau::diversify_shared_model(',
    'model_value_grouping': 'group_by_model_value<',
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normal(path: Path, case: dict, native_hash: str | None) -> tuple[dict, dict]:
    identity = json.loads((path / 'provenance.json').read_text())
    with (path / 'answers.csv').open() as stream:
        row, = csv.DictReader(stream)
    assert identity['status'] == 'complete' and identity['inputs_unchanged']
    assert identity['inputs'] == {case['file']: case['input_sha256']}
    assert row['file'] == case['file'] and row['wrong'] == 'false'
    assert row['status'] in {'ok', 'timeout'}
    if row['status'] == 'ok':
        assert row['exit_code'] == '0'
    if native_hash:
        assert identity['solver']['sha256'] == native_hash
    passed_model = False
    validation = None
    if native_hash and row['status'] == 'ok' and row['answer'] == 'sat':
        cover = json.loads((path / 'model-validation/coverage.json').read_text())
        assert cover['status'] == 'complete' and cover['expected_sat'] == cover['dolmen_pass'] == 1
        assert cover['inputs_and_binary_unchanged']
        assert cover['provenance']['parent_answers_sha256'] == digest(path / 'answers.csv')
        assert cover['provenance']['parent_provenance_sha256'] == digest(path / 'provenance.json')
        passed_model = True
        proof = cover['provenance']
        validation = {key: proof[key] for key in ('validator_script_sha256', 'solver_timeout',
                                                  'dolmen_timeout', 'dolmen_args', 'dolmen_size')}
        validation['dolmen'] = {key: proof['dolmen'][key] for key in ('sha256', 'version')}
    result = {key: row[key] for key in ('status', 'answer', 'seconds', 'exit_code', 'timeout')}
    result['seconds'] = float(result['seconds'])
    result['model_pass'] = passed_model
    result['model_validation'] = validation
    result['solver_args'] = identity['solver_args']
    result['solver'] = {key: identity['solver'][key] for key in ('sha256', 'version')}
    result['statistics'] = {key: int(value) for key, value in row.items()
                            if key.startswith(':') and value not in {'', '0'}}
    return result, identity


def callgrind(path: Path) -> dict:
    text = path.read_text()
    match = re.search(r'^\s*([\d,]+)\s+\(100\.0%\)\s+PROGRAM TOTALS', text, re.MULTILINE)
    assert match, path
    functions = []
    for line in text.splitlines():
        row = re.match(r'^\s*([\d,]+)\s+\(\s*([\d.]+)%\)\s+(.*)$', line)
        if not row or row[3] == 'PROGRAM TOTALS':
            continue
        name = re.sub(r' \[/[^\]]+\]$', '', row[3]).split(':', 1)[-1]
        assert '/home/' not in name
        functions.append({'function': name, 'instructions': int(row[1].replace(',', '')),
                          'percent': float(row[2])})
    selected = [r for r in functions if any(v in r['function'] for v in PATTERNS.values())]
    return {'instructions': int(match[1].replace(',', '')), 'top_functions': functions[:15],
            'selected_functions': selected}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--experiment', type=Path, required=True)
    parser.add_argument('--flattening', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    experiment = json.loads(args.experiment.read_text())
    result = {'source_commit': experiment['source_commit'], 'review_commit': experiment['review_commit'],
              'limitations': ['Twelve selected cases are a diagnostic sample, not a corpus evaluation.',
                              'GDB samples are perturbed wall-clock observations; memberships overlap.',
                              'Callgrind counts instructions, not CPU cycles; capped prefixes can reach unequal states.',
                              'Inclusive instruction percentages can overlap or exceed 100%; do not add them.',
                              'A fast control can have too few native stack samples for attribution.',
                              'Only the normal runs supply native runtimes; model replay is separate.'],
              'cases': {}, 'flattening': {}}
    model_passes = 0
    for index, selected in enumerate(experiment['cases']):
        root = args.root / f'case-{index}'
        assert (root / 'complete').is_file(), root
        metadata = json.loads((root / 'provenance.json').read_text())
        assert metadata['method_sha256'] == experiment['method_sha256']
        assert metadata['binary_sha256'] == experiment['binary_sha256']
        assert metadata['case']['logic'] == selected['logic'] and metadata['case']['file'] == selected['file']
        if selected.get('sha256'):
            assert metadata['input_sha256'] == selected['sha256']
        case = {'logic': selected['logic'], 'file': selected['file'], 'reason': selected['reason'],
                'input_sha256': metadata['input_sha256'], 'cpu_model': metadata['cpu_model'],
                'normal': {}, 'profiles': {}}
        labels = ['default', 'cube-m124'] if selected['logic'] == 'QF_LIA' else ['default']
        known = set()
        for label in [*labels, 'z3']:
            data, identity = normal(root / label / 'normal', case,
                                    None if label == 'z3' else experiment['binary_sha256'])
            case['normal'][label] = data
            model_passes += data['model_pass']
            if data['status'] == 'ok' and data['answer'] in {'sat', 'unsat'}:
                known.add(data['answer'])
            if label == 'z3':
                continue
            sample_root = root / label / 'gdb'
            sampled = json.loads((sample_root / 'summary.json').read_text())
            assert sampled['status'] == 'complete' or sampled['diagnostic'] == 'profile deadline exceeded'
            assert sampled['executable_sha256'] == experiment['binary_sha256']
            assert sampled['command'] == [identity['solver']['path'], *data['solver_args'],
                                          str(Path(identity['root']) / case['file'])]
            samples = [json.loads(line) for line in (sample_root / 'samples.jsonl').read_text().splitlines()]
            assert len(samples) == sampled['samples']
            memberships = {key: sum(any(value in f for f in s['frames']) for s in samples)
                           for key, value in PATTERNS.items()}
            first = collections.Counter(next((f for f in s['frames'] if f.startswith(('primo::smt::', 'primo::theory::'))),
                                              s['frames'][0] if s['frames'] else '?') for s in samples)
            leaves = collections.Counter(s['frames'][0] for s in samples if s['frames'])
            code = int((root / label / 'callgrind.exit-code').read_text())
            assert code in {0, 124}
            self_cost = callgrind(root / label / 'callgrind-self.txt')
            inclusive = callgrind(root / label / 'callgrind-inclusive.txt')
            assert self_cost['instructions'] == inclusive['instructions']
            case['profiles'][label] = {'gdb': {'status': sampled['status'], 'diagnostic': sampled['diagnostic'],
                'samples': len(samples), 'memberships': memberships,
                'first_project_functions': first.most_common(12), 'leaf_functions': leaves.most_common(12)},
                'callgrind': {'exit_code': code, 'capped': code == 124, 'self': self_cost,
                              'inclusive': inclusive}}
        assert len(known) <= 1, ('reference disagreement', case['file'])
        result['cases'][str(index)] = case
    if args.flattening:
        for index in (0, 1, 6, 11):
            root = args.flattening / f'case-{index}'
            assert (root / 'complete').is_file(), root
            metadata = json.loads((root / 'provenance.json').read_text())
            case = result['cases'][str(index)]
            assert metadata['input_sha256'] == case['input_sha256']
            assert metadata['method_sha256'] == experiment['flattening']['method_sha256']
            known, data = set(), {}
            for round_id in range(2):
                for cap in (0, 256, 2048):
                    row, _ = normal(root / f'round-{round_id}' / f'post-{cap}', case,
                                    experiment['binary_sha256'])
                    assert row['solver_args'] == ['--quiet', '--stats', '--boolean-flatten-post-threshold=' + str(cap)]
                    data[f'{round_id}/{cap}'] = row
                    model_passes += row['model_pass']
                    if row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}:
                        known.add(row['answer'])
            reference, _ = normal(root / 'z3', case, None)
            if reference['status'] == 'ok' and reference['answer'] in {'sat', 'unsat'}:
                known.add(reference['answer'])
            assert len(known) <= 1
            result['flattening'][str(index)] = {'rounds': data, 'z3': reference}
    result['native_model_passes'] = model_passes
    references = {case['normal']['z3']['solver']['sha256'] for case in result['cases'].values()}
    references.update(case['z3']['solver']['sha256'] for case in result['flattening'].values())
    assert len(references) == 1, 'reference binary changed between cases'
    result['reference'] = result['cases']['0']['normal']['z3']['solver']
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Audited', len(result['cases']), 'cases;', model_passes, 'separate native SAT model passes')


if __name__ == '__main__':
    main()
