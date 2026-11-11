#!/usr/bin/env python3
"""Replay one official-selection input across five profiles, then validate SAT separately."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import resource
import socket
import subprocess
import sys
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'scripts/bench_qf_uf_full.py').is_file())
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402
from scripts.qualification.long_limits.campaign import (  # noqa: E402
    decisive,
    memory_exhausted,
)
from scripts.qualification.uniform120 import resource_triage as resources  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: object) -> None:
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def validate_result(row: dict, expected: str) -> None:
    """Reject contradictory/abnormal answers; known memory limits stay unsolved."""
    if decisive(row) and expected in ('sat', 'unsat') and row['answer'] != expected:
        raise ValueError('contradictory successful answer')
    if row['status'] not in ('ok', 'timeout') and not memory_exhausted(row) and not resources.candidate(row):
        raise ValueError('unclassified native execution failure')
    if row['status'] == 'ok' and (str(row['exit_code']) != '0' or str(row['timeout']).lower() != 'false'):
        raise ValueError('successful status with failed process')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('timing', 'resources', 'models'))
    for name in ('manifest', 'root', 'solver', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--slot', type=int, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    case = manifest['cases'][args.slot]
    source = args.root / case['file']
    solver = args.solver.resolve()
    assert sha(source) == case['sha256']
    assert sha(solver) == manifest['native_sha256']
    directory = args.out / f'slot-{args.slot}'
    profile_names = manifest['profiles']
    config = {'memory_mib': manifest['native_address_space_bytes'] // 1024**2}
    plan = {'tools': {'primo': {'path': str(solver), 'sha256': sha(solver)}}, 'config': config}
    inventory = {'root': str(args.root), 'inputs': {case['file']: {'sha256': case['sha256']}}}
    if args.phase == 'timing':
        directory.mkdir(parents=True, exist_ok=False)
        version = subprocess.check_output([str(solver), '--version'], text=True)
        assert 'git: ' + manifest['source_git'][:8] in version.splitlines()
        order = profile_names[args.slot % len(profile_names):] + profile_names[:args.slot % len(profile_names)]
        provenance = {'case': case, 'profiles': profile_names, 'order': order, 'binary_sha256': sha(solver),
                      'version': version, 'manifest_sha256': sha(args.manifest), 'runner_sha256': sha(Path(__file__)),
                      'job': os.environ['SLURM_JOB_ID'], 'host': socket.gethostname(),
                      'affinity': sorted(os.sched_getaffinity(0)), 'SLURM_CPUS_PER_TASK': os.environ['SLURM_CPUS_PER_TASK'],
                      'cpu_models': sorted({line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')})}
        write(directory / 'provenance.json', provenance)
        resource.setrlimit(resource.RLIMIT_AS, (manifest['native_address_space_bytes'],) * 2)
        expected = case['expected']
        declared = parse_smtlib_status(source)
        assert expected not in ('sat', 'unsat') or declared not in ('sat', 'unsat') or expected == declared
        if expected == 'unknown':
            expected = declared
        with (directory / 'rows.jsonl').open('w') as stream:
            for profile in order:
                argv = manifest['profile_args'][profile]
                result = run_solver([str(solver), *argv], source, manifest['timeout_seconds'])
                for output in ('stdout', 'stderr'):
                    (directory / f'{profile}.{output}.txt').write_text(str(result.pop(output)))
                row = dict(result, file=case['file'], profile=profile, argv=argv, expected=expected,
                           stdout_log=str(directory / f'{profile}.stdout.txt'),
                           stderr_log=str(directory / f'{profile}.stderr.txt'))
                row['wrong'] = decisive(row) and expected in ('sat', 'unsat') and row['answer'] != expected
                stream.write(json.dumps(row) + '\n')
                stream.flush()
                os.fsync(stream.fileno())
                print(profile, result['status'], result['answer'], result['seconds'], flush=True)
                validate_result(row, expected)
        assert sha(source) == case['sha256'] and sha(solver) == manifest['native_sha256']
        write(directory / 'timing-complete.json', {'rows': len(order)})
        return 0
    provenance = json.loads((directory / 'provenance.json').read_text())
    assert provenance['binary_sha256'] == sha(solver) and provenance['manifest_sha256'] == sha(args.manifest)
    rows = [json.loads(line) for line in (directory / 'rows.jsonl').read_text().splitlines()]
    assert len(rows) == len(profile_names) and (directory / 'timing-complete.json').is_file()
    if args.phase == 'resources':
        # Globally scheduled after every timing task, preserving original timing rows.
        resource.setrlimit(resource.RLIMIT_AS, (manifest['native_address_space_bytes'],) * 2)
        classified = []
        for row in rows:
            task = {'logic': 'QF_UFLIA', 'label': row['profile'], 'arguments': row['argv']}
            if resources.candidate(row):
                resources.diagnose(row, directory, task, plan, inventory)
            checked = resources.annotate([row], directory, task, plan, inventory)[0]
            if checked['status'] == 'error' and not (memory_exhausted(checked) or checked.get('_confirmed_oom')):
                raise ValueError('unexplained error after resource classification')
            classified.append({'profile': row['profile'], 'memory_out': memory_exhausted(checked) or bool(checked.get('_confirmed_oom')),
                               'confirmed_cleanup_memory_out': bool(checked.get('_confirmed_oom'))})
        write(directory / 'resources.json', classified)
        return 0
    assert (directory / 'resources.json').is_file()
    selected = [row for row in rows if decisive(row) and row['answer'] == 'sat']
    coverage = {'status': 'running', 'selected_sat': len(selected), 'direct_dolmen_pass': 0, 'rows': []}
    write(directory / 'coverage.json', coverage)
    for row in selected:
        model_out = directory / 'models' / row['profile']
        command = [sys.executable, 'scripts/validate_model.py', '--solver', str(solver),
                   *(f'--solver-arg={arg}' for arg in row['argv']), '--solver-arg=--check-model',
                   '--expect-sat', '--require-dolmen-pass', '--dolmen-arg=--strict=false',
                   '--timeout', '180', '--dolmen-timeout', '60', '--out', str(model_out), str(source)]
        process = subprocess.run(command, check=False)
        with (model_out / 'summary.csv').open(newline='') as summary:
            model = next(csv.DictReader(summary))
        passed = process.returncode == 0 and model['outcome'] == 'pass' and model['validator'] == 'dolmen' and model['solver_exit'] == model['dolmen_exit'] == '0'
        coverage['rows'].append({'profile': row['profile'], 'file': case['file'], 'outcome': model['outcome'],
                                 'validator': model['validator'], 'summary_sha256': sha(model_out / 'summary.csv')})
        coverage['direct_dolmen_pass'] += int(passed)
        coverage['status'] = 'running' if passed else 'failed-or-incomplete'
        write(directory / 'coverage.json', coverage)
        if not passed:
            return 1
    coverage['status'] = 'complete'
    write(directory / 'coverage.json', coverage)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
