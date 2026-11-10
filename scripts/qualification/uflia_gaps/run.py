#!/usr/bin/env python3
"""Run selected native diagnostics, then separately validate their SAT timing rows."""
from __future__ import annotations

import csv
import hashlib
import json
import os
import resource
import subprocess
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    if not os.environ.get('SLURM_JOB_ID'):
        raise SystemExit('SLURM allocation required')
    workspace = Path(os.environ['PRIMO_WORKDIR'])
    config_path = Path(os.environ['PRIMO_GAP_CONFIG'])
    config = json.loads(config_path.read_text())
    index = int(os.environ['SLURM_ARRAY_TASK_ID'])
    case = config['cases'][index]
    root = Path(os.environ['PRIMO_GAP_CORPUS'])
    source = root / case['file']
    binary = workspace / 'build-slurm-rel/primo'
    out = Path(os.environ['PRIMO_GAP_OUT']) / f'case-{index}'
    if digest(source) != case['sha256'] or digest(binary) != config['binary_sha256']:
        raise ValueError('frozen binary/input identity mismatch')
    profiles = config['profiles']
    phase = os.environ['PRIMO_GAP_PHASE']

    def limit_native() -> None:
        resource.setrlimit(resource.RLIMIT_AS, (config['address_space_bytes'],) * 2)

    if phase == 'models':
        if not (out / 'profile-complete.json').exists():
            raise ValueError('missing completed timing/profile stage')
        for label in profiles:
            subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'replay',
                            '--out', str(out / label / 'normal'), '--model-timeout', '180',
                            '--dolmen-timeout', '60', '--dolmen-size', '5G',
                            '--dolmen-arg=--strict=false'], check=True, preexec_fn=limit_native)
        (out / 'models-complete').write_text('All successful SAT timing rows separately direct-Dolmen validated.\n')
        return
    if phase != 'profile':
        raise ValueError('unknown phase')
    out.mkdir(parents=True, exist_ok=False)
    manifest = out / 'inputs.txt'
    manifest.write_text(case['file'] + '\n')
    provenance = {'case': case, 'source_commit': config['source_commit'],
                  'binary_sha256': digest(binary), 'runner_sha256': digest(Path(__file__)),
                  'sampler_sha256': digest(workspace / 'scripts/profile_gdb_samples.py'),
                  'config_sha256': digest(config_path), 'job': os.environ['SLURM_ARRAY_JOB_ID'],
                  'task': index, 'node': os.uname().nodename,
                  'cpu_model': next(line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),
                  'address_space_bytes': config['address_space_bytes'],
                  'normal_timeout': config['normal_timeout'], 'profiles': profiles,
                  'callgrind_limit': '35s diagnostic prefix, SLURM memory only; instrumentation has no RLIMIT_AS',
                  'note': 'Profiler wall times are not native timings; archived cvc5 evidence reused without rerun.'}
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    for tool in ['gdb', 'valgrind']:
        result = subprocess.run([tool, '--version'], check=True, text=True, capture_output=True)
        (out / f'{tool}-version.txt').write_text(result.stdout + result.stderr)
    order = list(profiles)
    if index % 2:
        order.reverse()
    answers = {}
    for label in order:
        command = ['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
                   '--root', str(root), '--manifest', str(manifest),
                   '--out', str(out / label / 'normal'), '--timeout', str(config['normal_timeout']),
                   '--solver', str(binary), '--expected-git', config['expected_git'],
                   '--run-id', f'uflia-gap-20261110-{index}-{label}',
                   *['--solver-arg=' + flag for flag in profiles[label]]]
        subprocess.run(command, check=True, preexec_fn=limit_native)
        with (out / label / 'normal/answers.csv').open() as stream:
            row, = csv.DictReader(stream)
        if row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'}:
            raise ValueError('blocking native outcome; preserve logs and triage before tuning')
        if (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
                and row['answer'] != case['answer']):
            raise ValueError('concrete archived reference disagreement')
        answers[label] = row
    # Profile each mechanism separately after the task's native timings end.
    for label, flags in profiles.items():
        target = out / label
        result = subprocess.run(['python3', 'scripts/profile_gdb_samples.py',
                                 '--out', str(target / 'gdb'), '--timeout', str(config['gdb_seconds']),
                                 '--interval', '0.15', '--seed', str(202611100 + index),
                                 '--', str(binary), *flags, str(source)], preexec_fn=limit_native, check=False)
        sampled = json.loads((target / 'gdb/summary.json').read_text())
        if result.returncode and sampled['diagnostic'] != 'profile deadline exceeded':
            raise ValueError('unexpected profiler failure: ' + str(sampled))
    target = out / 'default'
    with (target / 'callgrind.stdout').open('w') as stdout, (target / 'callgrind.stderr').open('w') as stderr:
        result = subprocess.run(['timeout', '--signal=TERM', '--kill-after=5s',
                                 str(config['callgrind_seconds']), 'valgrind', '--tool=callgrind', '--quiet',
                                 '--callgrind-out-file=' + str(target / 'callgrind.out'),
                                 str(binary), *profiles['default'], str(source)], stdout=stdout, stderr=stderr, check=False)
    if result.returncode not in {0, 124}:
        raise ValueError('unexpected Callgrind failure: ' + str(result.returncode))
    (target / 'callgrind.exit-code').write_text(str(result.returncode) + '\n')
    for mode in ['self', 'inclusive']:
        with (target / f'callgrind-{mode}.txt').open('w') as stream:
            subprocess.run(['callgrind_annotate', '--inclusive=' + ('yes' if mode == 'inclusive' else 'no'),
                            '--threshold=95', str(target / 'callgrind.out')], check=True, stdout=stream)
    if digest(source) != case['sha256'] or digest(binary) != config['binary_sha256']:
        raise ValueError('input/binary changed during run')
    (out / 'profile-complete.json').write_text(json.dumps(answers, indent=2) + '\n')


if __name__ == '__main__':
    main()
