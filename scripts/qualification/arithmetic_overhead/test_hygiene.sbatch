#!/usr/bin/env bash
# Recheck a test-only capacity fix without replacing measured solver binaries.
#SBATCH --job-name=primo-arithmetic-overhead-test-hygiene
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
export LD_LIBRARY_PATH="${PRIMO_ASAN_RUNTIME_LIBDIR:?}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ASAN_OPTIONS=detect_leaks=1:strict_init_order=1
python3 - <<'PY'
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

source = Path(os.environ['PRIMO_HYGIENE_SOURCE']).resolve()
out = Path(os.environ['PRIMO_HYGIENE_OUT'])
out.mkdir(exist_ok=False, parents=True)
identity = hashlib.sha256(Path('build-slurm-rel/primo').read_bytes()).hexdigest()
commands = []
for build in ('build-slurm-rel', 'build-asan'):
    database = json.loads(Path(build, 'compile_commands.json').read_text())
    entry, = (row for row in database if row['file'].endswith('/tests/unit/preprocessing_tests.cpp'))
    original = shlex.split(entry['command'])
    flags = []
    index = 1
    while index < len(original):
        if original[index] in ('-o', '-c'):
            index += 2
        else:
            flags.append(original[index])
            index += 1
    executable = (out / build).resolve()
    command = [original[0], *flags, str(source), '-Wl,--start-group',
               *map(str, sorted(Path(build).glob('libprimo_*.a'))),
               'contrib/local/lib/libcadical.a', 'contrib/local/lib/libgmpxx.a',
               'contrib/local/lib/libgmp.a', '-Wl,--end-group', '-pthread', '-ldl',
               '-o', str(executable)]
    if build == 'build-asan':
        runtime = os.environ['PRIMO_ASAN_RUNTIME_LIBDIR']
        command += ['-L' + runtime, '-Wl,-rpath,' + runtime]
    commands.append(command)
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
    if build == 'build-slurm-rel':
        with (out / 'clang-tidy.log').open('w') as stream:
            subprocess.run(['clang-tidy', str(source), '-checks=clang-analyzer-*,misc-include-cleaner',
                            '--', *flags], check=True, stdout=stream, stderr=subprocess.STDOUT)
assert hashlib.sha256(Path('build-slurm-rel/primo').read_bytes()).hexdigest() == identity
metadata = {'test_source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'measured_binary_sha256': identity, 'measured_binary_unchanged': True,
            'job': os.environ['SLURM_JOB_ID'], 'commands': commands,
            'checks': ['Release preprocessing regression suite', 'ASan preprocessing regression suite',
                       'focused clang-analyzer/misc-include-cleaner']}
(out / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
