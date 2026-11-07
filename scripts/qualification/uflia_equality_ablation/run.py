#!/usr/bin/env python3
"""Run rotated equality-policy arms with immutable binary/input provenance."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / 'scripts/bench_qf_uf_full.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402
from scripts.qualification.uflia_equalities.run import NATIVE  # noqa: E402


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'root', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--baseline-git', required=True)
    parser.add_argument('--candidate-git', required=True)
    parser.add_argument('--profiles', nargs='+', default=['old-off', 'off', 'scan', 'names', 'fixed', 'all'],
                        choices=['old-off', 'old-all', 'off', 'scan', 'names', 'fixed', 'all'])
    parser.add_argument('--shards', type=int, required=True)
    parser.add_argument('--shard', type=int, required=True)
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    if (args.out.exists() or args.timeout <= 0 or not 0 <= args.shard < args.shards
            or len(set(args.profiles)) != len(args.profiles)):
        parser.error('fresh output, positive timeout, valid shard and unique profiles required')
    args.root = args.root.resolve()
    if args.manifest:
        names = [line.split('#', 1)[0].strip() for line in args.manifest.read_text().splitlines()]
        all_paths = [(args.root / name).resolve() for name in names if name]
        for path in all_paths:
            path.relative_to(args.root)
    else:
        all_paths = sorted(args.root.rglob('*.smt2'))
    if len(set(all_paths)) != len(all_paths):
        parser.error('duplicate inputs')
    paths = all_paths[args.shard::args.shards]
    if not paths:
        parser.error('empty shard')
    args.out.mkdir(parents=True)
    binaries = {}
    for name in ('baseline', 'candidate'):
        path = getattr(args, name).resolve()
        version = subprocess.check_output([str(path), '--version'], text=True)
        if f'git: {getattr(args, name + "_git")}' not in version.splitlines():
            raise SystemExit('unexpected binary revision: ' + name)
        binaries[name] = {'path': str(path), 'sha256': digest(path), 'version': version}
    profile_binaries = {p: 'baseline' if p.startswith('old-') else 'candidate' for p in args.profiles}
    identities = {str(p.relative_to(args.root)): digest(p) for p in paths}
    provenance = {'binaries': binaries, 'inputs': identities, 'timeout': args.timeout,
                  'shard': args.shard, 'shards': args.shards, 'runner_sha256': digest(Path(__file__)),
                  'manifest_sha256': digest(args.manifest) if args.manifest else None,
                  'job': os.environ.get('SLURM_JOB_ID'), 'profiles': args.profiles,
                  'profile_binaries': profile_binaries}
    (args.out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    for key in list(os.environ):
        if key.startswith(('PRIMO_PHASE_PROBE', 'PRIMO_REGION_')):
            os.environ.pop(key)
    with (args.out / 'rows.jsonl').open('w') as stream:
        for index, path in enumerate(paths):
            offset = (index + args.shard) % len(args.profiles)
            for profile in args.profiles[offset:] + args.profiles[:offset]:
                binary = profile_binaries[profile]
                flags = ['--quiet', '--stats', '--arithmetic-model-phase=off', *NATIVE]
                if binary == 'candidate':
                    flags.append('--qf-uflia-tableau-equalities=' + profile)
                elif profile == 'old-all':
                    flags.append('--qf-uflia-tableau-equalities')
                command = [binaries[binary]['path'], *flags]
                result = run_solver(command, path, args.timeout)
                stem = f'{index:03d}-{profile}'
                for channel in ('stdout', 'stderr'):
                    (args.out / f'{stem}.{channel}').write_text(str(result.pop(channel)))
                expected = parse_smtlib_status(path)
                solved = result['status'] == 'ok' and result['exit_code'] == 0 and result['answer'] in {'sat', 'unsat'}
                wrong = solved and expected in {'sat', 'unsat'} and result['answer'] != expected
                row = dict(result, file=str(path.relative_to(args.root)), profile=profile,
                           expected=expected, wrong=wrong, command=[*command, str(path)], log_stem=stem)
                stream.write(json.dumps(row) + '\n')
                stream.flush()
                print(json.dumps({k: row[k] for k in ('file', 'profile', 'status', 'answer', 'seconds')}), flush=True)
                if wrong or result['status'] in {'error', 'unsupported'}:
                    raise SystemExit('blocking solver outcome; stop comparisons')
    if identities != {str(p.relative_to(args.root)): digest(p) for p in paths}:
        raise SystemExit('inputs changed')
    for identity in binaries.values():
        if digest(Path(identity['path'])) != identity['sha256']:
            raise SystemExit('binary changed')
    (args.out / 'complete').write_text('all rows recorded; SAT validation is separate\n')


if __name__ == '__main__':
    main()
