#!/usr/bin/env python3
"""Run a fresh, matched full-corpus comparison with explicit resource limits."""
from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

PROJECT = Path(os.environ.get('PRIMO_WORKDIR', str(Path(__file__).resolve().parents[3])))
BASE = PROJECT / 'scripts/qualification/long_limits/campaign.py'
SPEC = importlib.util.spec_from_file_location('long_limits_worker', BASE)
legacy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(legacy)
# Shared provenance/audit primitives; old frozen campaigns remain unmodified.
digest, write, load = legacy.digest, legacy.write, legacy.load
recovery, memory_exhausted, decisive = legacy.recovery, legacy.memory_exhausted, legacy.decisive
check_rows = legacy.check_rows


def allocation(config: dict) -> dict:
    value = legacy.allocation(config)
    value['driver_sha256'] = digest(Path(__file__))
    value['common_worker_sha256'] = digest(BASE)
    return value


def strict_rows(rows: list[dict], peers: list[dict], reference: bool) -> None:
    check_rows(rows, peers, reference=reference)
    for row in rows:
        # Explicit allocation exhaustion is a resource outcome; signal crashes
        # and other execution errors stop the campaign, including reference ones.
        if (row['status'] == 'error' and not memory_exhausted(row)) or (
            row['status'] == 'unsupported' and not reference
        ):
            raise legacy.CorrectnessFailure(f'execution failure: {row}')


def prepare(args, config: dict, out: Path) -> None:
    metadata = allocation(config)
    binaries = out / 'bin'
    binaries.mkdir()
    tools = load(args.references / 'tools.json')
    parent = load(args.references / 'plan.json')
    if digest(args.references / 'plan.json') != config['reference_parent_plan_sha256']:
        raise ValueError('unexpected reference parent plan')
    if parent['config']['memory_mib'] != config['memory_mib'] or parent['config']['timeout_seconds'] != 1200:
        raise ValueError('reference resource limits differ')
    for name in ('primo', 'dolmen'):
        source = args.workspace / ('build-slurm-rel/primo' if name == 'primo' else 'contrib/local/bin/dolmen')
        target = binaries / name
        shutil.copy2(source.resolve(), target)
        version = subprocess.run([str(target), '--version'], capture_output=True, text=True, timeout=15, check=True)
        tools[name] = {'path': str(target), 'sha256': digest(target), 'version': version.stdout + version.stderr}
    if 'git: ' + config['native_source_git'] not in tools['primo']['version'].splitlines():
        raise ValueError('unexpected native revision')
    write(out / 'tools.json', tools)
    # Read the original admission/results; no reference executable is run here.
    original = load(args.references / 'capabilities.json')
    write(out / 'capabilities.json', {**original, 'reference_parent_plan_sha256': digest(args.references / 'plan.json'),
                                     'reuse_only': True, 'native_preparation': metadata})


def plan(args, config: dict, out: Path) -> None:
    capabilities = load(out / 'capabilities.json')['logics']
    tasks, runs = [], []
    for logic, count in config['corpora'].items():
        corpus_path = out / 'inventory' / f'{logic}.json'
        corpus = load(corpus_path)
        if corpus['count'] != count:
            raise ValueError('corpus count mismatch')
        labels = ['primo', *config['extra_native_profiles'].get(logic, [])]
        labels += [name for name in config['references'] if capabilities[logic][name]['supported']]
        shards = math.ceil(count / config['maximum_inputs_per_shard'])
        names = list(corpus['inputs'])
        for label in labels:
            run = {'logic': logic, 'label': label, 'solver': 'primo' if label.startswith('primo') else label,
                   'arguments': config['native_profiles'].get(label, []), 'shards': shards, 'inputs': count}
            runs.append(run)
            for shard in range(shards):
                manifest = out / 'manifests' / logic / f'shard-{shard}.txt'
                manifest.parent.mkdir(parents=True, exist_ok=True)
                manifest.write_text(''.join(name + '\n' for name in names[shard::shards]))
                if run['solver'] == 'primo':
                    tasks.append({**run, 'shard': shard, 'manifest': str(manifest), 'manifest_sha256': digest(manifest),
                                  'inventory_sha256': digest(corpus_path)})
    tasks.sort(key=lambda t: (t['shard'], t['logic'], t['label']))
    write(out / 'plan.json', {'config': config, 'tools': load(out / 'tools.json'), 'runs': runs, 'tasks': tasks,
                             'model_tasks': [t for t in tasks if t['solver'] == 'primo'],
                             'driver_sha256': digest(Path(__file__)), 'common_worker_sha256': digest(BASE),
                             'config_sha256': digest(args.config), 'campaign_out': str(out)})
    import_references(args, config, out, runs)
    value = load(out / 'plan.json')
    value['reference_reuse_sha256'] = digest(out / 'reference-reuse.json')
    plotter = Path(__file__).with_name('plot_cactus.py')
    if not plotter.exists():
        plotter = PROJECT / 'scripts/plot_cactus.py'
    value['plotter_sha256'] = digest(plotter)
    write(out / 'plan.json', value)
    print(json.dumps({'tasks': len(tasks), 'runs': len(runs), 'reference_benchmark_executions': 0}))


