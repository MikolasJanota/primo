"""Verify masked allocation failures separately from immutable solver timings."""
from __future__ import annotations

import hashlib
import json
import os
import signal
import subprocess
import time
from pathlib import Path

PREFIX = 'PRIMO_RESOURCE_TRACE '
DIAGNOSTIC = "invalid API usage of 'void CaDiCaL::Solver::disconnect_external_propagator()'"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def candidate(row: dict) -> bool:
    return (row['status'] == 'error' and str(row['exit_code']) == '-6'
            and DIAGNOSTIC in row.get('stderr_head', '')
            and 'solver in invalid state' in row.get('stderr_head', ''))


def identity(row: dict, task: dict, plan: dict, inventory: dict) -> dict:
    return {'logic': task['logic'], 'profile': task['label'], 'arguments': task['arguments'],
            'file': row['file'], 'input_sha256': inventory['inputs'][row['file']]['sha256'],
            'binary_sha256': plan['tools']['primo']['sha256'],
            'row_sha256': hashlib.sha256(json.dumps(row, sort_keys=True).encode()).hexdigest(),
            'address_space_bytes': plan['config']['memory_mib'] * 1024**2}


def key(value: dict) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def traces(stdout: str, memory: int) -> list[dict]:
    records = [json.loads(line[len(PREFIX):]) for line in stdout.splitlines() if line.startswith(PREFIX)]
    if (len(records) != 2 or any(not isinstance(r, dict) for r in records)
            or [r.get('stage') for r in records] != ['throw', 'abort']):
        raise ValueError('incomplete allocation/cleanup trace')
    throwing, aborting = records
    if (any(not isinstance(r.get('frames'), list) or any(not isinstance(n, str) for n in r['frames']) for r in records)
            or not isinstance(aborting.get('state'), str)):
        raise ValueError('malformed allocation/cleanup trace')
    if (throwing.get('address_space_bytes') != [memory, memory]
            or not any(name.startswith(('operator new(', 'operator new[](')) for name in throwing['frames'])
            or not any('__cxa_throw' in name for name in throwing['frames'])
            or 'SIGABRT' not in aborting['state']
            or not any('primo::sat::CadicalBackend::disconnect_external_propagator' in name for name in aborting['frames'])
            or not any('primo::smt::AssignmentConnection' in name for name in aborting['frames'])):
        raise ValueError('replay does not establish allocation failure followed by cleanup abort')
    return records


def annotate(rows: list[dict], directory: Path, task: dict, plan: dict, inventory: dict) -> list[dict]:
    result = []
    for row in rows:
        annotated = dict(row)
        annotated.pop('_confirmed_oom', None)
        if candidate(row):
            expected = identity(row, task, plan, inventory)
            root = directory / 'resource-triage' / key(expected)
            proof_path = root / 'proof.json'
            if proof_path.exists():
                proof = json.loads(proof_path.read_text())
                if (proof.get('identity') != expected or proof.get('status') != 'confirmed-memory-out'
                        or set(proof.get('artifacts', {})) != {'stdout.txt', 'stderr.txt'}
                        or any(digest(root / name) != checksum for name, checksum in proof['artifacts'].items())):
                    raise ValueError('resource certificate identity/artifacts changed')
                traces((root / 'stdout.txt').read_text(), expected['address_space_bytes'])
                annotated['_confirmed_oom'] = True
        result.append(annotated)
    return result


def diagnose(row: dict, directory: Path, task: dict, plan: dict, inventory: dict) -> None:
    expected = identity(row, task, plan, inventory)
    binary = Path(plan['tools']['primo']['path'])
    source = Path(inventory['root']) / row['file']
    if digest(binary) != expected['binary_sha256'] or digest(source) != expected['input_sha256']:
        raise ValueError('diagnostic input or binary changed')
    root = directory / 'resource-triage' / key(expected)
    root.mkdir(parents=True, exist_ok=False)
    commands = Path(__file__).with_name('resource.gdb')
    command = ['gdb', '-iex', 'set debuginfod enabled off', '-batch', '-x', str(commands),
               '--args', str(binary), *task['arguments'], str(source)]
    started = time.monotonic()
    with (root / 'stdout.txt').open('w') as stdout, (root / 'stderr.txt').open('w') as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True,
                                   env={**os.environ, 'DEBUGINFOD_URLS': ''})
        try:
            code = process.wait(timeout=240)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise ValueError('allocation diagnostic timed out; crash remains unexplained') from None
    # GDB's exit status alone says nothing about its inferior's success.
    if code != 0:
        raise ValueError('debugger failed; crash remains unexplained')
    records = traces((root / 'stdout.txt').read_text(), expected['address_space_bytes'])
    proof = {'status': 'confirmed-memory-out', 'identity': expected, 'traces': records,
             'command': command, 'diagnostic_seconds': time.monotonic() - started,
             'commands_sha256': digest(commands), 'triage_sha256': digest(Path(__file__)),
             'artifacts': {name: digest(root / name) for name in ['stdout.txt', 'stderr.txt']}}
    (root / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
