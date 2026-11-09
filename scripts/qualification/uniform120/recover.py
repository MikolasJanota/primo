#!/usr/bin/env python3
"""Certify completed native shards and preserve cancelled attempts for recovery."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path

from campaign import digest, load, recovery, resources, strict_rows, write
from report import legacy_report


def prepare(out: Path, workspace: Path) -> None:
    plan = load(out / 'plan.json')
    prior = recovery(out, plan)
    worker = Path(__file__).with_name('campaign.py')
    if prior and prior['worker_driver_sha256'] == digest(worker):
        raise ValueError('this recovery worker was already prepared')
    jobs = load(out / 'jobs.json')
    owned = set(jobs['timing'] + jobs['models'] + [jobs['timing_audit'], jobs['final_report']])
    active = subprocess.check_output(['squeue', '--me', '--noheader', '--format=%i'], text=True)
    if owned.intersection(line.strip().split('_')[0] for line in active.splitlines()):
        raise ValueError('prior experiment still active')
    inventories = {logic: load(out / 'inventory' / f'{logic}.json') for logic in plan['config']['corpora']}
    auditor = legacy_report(workspace)
    complete, pending, reused, moves = [], [], {}, []
    for index, task in enumerate(plan['tasks']):
        root = out / 'timing' / task['logic'] / task['label'] / f"shard-{task['shard']}"
        if (root / 'provenance.json').exists() and load(root / 'provenance.json')['status'] == 'complete':
            corpus = inventories[task['logic']]
            expected = {name: value['sha256'] for name, value in corpus['inputs'].items()}
            _, _, rows = auditor.timing_shard(out, plan, task, task['shard'], expected)
            annotated = resources.annotate(rows, root, task, plan, corpus)
            strict_rows(annotated, [], False, pending=True)
            if any(resources.candidate(r) and not r.get('_confirmed_oom') for r in annotated):
                pending.append(index)
                moves.append(root)
                continue
            strict_rows(annotated, [], False)
            reused[f"{task['logic']}/{task['label']}/{task['shard']}"] = {
                name: digest(root / name) for name in ['answers.csv', 'provenance.json', 'execution-timing.json']}
            complete.append(index)
        else:
            pending.append(index)
            if root.exists():
                moves.append(root)
    ancestors = dict(prior.get('ancestor_recoveries', {})) if prior else {}
    archive = out / 'attempts' / ('resource-recovery-' + str(len(ancestors) + 1))
    archive.mkdir(parents=True, exist_ok=False)
    shutil.copy2(out / 'jobs.json', archive / 'jobs.json')
    if prior:
        ancestors[prior['worker_driver_sha256']] = digest(out / 'recovery.json')
        shutil.copy2(out / 'recovery.json', archive / 'recovery.json')
    for root in moves:
        destination = archive / root.relative_to(out)
        destination.parent.mkdir(parents=True, exist_ok=True)
        root.rename(destination)
    for name in ['failures', 'task-failures']:
        if (out / name).exists():
            (out / name).rename(archive / name)
    write(out / 'recovery.json', {
        'status': 'ready', 'plan_sha256': digest(out / 'plan.json'),
        'parent_driver_sha256': plan['driver_sha256'], 'worker_driver_sha256': digest(worker),
        'ancestor_recoveries': ancestors, 'completed_task_indices': complete,
        'pending_task_indices': pending, 'reused_shards': reused,
        'resource_workflow_sha256': {name: digest(worker.with_name(name)) for name in
                                     ['resource_triage.py', 'resource.gdb', 'report.py']},
        'archive': str(archive),
        'policy': 'Explicit or independently confirmed memory-outs are unsolved budget outcomes. '
                  'Masked cleanup errors require exact-input/profile/binary allocation and abort traces; '
                  'raw timings remain unchanged. Unexplained errors/wrong answers/disagreements block.'})
    print(f'Certified {len(complete)} native shards; {len(pending)} timing tasks remain.')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, default=Path(os.environ.get('PRIMO_WORKDIR', '.')))
    args = parser.parse_args()
    prepare(args.out, args.workspace)


if __name__ == '__main__':
    main()
