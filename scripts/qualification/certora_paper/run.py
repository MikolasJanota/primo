#!/usr/bin/env python3
"""Focused Certora controls and independent, bounded native stack profiles."""
from __future__ import annotations

import csv
import hashlib
import json
import os
import resource
import subprocess
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    assert os.environ.get('SLURM_JOB_ID')
    workspace = Path(os.environ['PRIMO_WORKDIR'])
    config_path = Path(os.environ['PRIMO_CERT_CONFIG'])
    config = json.loads(config_path.read_text())
    root = Path(os.environ['PRIMO_CORPUS_ROOT'])
    solver = Path(os.environ['PRIMO_CERT_SOLVER'])
    out = Path(os.environ['PRIMO_CERT_OUT'])
    phase = os.environ['PRIMO_CERT_PHASE']
    slot = int(os.environ['SLURM_ARRAY_TASK_ID'])
    names = list(config['profiles'])
    index = slot if phase == 'profile' else slot // len(names)
    case = config['cases'][index]
    source = root / case['file']
    assert sha(source) == case['sha256'] and sha(solver) == config['binary_sha256']

    def limit() -> None:
        resource.setrlimit(resource.RLIMIT_AS, (config['address_space_bytes'],) * 2)

    if phase == 'timing':
        label = names[(slot % len(names) + index) % len(names)]
        target = out / 'timing' / f'slot-{slot}'
        target.mkdir(parents=True, exist_ok=False)
        manifest = target / 'input.txt'
        manifest.write_text(case['file'] + '\n')
        identity = {'case': case, 'profile': label, 'flags': config['profiles'][label],
                    'binary_sha256': sha(solver), 'config_sha256': sha(config_path),
                    'runner_sha256': sha(Path(__file__)), 'job': os.environ['SLURM_ARRAY_JOB_ID'], 'slot': slot}
        (target / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
        subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'benchmark', '--root', str(root),
                        '--manifest', str(manifest), '--solver', str(solver), '--expected-git', config['expected_git'],
                        '--out', str(target / 'normal'), '--timeout', str(config['timeout']),
                        '--run-id', f'certora-paper-20261111-{slot}',
                        *(f'--solver-arg={arg}' for arg in config['profiles'][label])], preexec_fn=limit, check=True)
        with (target / 'normal/answers.csv').open() as stream:
            row, = csv.DictReader(stream)
        assert row['wrong'] == 'false' and row['status'] in ('ok', 'timeout'), row
        assert row['status'] != 'ok' or row['answer'] not in ('sat', 'unsat') or row['answer'] == case['answer'], row
        (target / 'complete').write_text('Timing recorded; SAT models remain separate.\n')
        return
    if phase == 'models':
        target = out / 'timing' / f'slot-{slot}'
        assert (target / 'complete').is_file()
        subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'replay', '--out', str(target / 'normal'),
                        '--model-timeout', '180', '--dolmen-timeout', '60', '--dolmen-size', '1G',
                        '--dolmen-arg=--strict=false'], check=True)
        return
    assert phase == 'profile'
    target = out / 'profile' / f'case-{index}'
    target.mkdir(parents=True, exist_ok=False)
    base = config['profiles']['model-all']
    if index == 0:
        subprocess.run(['python3', 'scripts/qualification/uflia_gaps/shape.py', '--cases', str(config_path),
                        '--root', str(root), '--out', str(out / 'shape.json')], check=True)
    result = subprocess.run(['python3', 'scripts/profile_gdb_samples.py', '--out', str(target / 'gdb'),
                             '--timeout', '35', '--interval', '0.15', '--seed', str(2026111110 + index),
                             '--', str(solver), *base, str(source)], preexec_fn=limit, check=False)
    sampled = json.loads((target / 'gdb/summary.json').read_text())
    assert result.returncode == 0 or sampled['diagnostic'] == 'profile deadline exceeded', sampled
    with (target / 'callgrind.stdout').open('w') as stdout, (target / 'callgrind.stderr').open('w') as stderr:
        result = subprocess.run(['timeout', '--signal=TERM', '--kill-after=5s', '35s', 'valgrind',
                                 '--tool=callgrind', '--quiet', '--callgrind-out-file=' + str(target / 'callgrind.out'),
                                 str(solver), *base, str(source)], stdout=stdout, stderr=stderr, check=False)
    assert result.returncode in (0, 124), result.returncode
    for mode in ('self', 'inclusive'):
        with (target / f'callgrind-{mode}.txt').open('w') as stream:
            subprocess.run(['callgrind_annotate', '--inclusive=' + ('yes' if mode == 'inclusive' else 'no'),
                            '--threshold=95', str(target / 'callgrind.out')], stdout=stream, check=True)
    (target / 'identity.json').write_text(json.dumps({'case': case, 'flags': base, 'binary_sha256': sha(solver),
        'runner_sha256': sha(Path(__file__)), 'config_sha256': sha(config_path), 'job': os.environ['SLURM_ARRAY_JOB_ID'],
        'slot': slot, 'sampler_sha256': sha(workspace / 'scripts/profile_gdb_samples.py'),
        'profile_boundary': '35s native wall-clock stack sampling and35s instrumented instruction prefix; not native timing or CPU fractions'}, indent=2) + '\n')


if __name__ == '__main__':
    main()
