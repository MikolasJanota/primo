#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-sweep-feedback
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Audit completed checker-stage failures before exporting resource-feedback files.
set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen validation helpers and this script}"
: "${LRA_SWEEP_RESULTS:?completed eight-shard result root}"
: "${ARITHMETIC_REFERENCE:?Yices SMT-LIB executable}"
: "${PRIMO_SAT_SOLVER:?independent CaDiCaL executable}"
cd "$PRIMO_WORKDIR"
export LRA_FEEDBACK_OUT="$PWD/results/lra-sweep-feedback-$SLURM_JOB_ID"
python3 - <<'PY'
import gzip
import json
import os
import shutil
import sys
import tarfile
from pathlib import Path

workflow = Path(os.environ['ARITHMETIC_WORKFLOW_DIR'])
sys.path.insert(0, str(workflow))
from micromath_stress import digest, invoke, write_json

out = Path(os.environ['LRA_FEEDBACK_OUT'])
out.mkdir(parents=True, exist_ok=False)
results = Path(os.environ['LRA_SWEEP_RESULTS'])
yices = Path(os.environ['ARITHMETIC_REFERENCE'])
cadical = Path(os.environ['PRIMO_SAT_SOLVER'])
package = out / 'feedback'
package.mkdir()
report = {'job_id': os.environ['SLURM_JOB_ID'], 'rows': [],
          'reference_sha256': digest(yices), 'cadical_sha256': digest(cadical),
          'workflow_sha256': {name: digest(workflow / name) for name in (
              'slurm_lra_sweep_feedback.sbatch', 'micromath_stress.py',
              'check_lra_certificate.py', 'certificate_cnf_statement.py')},
          'source_to_cnf_verified': False,
          'limits': {'stage_seconds': 60, 'address_bytes': 3221225472,
                     'file_bytes': 268435456}}

def bounded(command, prefix):
    return invoke(['prlimit', '--as=3221225472', '--fsize=268435456', '--',
                   *map(str, command)], out, prefix, 60)

def checkpoint():
    write_json(out / 'summary.json', report)

cases = []
for shard in range(8):
    summary = results / f'shard-{shard}' / 'run/summary.json'
    saved = json.loads(summary.read_text())
    if not saved.get('finished') or saved['completed'] != saved['requested']:
        raise ValueError('sweep is incomplete; no partial feedback export')
    if saved['logic'] != 'QF_LRA' or saved['statement_source'] != 'exported-cnf':
        raise ValueError('wrong certificate boundary')
    if saved['binary_sha256']['reference'] != digest(yices):
        raise ValueError('reference binary changed')
    if saved['binary_sha256']['cadical'] != digest(cadical):
        raise ValueError('SAT binary changed')
    for index, row in enumerate(saved['rows']):
        if row['status'] in ('proof_error', 'proof_timeout', 'challenge_error', 'challenge_timeout', 'proof_killed', 'proof_file_limit'):
            cases.append((shard, index, summary.parent / f'{index:03}', row))
