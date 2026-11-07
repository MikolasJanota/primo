#!/usr/bin/env python3
"""Summarize instruction profiles and native stack observations without CPU-time claims."""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


def annotated(path: Path) -> tuple[int, list[dict]]:
    text = path.read_text()
    total = re.search(r'^\s*([\d,]+)\s+\(100\.0%\)\s+PROGRAM TOTALS', text, re.M)
    assert total, path
    functions = []
    for line in text.splitlines():
        match = re.match(r'^\s*([\d,]+)\s+\(\s*([\d.]+)%\)\s+(.*)$', line)
        if not match or match[3] == 'PROGRAM TOTALS':
            continue
        name = re.sub(r' \[/[^\]]+\]$', '', match[3])
        assert '/home/' not in name
        functions.append({'function': name, 'instructions': int(match[1].replace(',', '')),
                          'percent_of_instructions': float(match[2])})
    return int(total[1].replace(',', '')), functions


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = {'callgrind': {}, 'native_stacks': {},
              'limitations': ['Callgrind instruction counts are not CPU cycles or native runtime.',
                              'Bounded instrumented prefixes reach unequal search states.',
                              'Inclusive functions may overlap; do not add their percentages.',
                              'Native samples include probe overhead, not CPU-time attribution.']}
    for index in range(4):
        path = args.root / 'callgrind' / f'case-{index}'
        assert (path / 'complete').is_file()
        profiles = {}
        for role in ['off', 'on']:
            total, functions = annotated(path / (role + '-self.txt'))
            inclusive_total, inclusive = annotated(path / (role + '-inclusive.txt'))
            assert total == inclusive_total
            code = int((path / (role + '.exit-code')).read_text())
            assert code in {0, 124}
            answer = (path / (role + '.stdout')).read_text().splitlines()[0]
            assert answer in {'sat', 'unsat', 'unknown'}
            keys = ['LraTableau::pivot(', 'LiaTheoryComponent::assign(', 'LraTableau::pop(',
                    'BooleanClausifier::assert_formula(', 'entailed_equalities(']
            profiles[role] = {'instruction_total': total, 'exit_code': code,
                              'bounded_timeout': code == 124, 'answer': answer,
                              'top_self_functions': functions[:12],
                              'selected_inclusive_functions': [f for f in inclusive if any(k in f['function'] for k in keys)]}
        result['callgrind'][str(index)] = profiles
    for index in [0, 3]:
        path = args.root / 'native-stacks' / f'case-{index}'
        assert (path / 'complete').is_file()
        profiles = {}
        for role in ['off', 'on']:
            data = json.loads((path / (role + '.json')).read_text())
            stages, leaves, operations = Counter(), Counter(), Counter()
            gmp = 0
            for sample in data['samples']:
                frames = sample['frames']
                stack = '\n'.join(frames)
                stage = ('integer completion' if any(s in stack for s in ['branch_and_bound(', 'complete_integer_assignment(', 'complete_lia_integer_model('])
                         else 'SAT and callbacks' if any('CaDiCaL::' in f for f in frames)
                         else 'equality scan' if 'entailed_equalities(' in stack
                         else 'other or truncated')
                stages[stage] += 1
                operation = ('pivot' if 'LraTableau::pivot' in stack
                             else 'LRA update' if 'LraTableau::update(' in stack
                             else 'LIA assignment' if 'LiaTheoryComponent::assign' in stack
                             else 'LRA check' if 'LraTableau::check' in stack
                             else 'EUF' if any(s in stack for s in ['OnlineEufPropagator', 'CongruenceClosure'])
                             else 'other')
                operations[operation] += 1
                gmp += int(any('gmp' in f.lower() for f in frames))
                if frames:
                    leaves[frames[0]] += 1
            profiles[role] = {'samples': len(data['samples']), 'stages': dict(stages),
                              'operations': dict(operations), 'stacks_containing_GMP': gmp,
                              'top_leaf_functions': leaves.most_common(10),
                              'sampled_resume_seconds': sum(s['running_seconds'] for s in data['samples'])}
        result['native_stacks'][str(index)] = profiles
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: {role: {'samples': v['samples'], 'stages': v['stages']}
                         for role, v in profiles.items()}
                      for k, profiles in result['native_stacks'].items()}, indent=2))


if __name__ == '__main__':
    main()
