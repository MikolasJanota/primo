#!/usr/bin/env python3
"""Focused existing-option controls; separate SAT replay follows all timings."""
from __future__ import annotations

import csv
import hashlib
import json
import os
import resource
import subprocess
from pathlib import Path


def main() -> None:
    if not os.environ.get('SLURM_JOB_ID'):
        raise SystemExit('SLURM required')
    config = json.loads(Path(os.environ['PRIMO_GAP_CONFIG']).read_text())
    slot = int(os.environ['SLURM_ARRAY_TASK_ID'])
    case_index, arm = [(i, a) for i in [2, 3, 5] for a in ['no-propagation', 'strong']][slot]
    case = config['cases'][case_index]
    root = Path(os.environ['PRIMO_GAP_CORPUS'])
    binary = Path(os.environ['PRIMO_WORKDIR']) / 'build-slurm-rel/primo'
    out = Path(os.environ['PRIMO_GAP_OUT']) / f'control-{slot}'
    def sha(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(binary) != config['binary_sha256'] or sha(root / case['file']) != case['sha256']:
        raise ValueError('input/binary identity mismatch')

    def limits() -> None:
        resource.setrlimit(resource.RLIMIT_AS, (config['address_space_bytes'],) * 2)

    if os.environ['PRIMO_GAP_PHASE'] == 'models':
        subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'replay',
                        '--out', str(out / 'normal'), '--model-timeout', '180', '--dolmen-timeout', '60',
                        '--dolmen-size', '5G', '--dolmen-arg=--strict=false'], check=True, preexec_fn=limits)
        (out / 'models-complete').write_text('Successful SAT row directly validated separately.\n')
        return
    out.mkdir(parents=True, exist_ok=False)
    manifest = out / 'inputs.txt'
    manifest.write_text(case['file'] + '\n')
    flags = ['--quiet', '--stats', '--no-lra-propagation' if arm == 'no-propagation' else '--preprocessing-mode=strong']
    identity = {'case_index': case_index, 'case': case, 'arm': arm, 'flags': flags,
                'binary_sha256': sha(binary), 'runner_sha256': sha(Path(__file__)),
                'config_sha256': sha(Path(os.environ['PRIMO_GAP_CONFIG'])),
                'job': os.environ['SLURM_ARRAY_JOB_ID'], 'task': slot,
                'address_space_bytes': config['address_space_bytes'], 'timeout_seconds': 120,
                'note': 'Single existing-option diagnostic. Different search traces do not isolate direct mechanism overhead.'}
    (out / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
                    '--root', str(root), '--manifest', str(manifest), '--out', str(out / 'normal'),
                    '--solver', str(binary), '--expected-git', config['expected_git'], '--timeout', '120',
                    '--run-id', f'uflia-control-20261110-{slot}',
                    *['--solver-arg=' + flag for flag in flags]], check=True, preexec_fn=limits)
    with (out / 'normal/answers.csv').open() as stream:
        row, = csv.DictReader(stream)
    if row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'} or (
            row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'} and row['answer'] != case['answer']):
        raise ValueError('blocking control outcome; preserve logs before further performance work')
    (out / 'timing-complete').write_text('Timing recorded; SAT qualification remains separate.\n')


if __name__ == '__main__':
    main()
