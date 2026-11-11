#!/usr/bin/env python3
"""Audit Certora diagnostic rows/model coverage and summarize bounded profiles."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / "scripts/qualification/uflia_equalities/profile_summary.py").is_file())
sys.path.insert(0, str(ROOT))
from scripts.qualification.uflia_equalities.profile_summary import (  # noqa: E402
    annotated,
)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'config', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    result = {'status': 'complete', 'scope': 'Four existing-option diagnostics; single timing observations, not default qualification',
              'timings': [], 'profiles': [], 'native_sat': 0, 'direct_dolmen': 0, 'artifact_sha256': {},
              'limitations': ['GDB stack counts are overlapping wall-clock observations, not CPU fractions.',
                              'Callgrind prefixes are instrumented and can emphasize preprocessing; not full native solve attribution.',
                              'The new option combinations have diagnostic/model evidence only, without a new merge-grade fuzz gate.']}
    for slot in range(16):
        parent = args.root / 'timing' / f'slot-{slot}'
        identity = json.loads((parent / 'identity.json').read_text())
        assert identity['config_sha256'] == sha(args.config) and identity['binary_sha256'] == config['binary_sha256']
        with (parent / 'normal/answers.csv').open() as stream:
            row, = csv.DictReader(stream)
        assert row['status'] in ('ok', 'timeout') and row['wrong'] == 'false'
        success = row['status'] == 'ok' and row['answer'] in ('sat', 'unsat') and row['exit_code'] == '0'
        assert not success or row['answer'] == identity['case']['answer']
        coverage = json.loads((parent / 'normal/model-validation/coverage.json').read_text())
        selected = int(success and row['answer'] == 'sat')
        assert coverage['status'] == 'complete' and coverage['expected_sat'] == coverage['dolmen_pass'] == selected
        for name, digest in coverage.get('artifacts', {}).items():
            path = parent / 'normal/model-validation' / name
            if digest is None:
                assert not path.exists()
            else:
                assert sha(path) == digest
        assert coverage['inputs_and_binary_unchanged']
        if selected:
            with (parent / 'normal/model-validation/models/summary.csv').open() as stream:
                model, = csv.DictReader(stream)
            assert model['outcome'] == 'pass' and model['validator'] == 'dolmen' and model['solver_exit'] == model['dolmen_exit'] == '0'
        result['native_sat'] += selected
        result['direct_dolmen'] += coverage['dolmen_pass']
        result['timings'].append({'case': slot // 4, 'file': identity['case']['file'], 'profile': identity['profile'],
                                  'status': row['status'], 'answer': row['answer'], 'seconds': float(row['seconds']),
                                  'exit_code': row['exit_code'], 'timeout': row['timeout']})
        for name in ('identity.json', 'normal/answers.csv', 'normal/provenance.json', 'normal/model-validation/coverage.json'):
            result['artifact_sha256'][str((parent / name).relative_to(args.root))] = sha(parent / name)
    for index in range(4):
        parent = args.root / 'profile' / f'case-{index}'
        identity = json.loads((parent / 'identity.json').read_text())
        assert identity['binary_sha256'] == config['binary_sha256'] and identity['config_sha256'] == sha(args.config)
        summary = json.loads((parent / 'gdb/summary.json').read_text())
        samples = [json.loads(line) for line in (parent / 'gdb/samples.jsonl').read_text().splitlines()]
        assert len(samples) == summary['samples'] and len(samples) >= 20
        assert summary['status'] == 'complete' or summary['diagnostic'] == 'profile deadline exceeded'
        needles = {'integer_completion': 'complete_lia_integer_model', 'branch_and_bound': 'branch_and_bound',
                   'patching': 'patch_integer_variables', 'cuts': 'apply_gomory_cuts', 'pivot': 'LraTableau::pivot',
                   'rational_check': 'LraTableau::check', 'care_pairs': 'euf_care_pair_diagnostics',
                   'model_grouping': 'group_by_model_value', 'unit_equality_query': 'model_filtered_unit_equalities',
                   'GMP': '__gmp', 'SAT': 'CaDiCaL::'}
        counts = {name: sum(needle in '\n'.join(s['frames']) for s in samples) for name, needle in needles.items()}
        instructions, functions = annotated(parent / 'callgrind-self.txt')
        total_inclusive, inclusive = annotated(parent / 'callgrind-inclusive.txt')
        assert instructions == total_inclusive
        result['profiles'].append({'case': index, 'samples': len(samples), 'sample_categories': counts,
                                   'callgrind_instructions': instructions, 'top_self_functions': functions[:15],
                                   'selected_inclusive': [f for f in inclusive if any(n in f['function'] for n in ['substitute_solved_constants', 'branch_and_bound', 'euf_care_pair', 'LraTableau::pivot', 'LraTableau::check'])]})
        for name in ('identity.json', 'gdb/summary.json', 'gdb/samples.jsonl', 'callgrind-self.txt', 'callgrind-inclusive.txt'):
            result['artifact_sha256'][str((parent / name).relative_to(args.root))] = sha(parent / name)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print('Audited16native controls,4profiles,', result['native_sat'], 'SAT rows direct-Dolmen validated')


if __name__ == '__main__':
    main()