def import_references(args, config: dict, out: Path, runs: list[dict]) -> None:
    parent_path = args.references / 'plan.json'
    parent = load(parent_path)
    if digest(parent_path) != config['reference_parent_plan_sha256']:
        raise ValueError('reference parent plan changed')
    certificates = {}
    for run in runs:
        if run['solver'] == 'primo':
            continue
        logic, label = run['logic'], run['label']
        corpus = load(out / 'inventory' / f'{logic}.json')
        expected = {name: value['sha256'] for name, value in corpus['inputs'].items()}
        old_run, = (r for r in parent['runs'] if r['logic'] == logic and r['label'] == label)
        merged, hashes, source_files, resources = {}, {}, {}, []
        for shard in range(old_run['shards']):
            root = args.references / 'timing' / logic / label / f'shard-{shard}'
            proof = load(root / 'provenance.json')
            execution = load(root / 'execution-timing.json')
            if (proof['status'] != 'complete' or not proof['inputs_unchanged'] or proof['timeout'] != 1200
                    or proof['solver']['sha256'] != parent['tools'][label]['sha256']
                    or proof['solver_args'] != old_run['arguments']
                    or execution['memory_mib'] != config['memory_mib']
                    or execution['address_space_limit_bytes'] != [config['memory_mib'] * 1024**2] * 2
                    or execution['partition'] != 'compute'
                    or execution['cpu_models'] != ['Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz']):
                raise ValueError('invalid archived reference provenance')
            resources.append({k: execution[k] for k in ['job', 'array_job', 'task', 'host', 'cpu_models', 'memory_mib', 'affinity']})
            for filename in ['answers.csv', 'provenance.json', 'execution-timing.json']:
                source_files[f'{shard}/{filename}'] = digest(root / filename)
            for name, checksum in proof['inputs'].items():
                if name in hashes:
                    raise ValueError('duplicate archived input')
                hashes[name] = checksum
            with (root / 'answers.csv').open() as stream:
                old_rows = list(csv.DictReader(stream))
            if len(old_rows) != len(proof['inputs']) or {r['file'] for r in old_rows} != set(proof['inputs']):
                raise ValueError('incomplete archived reference shard')
            for row in old_rows:
                if row['wrong'] != 'false' or row['file'] in merged:
                    raise ValueError('archived wrong answer or duplicate')
                derived = dict(row)
                derived['original_seconds'] = row['seconds']
                derived['original_status'] = row['status']
                if decisive(row) and float(row['seconds']) > config['timeout_seconds']:
                    derived.update(status='timeout', answer='unknown', timeout='true')
                merged[row['file']] = derived
        if hashes != expected or set(merged) != set(expected):
            raise ValueError(f'archived/current input-byte mismatch: {logic}/{label}')
        derived_hashes = {}
        names = list(expected)
        for shard in range(run['shards']):
            root = out / 'timing' / logic / label / f'shard-{shard}'
            root.mkdir(parents=True)
            selected = [merged[name] for name in names[shard::run['shards']]]
            with (root / 'answers.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(selected[0]))
                writer.writeheader()
                writer.writerows(selected)
            (root / 'answers.csv.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in selected))
            derived_hashes[str(shard)] = digest(root / 'answers.csv')
        certificates[f'{logic}/{label}'] = {'kind': 'archived-reference-reuse', 'parent_plan_sha256': digest(parent_path),
                                           'source_files': source_files, 'derived_csv_sha256': derived_hashes,
                                           'input_hashes_match': True, 'original_timeout': 1200,
                                           'display_timeout': config['timeout_seconds'], 'resources': resources,
                                           'reference_benchmark_executions': 0}
    write(out / 'reference-reuse.json', certificates)


def guarded(command: list[str], directory: Path, selected: dict, plan_value: dict, out: Path, index: int) -> None:
    process = subprocess.Popen(command, start_new_session=True)
    try:
        while True:
            peers = []
            for run in plan_value['runs']:
                if run['logic'] == selected['logic'] and run['label'] != selected['label']:
                    peers += legacy.journal(out / 'timing' / run['logic'] / run['label'] / f"shard-{selected['shard']}" / 'answers.csv.jsonl', live=True)
            strict_rows(legacy.journal(directory / 'answers.csv.jsonl', live=True), peers, selected['solver'] != 'primo')
            if process.poll() is not None:
                break
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if process.returncode:
            raise RuntimeError(f'benchmark harness failed: {process.returncode}')
        with (directory / 'answers.csv').open() as stream:
            strict_rows(list(csv.DictReader(stream)), peers, selected['solver'] != 'primo')
    except BaseException as error:
        folder = 'failures' if isinstance(error, legacy.CorrectnessFailure) else 'task-failures'
        write(out / folder / f'timing-{index}.json', {'error': str(error), 'selected': selected})
        for sig in [signal.SIGTERM, signal.SIGKILL]:
            try:
                os.killpg(process.pid, sig)
            except ProcessLookupError:
                pass
            if sig == signal.SIGTERM:
                time.sleep(1)
        process.wait()
        if isinstance(error, legacy.CorrectnessFailure):
            legacy.cancel_owned(out)
        raise


def task(args, config: dict, out: Path, phase: str) -> None:
    value = load(out / 'plan.json')
    if digest(Path(__file__)) != value['driver_sha256'] or digest(BASE) != value['common_worker_sha256'] or digest(args.config) != value['config_sha256']:
        raise ValueError('workflow changed after planning')
    index = int(os.environ.get('PRIMO_TASK_OFFSET', '0')) + int(os.environ['SLURM_ARRAY_TASK_ID'])
    selected = value['model_tasks' if phase == 'models' else 'tasks'][index]
    if selected['solver'] != 'primo':
        raise ValueError('reference benchmark execution is forbidden')
    corpus_path = out / 'inventory' / f"{selected['logic']}.json"
    corpus = load(corpus_path)
    tool = value['tools'][selected['solver']]
    if digest(corpus_path) != selected['inventory_sha256'] or digest(Path(tool['path'])) != tool['sha256']:
        raise ValueError('input/tool identity changed')
    directory = out / 'timing' / selected['logic'] / selected['label'] / f"shard-{selected['shard']}"
    directory.mkdir(parents=True, exist_ok=True)
    record = {**allocation(config), 'task_index': index, 'selected': selected, 'tool': tool,
              'plan_sha256': digest(out / 'plan.json')}
    if phase == 'models':
        barrier = out / 'timing-audit.json'
        if load(barrier)['status'] != 'timing-complete':
            raise ValueError('global timing barrier incomplete')
        record['parent_timing_audit_sha256'] = digest(barrier)
    write(directory / f'execution-{phase}.json', record)
    command = [sys.executable, str(args.workspace / 'scripts/benchmark_model_replay.py'),
               'benchmark' if phase == 'timing' else 'replay', '--out', str(directory)]
    if phase == 'timing':
        if digest(Path(selected['manifest'])) != selected['manifest_sha256']:
            raise ValueError('manifest changed')
        command += ['--root', corpus['root'], '--manifest', selected['manifest'], '--solver', tool['path'],
                    '--run-id', f"uniform120-{selected['logic']}-{selected['label']}-{selected['shard']}",
                    '--shards', '1', '--shard-index', '0', '--timeout', str(config['timeout_seconds'])]
        command += [f'--solver-arg={arg}' for arg in selected['arguments']]
        if selected['solver'] == 'primo':
            command += ['--expected-git', config['native_source_git']]
        guarded(command, directory, selected, value, out, index)
    else:
        command += ['--model-timeout', str(config['model_timeout_seconds']), '--dolmen-timeout', str(config['dolmen_timeout_seconds']),
                    '--dolmen-size', '5G', '--dolmen', value['tools']['dolmen']['path'], '--dolmen-arg=--strict=false']
        subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=['inventory', 'prepare', 'plan', 'timing', 'models'])
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--references', type=Path)
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('config.json'))
    args = parser.parse_args()
    config = load(args.config)
    args.out.mkdir(parents=True, exist_ok=True)
    if args.phase == 'inventory':
        legacy.inventory(args, config, args.out)
    elif args.phase == 'prepare':
        prepare(args, config, args.out)
    elif args.phase == 'plan':
        plan(args, config, args.out)
    else:
        task(args, config, args.out, args.phase)


if __name__ == '__main__':
    main()