report['checker_cases'] = len(cases)
checkpoint()
for shard, index, source, previous in cases:
    name = f'shard-{shard}-case-{index:03}'
    target = out / name
    target.mkdir()
    row = {'name': name, 'file': previous['case']['file'], 'previous_status': previous['status'],
           'status': 'validation_incomplete', 'stages': {}, 'files': {}}
    expected = {'input.smt2': previous['case']['sha256'],
                'native.sexp': previous['native_certificate_sha256'],
                'exported-cnf.smt2': previous['statement']['sha256']}
    for filename, sha in expected.items():
        shutil.copyfile(source / filename, target / filename)
        if digest(target / filename) != sha:
            raise ValueError('feedback payload changed')
        row['files'][filename] = {'sha256': sha, 'bytes': (target / filename).stat().st_size}
    for label, filename in (('original_yices', 'input.smt2'), ('cnf_yices', 'exported-cnf.smt2')):
        stage = bounded([yices, target / filename], target / label)
        row['stages'][label] = stage
    regenerated = target / 'regenerated.smt2'
    stage = bounded([sys.executable, workflow / 'certificate_cnf_statement.py',
                     target / 'native.sexp', regenerated, '--logic', 'QF_LRA'], target / 'regeneration')
    row['stages']['regeneration'] = stage
    row['statement_byte_match'] = stage['state'] == 'ok' and digest(regenerated) == expected['exported-cnf.smt2']
    if row['statement_byte_match']:
        regenerated.unlink()
    stage = bounded([sys.executable, workflow / 'check_lra_certificate.py', target / 'native.sexp',
                     '--sat-solver', cadical], target / 'independent')
    row['stages']['independent'] = stage
    if stage['state'] == 'ok':
        row['independent_report'] = json.loads(Path(stage['stdout']).read_text())
    yices_ok = all(row['stages'][label]['state'] == 'ok' and
                   Path(row['stages'][label]['stdout']).read_text().splitlines() == ['unsat']
                   for label in ('original_yices', 'cnf_yices'))
    disagreement = any(row['stages'][label]['state'] == 'ok' and
                       Path(row['stages'][label]['stdout']).read_text().splitlines() == ['sat']
                       for label in ('original_yices', 'cnf_yices'))
    if disagreement:
        row['status'] = 'reference_disagreement'
        report['rows'].append(row)
        checkpoint()
        raise SystemExit(2)
    if yices_ok and row['statement_byte_match'] and stage['state'] == 'ok' and row['independent_report'].get('propositional_unsat') is True:
        row['status'] = 'independent_checks_passed'
        destination = package / name
        shutil.copytree(target, destination)
        old_logs = destination / 'original-logs'
        old_logs.mkdir()
        for pattern in ('*.out', '*.err', 'pipe/*.out', 'pipe/*.err', 'pipe/exit-status.txt'):
            for path in source.glob(pattern):
                relative = path.relative_to(source)
                copied = old_logs / relative
                copied.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, copied)
        write_json(destination / 'case.json', row)
    report['rows'].append(row)
    checkpoint()
valid = [row for row in report['rows'] if row['status'] == 'independent_checks_passed']
if valid:
    checker = json.loads((results / 'shard-0/run/summary.json').read_text())['micromath_git']
    guide = f'''# QF_LRA larger-sweep checker feedback

These {len(valid)} original failed cases passed fresh Yices UNSAT checks on both
original and exported-CNF statements, exact rational witness/SAT checking, saved
payload hashes and regenerated-statement byte equality. Audit SLURM {report['job_id']}.
No producer-side defect was found by these checks. Preprocessing and checker
soundness are not formally established by the audit.

The checker observations are historical results from array 362761, pinned
Micromath {checker}. Their checker stages were not repeated during this audit.
Timeouts are workload-and-budget feedback, not correctness bugs. No checker
implementation was inspected/changed and no upstream fetch occurred.
These sources and native certificates are unchanged, not delta-minimized.
No raw proofs, checker binaries or challenge dumps are bundled.

From an allocated compute node and the pinned Micromath root, with CASE set
to an absolute extracted case directory and CADICAL to an LRAT-capable build:

    set -o pipefail
    ./build-axioms --fragment finitary axioms.umd
    ./smt/build-lra-challenge axioms.umd "$CASE/exported-cnf.smt2" challenge.umd
    ./smt/prove-lra challenge.umd "$CASE/native.sexp" - --raw --cadical "$CADICAL" |
      ./check-solution challenge.umd -

Original limits: 30 seconds for challenge building, one overall 60-second
proof/kernel deadline, 3 GiB address space and 256 MiB file limit per child.
The source being checked is exported-cnf.smt2, not the original input.smt2.
The original source is retained for provenance and independent reference checks.
Only both exits zero and exact challenge proven count as a successful proof.
See case.json and original-logs for the actual observed stage and diagnostics.
'''
    (package / 'README.md').write_text(guide)
    files = {str(path.relative_to(package)): {'sha256': digest(path), 'bytes': path.stat().st_size}
             for path in sorted(package.rglob('*')) if path.is_file()}
    write_json(package / 'manifest.json', {'files': files, 'cases': valid, 'checker_git': checker})
    archive = out / 'lra-sweep-feedback.tar.gz'
    with archive.open('wb') as output, gzip.GzipFile(fileobj=output, mode='wb', mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w') as packed:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                info = packed.gettarinfo(str(path), str(path.relative_to(package)))
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mtime = 0
                with path.open('rb') as stream:
                    packed.addfile(info, stream)
    report['archive'] = {'file': archive.name, 'sha256': digest(archive), 'bytes': archive.stat().st_size}
report['finished'] = True
checkpoint()
PY
