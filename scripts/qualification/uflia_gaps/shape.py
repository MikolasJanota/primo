#!/usr/bin/env python3
"""Count lexical/S-expression features of the frozen diagnostic inputs."""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = []
    pattern = re.compile(r';[^\n]*|"(?:[^"]|"")*"|\|[^|]*\||[()]|[^\s()]+')
    for case in json.loads(args.cases.read_text())['cases']:
        path = args.root / case['file']
        source = path.read_text()
        checksum = hashlib.sha256(path.read_bytes()).hexdigest()
        if checksum != case['sha256']:
            raise ValueError('diagnostic input identity mismatch')
        tokens = [x for x in pattern.findall(source) if not x.startswith(';')]
        root = []
        stack, nodes = [root], []
        for token in tokens:
            if token == '(':
                node = []
                stack[-1].append(node)
                stack.append(node)
                nodes.append(node)
            elif token == ')':
                stack.pop()
            else:
                stack[-1].append(token)
        if len(stack) != 1:
            raise ValueError('unbalanced diagnostic input')
        heads = collections.Counter(n[0] for n in nodes if n and isinstance(n[0], str))
        declarations = {n[1]: len(n[2]) for n in root if n and n[0] == 'declare-fun'}
        functions = {name: arity for name, arity in declarations.items() if arity > 0}
        applications = [n for n in nodes if n and isinstance(n[0], str) and n[0] in functions]
        ground = sum(all(isinstance(a, str) and re.fullmatch(r'-?\d+', a) for a in n[1:]) for n in applications)
        integers = [abs(int(t)).bit_length() for t in tokens if re.fullmatch(r'-?\d+', t)]
        output.append({'file': case['file'], 'sha256': checksum, 'bytes': len(source.encode()),
                       'tokens': len(tokens), 'assertions': heads['assert'],
                       'declared_constants': sum(arity == 0 for arity in declarations.values()),
                       'uf_symbols': functions, 'uf_application_occurrences': len(applications),
                       'all_numeral_UF_applications': ground,
                       'operator_occurrences': {name: heads[name] for name in
                                                ['and', 'or', 'not', 'ite', '=', 'distinct', '<', '<=', '>', '>=', '+', '-', '*', 'div', 'mod']},
                       'largest_numeral_bits': max(integers, default=0)})
    args.out.write_text(json.dumps(output, indent=2) + '\n')


if __name__ == '__main__':
    main()
