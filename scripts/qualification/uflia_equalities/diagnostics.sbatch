#!/usr/bin/env bash
# Target selection comes from a completed full-corpus comparison.
#SBATCH --job-name=primo-uflia-equality-diagnostics
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PYCODE'
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path.cwd()))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver
from scripts.qualification.uflia_equalities.run import M124, NATIVE

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

slot = int(os.environ['SLURM_ARRAY_TASK_ID'])
manifest = Path(os.environ['PRIMO_DIAGNOSTIC_MANIFEST'])
case = json.loads(manifest.read_text())[slot]
path = Path(os.environ['PRIMO_CORPUS_ROOT']) / case['file']
solver = Path(os.environ['PRIMO_DIAGNOSTIC_SOLVER']).resolve()
out = Path(os.environ['PRIMO_EQ_OUT']) / f'case-{slot}'
out.mkdir(parents=True, exist_ok=False)
version = subprocess.check_output([str(solver), '--version'], text=True)
assert 'git: ' + os.environ['PRIMO_EXPECTED_GIT'] in version.splitlines()
assert case['mode'] in {'native', 'm124'}
base = ['--quiet', '--stats', '--arithmetic-model-phase=off',
        *(M124 if case['mode'] == 'm124' else NATIVE)]
variants = [(f'seed-{seed}', ['--cadical-option', f'seed={seed}']) for seed in (0, 1, 2)]
variants += [('phase-on', ['--arithmetic-model-phase=on']),
             ('no-bound-propagation', ['--no-lra-propagation']),
             ('rebuild', ['--no-qf-uflia-online-components']),
             ('fixed-elimination', ['--lra-fixed-elimination'])]
limit = float(os.environ.get('PRIMO_DIAGNOSTIC_TIMEOUT', '120'))
assert limit > 0
identity = {'case': case, 'input_sha256': digest(path), 'binary_sha256': digest(solver),
            'version': version, 'manifest_sha256': digest(manifest), 'timeout': limit,
            'job': os.environ['SLURM_JOB_ID'], 'variants': variants,
            'note': 'Existing-option diagnostics; interaction tests do not isolate one mechanism.'}
(out / 'provenance.json').write_text(json.dumps(identity, indent=2) + '\n')
answers = set()
with (out / 'rows.jsonl').open('w') as stream:
    for index, (variant, extra) in enumerate(variants):
        roles = ['off', 'on'] if (index + slot) % 2 == 0 else ['on', 'off']
        for role in roles:
            common = [flag for flag in base if variant != 'phase-on' or
                      not flag.startswith('--arithmetic-model-phase=')]
            flags = [*common, *extra]
            if role == 'on':
                flags.append('--qf-uflia-tableau-equalities')
            command = [str(solver), *flags]
            row = run_solver(command, path, limit)
            stem = variant + '-' + role
            for name in ('stdout', 'stderr'):
                (out / (stem + '.' + name)).write_text(str(row.pop(name)))
            expected = parse_smtlib_status(path)
            concrete = row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'} and row['exit_code'] == 0
            if concrete:
                answers.add(row['answer'])
            wrong = concrete and ((bool(expected) and row['answer'] != expected) or
                                  (bool(case.get('expected_answer')) and row['answer'] != case['expected_answer']))
            row.update(file=case['file'], mode=case['mode'], variant=variant, role=role,
                       expected=expected, wrong=wrong, command=[*command, str(path)], log_stem=stem)
            stream.write(json.dumps(row) + '\n')
            stream.flush()
            print(json.dumps({k: row[k] for k in ('file', 'variant', 'role', 'status', 'answer', 'seconds')}), flush=True)
            if wrong or len(answers) > 1 or row['status'] in {'error', 'unsupported'}:
                raise SystemExit('blocking diagnostic outcome')
assert digest(path) == identity['input_sha256'] and digest(solver) == identity['binary_sha256']
(out / 'complete').write_text('all diagnostic rows recorded; SAT models require separate replay\n')
PYCODE
