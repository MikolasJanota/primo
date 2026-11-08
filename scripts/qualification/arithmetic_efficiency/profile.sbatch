#!/usr/bin/env bash
# Focused timings, native stack samples, Callgrind, then separate SAT replay.
# Instrumented prefixes are diagnostic evidence, never benchmark runtimes.
#SBATCH --job-name=primo-arithmetic-review
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=24G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export DEBUGINFOD_URLS=''
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PY'
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path

config = json.loads(Path(os.environ['PRIMO_REVIEW_CONFIG']).read_text())
index = int(os.environ['SLURM_ARRAY_TASK_ID'])
case = config['cases'][index]
source = Path(case['root']) / case['file']
out = Path(os.environ['PRIMO_REVIEW_OUT']) / f'case-{index}'
out.mkdir(parents=True, exist_ok=False)
manifest = out / 'inputs.txt'
manifest.write_text(case['file'] + '\n')
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
assert digest(config['binary']) == config['binary_sha256']
input_hash = digest(source)
if case.get('sha256'):
    assert input_hash == case['sha256']
provenance = {'case': case, 'input_sha256': input_hash,
              'binary_sha256': config['binary_sha256'], 'source_commit': config['source_commit'],
              'review_commit': config['review_commit'], 'job': os.environ['SLURM_ARRAY_JOB_ID'],
              'task': index, 'node': os.uname().nodename,
              'cpu_model': next(x.split(':', 1)[1].strip() for x in Path('/proc/cpuinfo').read_text().splitlines() if x.startswith('model name')),
              'cpus_requested': os.environ['SLURM_CPUS_PER_TASK'],
              'memory_requested_mb': os.environ['SLURM_MEM_PER_NODE'],
              'method_sha256': digest(os.environ['PRIMO_REVIEW_SCRIPT']),
              'sampler_sha256': digest('scripts/profile_gdb_samples.py'),
              'config_sha256': digest(os.environ['PRIMO_REVIEW_CONFIG'])}
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
# callgrind_annotate belongs to Valgrind but has no --version option.
for tool in ('gdb', 'valgrind'):
    version = subprocess.run([tool, '--version'], text=True, capture_output=True, check=True)
    (out / (tool + '-version.txt')).write_text(version.stdout + version.stderr)
profiles = {'default': ['--quiet', '--stats']}
if case['logic'] == 'QF_LIA':
    profiles['cube-m124'] = ['--quiet', '--stats', '--lia-round-integer-bounds',
                            '--lia-primitive-int-rows', '--no-lia-integer-slacks',
                            '--lia-patch-integral-moves', '--lia-cube=uniform', '--lia-cube-steps=1000']
order = [*profiles, 'z3']
order = order[index % len(order):] + order[:index % len(order)]
answers = {}
for label in order:
    command = ['python3', 'scripts/benchmark_model_replay.py', 'benchmark',
               '--root', case['root'], '--manifest', str(manifest),
               '--out', str(out / label / 'normal'), '--timeout', str(config['normal_timeout']),
               '--run-id', f'arithmetic-review-20261108-{index}-{label}',
               '--solver', config['z3'] if label == 'z3' else config['binary']]
    if label != 'z3':
        command += ['--expected-git', config['source_commit']]
        command += ['--solver-arg=' + flag for flag in profiles[label]]
    subprocess.run(command, check=True)
    with (out / label / 'normal/answers.csv').open() as stream:
        row, = csv.DictReader(stream)
    if row['wrong'] == 'true' or row['status'] not in {'ok', 'timeout'}:
        raise SystemExit('blocking solver outcome: ' + label)
    answers[label] = row
known = {r['answer'] for r in answers.values() if r['status'] == 'ok' and r['answer'] in {'sat', 'unsat'}}
assert len(known) <= 1, 'reference disagreement'
for label, flags in profiles.items():
    target = out / label
    command = [config['binary'], *flags, str(source)]
    sample = subprocess.run(['python3', 'scripts/profile_gdb_samples.py', '--out', str(target / 'gdb'),
                             '--timeout', str(config['gdb_seconds']), '--interval', '0.1',
                             '--seed', str(202611080 + index), '--', *command], check=False)
    report = json.loads((target / 'gdb/summary.json').read_text())
    if sample.returncode and report['diagnostic'] != 'profile deadline exceeded':
        raise SystemExit('unexpected native profiler failure: ' + label + ' ' + str(report))
    with (target / 'callgrind.stdout').open('w') as stdout, (target / 'callgrind.stderr').open('w') as stderr:
        run = subprocess.run(['timeout', '--signal=TERM', '--kill-after=5s',
                              str(config['callgrind_seconds']) + 's', 'valgrind', '--tool=callgrind',
                              '--quiet', '--callgrind-out-file=' + str(target / 'callgrind.out'),
                              *command], stdout=stdout, stderr=stderr, check=False)
    (target / 'callgrind.exit-code').write_text(str(run.returncode) + '\n')
    if run.returncode not in {0, 124}:
        raise SystemExit('unexpected Callgrind failure: ' + label)
    for mode in ('self', 'inclusive'):
        with (target / ('callgrind-' + mode + '.txt')).open('w') as stream:
            subprocess.run(['callgrind_annotate', '--inclusive=' + ('yes' if mode == 'inclusive' else 'no'),
                            '--threshold=99', str(target / 'callgrind.out')], stdout=stream, check=True)
for label in profiles:
    if answers[label]['status'] == 'ok' and answers[label]['answer'] == 'sat':
        subprocess.run(['python3', 'scripts/benchmark_model_replay.py', 'replay',
                        '--out', str(out / label / 'normal'), '--model-timeout', '90',
                        '--dolmen-timeout', '30', '--dolmen-arg=--strict=false'], check=True)
assert digest(source) == input_hash and digest(config['binary']) == config['binary_sha256']
(out / 'complete').write_text('normal timings and diagnostic profiles complete; SAT models separately replayed\n')
PY
