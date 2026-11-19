#!/usr/bin/env bash
# Verify include/help hygiene against the qualified, unchanged solver libraries.
#SBATCH --job-name=primo-euf-smtcomp-test-hygiene
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export LD_LIBRARY_PATH="${PRIMO_ASAN_RUNTIME:?}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ASAN_OPTIONS=detect_leaks=1:strict_init_order=1
python3.11 - <<'PY'
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

source = Path(os.environ['PRIMO_HYGIENE_SOURCE']).resolve()
out = Path(os.environ['PRIMO_HYGIENE_OUT']).resolve()
out.mkdir(parents=True, exist_ok=False)
solver = Path('build-slurm-rel/primo')
identity = hashlib.sha256(solver.read_bytes()).hexdigest()
commands = []
for build in ('build-slurm-rel', 'build-euf-performance-asan-' + os.environ['PRIMO_EXPECTED_GIT']):
    database = json.loads(Path(build, 'compile_commands.json').read_text())
    entry, = (row for row in database if row['file'].endswith('/tests/unit/qf_uflia_solver_tests.cpp'))
    original = shlex.split(entry['command'])
    flags = []
    index = 1
    while index < len(original):
        if original[index] in ('-o', '-c'):
            index += 2
        else:
            flags.append(original[index])
            index += 1
    executable = out / build
    command = [original[0], *flags, str(source), '-Wl,--start-group',
               *map(str, sorted(Path(build).glob('libprimo_*.a'))),
               'contrib/local/lib/libcadical.a', 'contrib/local/lib/libgmpxx.a',
               'contrib/local/lib/libgmp.a', '-Wl,--end-group', '-pthread', '-ldl',
               '-o', str(executable)]
    commands.append(command)
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
    if build == 'build-slurm-rel':
        with (out / 'clang-tidy.log').open('w') as stream:
            subprocess.run([os.environ['PRIMO_VERIFY_BIN'] + '/clang-tidy', str(source),
                            '-checks=clang-analyzer-*,misc-include-cleaner', '--', *flags],
                           check=True, stdout=stream, stderr=subprocess.STDOUT)
help_source = Path(os.environ['PRIMO_HELP_SOURCE']).resolve()
database = json.loads(Path('build-slurm-rel/compile_commands.json').read_text())
entry, = (row for row in database if row['file'].endswith('/src/main.cpp'))
original = shlex.split(entry['command'])
flags = []
index = 1
while index < len(original):
    if original[index] in ('-o', '-c'):
        index += 2
    else:
        flags.append(original[index])
        index += 1
executable = out / 'primo-help'
command = [original[0], *flags, str(help_source), '-Wl,--start-group',
           *map(str, sorted(Path('build-slurm-rel').glob('libprimo_*.a'))),
           'contrib/local/lib/libcadical.a', 'contrib/local/lib/libgmpxx.a',
           'contrib/local/lib/libgmp.a', '-Wl,--end-group', '-pthread', '-ldl',
           '-o', str(executable)]
commands.append(command)
subprocess.run(command, check=True)
help_text = subprocess.check_output([str(executable), '--help'], text=True)
assert 'count unconditional numeric bound opportunities' in help_text
assert 'root-level numeric bound' not in help_text
(out / 'help.txt').write_text(help_text)
assert hashlib.sha256(solver.read_bytes()).hexdigest() == identity
metadata = {'test_source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'qualified_solver_sha256': identity, 'solver_unchanged': True,
            'help_source_sha256': hashlib.sha256(help_source.read_bytes()).hexdigest(),
            'method_sha256': hashlib.sha256(Path(os.environ['PRIMO_HYGIENE_METHOD']).read_bytes()).hexdigest(),
            'job': os.environ['SLURM_JOB_ID'], 'commands': commands,
            'checks': ['Release UFLIA regression suite', 'ASan UFLIA regression suite',
                       'focused clang-analyzer/misc-include-cleaner']}
(out / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
