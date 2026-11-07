#!/usr/bin/env python3
"""Compare frozen baseline and candidate serially on identical QF_UFLIA inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402

M124 = ['--lia-round-integer-bounds', '--lia-primitive-int-rows',
        '--no-lia-integer-slacks', '--lia-patch-integral-moves']
NATIVE = ['--no-lia-round-integer-bounds', '--no-lia-primitive-int-rows',
          '--no-lia-integer-slacks', '--no-lia-patch-integral-moves']


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'root', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--baseline-git', required=True)
    parser.add_argument('--candidate-git', required=True)
    parser.add_argument('--shards', type=int, default=12)
    parser.add_argument('--shard', type=int, required=True)
    parser.add_argument('--timeout', type=float, default=15)
    args = parser.parse_args()
    if args.out.exists() or args.timeout <= 0 or not 0 <= args.shard < args.shards:
        parser.error('fresh output, positive timeout and valid shard required')
    args.out.mkdir(parents=True)
    binaries = {}
    for name in ('baseline', 'candidate'):
        path = getattr(args, name).resolve()
        version = subprocess.check_output([str(path), '--version'], text=True)
        if f'git: {getattr(args, name + "_git")}' not in version.splitlines():
            raise SystemExit('unexpected binary revision: ' + name)
        binaries[name] = {'path': str(path), 'sha256': digest(path), 'version': version}
    paths = sorted(args.root.rglob('*.smt2'))[args.shard::args.shards]
    if not paths:
        raise SystemExit('empty shard')
    identities = {str(p.relative_to(args.root)): digest(p) for p in paths}
    provenance = {'binaries': binaries, 'inputs': identities, 'timeout': args.timeout,
                  'shard': args.shard, 'shards': args.shards, 'runner_sha256': digest(Path(__file__)),
                  'job': os.environ.get('SLURM_JOB_ID'), 'profiles': ['baseline-native', 'candidate-native',
                  'baseline-m124', 'candidate-m124']}
    (args.out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    for key in list(os.environ):
        if key.startswith(('PRIMO_PHASE_PROBE', 'PRIMO_REGION_')):
            os.environ.pop(key)
    profiles = provenance['profiles']
    with (args.out / 'rows.jsonl').open('w') as rows:
        for index, path in enumerate(paths):
            offset = (index + args.shard) % len(profiles)
            for profile in profiles[offset:] + profiles[:offset]:
                binary, mode = profile.split('-')
                flags = ['--quiet', '--stats', '--arithmetic-model-phase=off',
                         *(M124 if mode == 'm124' else NATIVE)]
                command = [binaries[binary]['path'], *flags]
                result = run_solver(command, path, args.timeout)
                stem = f'{index:03d}-{profile}'
                for stream in ('stdout', 'stderr'):
                    (args.out / f'{stem}.{stream}').write_text(str(result.pop(stream)))
                expected = parse_smtlib_status(path)
                wrong = result['status'] == 'ok' and result['answer'] in {'sat', 'unsat'} and bool(expected) and result['answer'] != expected
                row = dict(result, file=str(path.relative_to(args.root)), profile=profile,
                           expected=expected, wrong=wrong, command=[*command, str(path)], log_stem=stem)
                rows.write(json.dumps(row) + '\n')
                rows.flush()
                print(json.dumps({k: row[k] for k in ('file', 'profile', 'status', 'answer', 'seconds')}), flush=True)
                if wrong or result['status'] in {'error', 'unsupported'}:
                    raise SystemExit('blocking solver outcome; remaining comparisons stopped')
    if identities != {str(p.relative_to(args.root)): digest(p) for p in paths}:
        raise SystemExit('inputs changed')
    for identity in binaries.values():
        if digest(Path(identity['path'])) != identity['sha256']:
            raise SystemExit('binary changed')
    (args.out / 'complete').write_text('all rows recorded; SAT model replay is separate\n')


if __name__ == '__main__':
    main()
