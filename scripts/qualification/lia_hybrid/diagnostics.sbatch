#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-diagnostics
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
export DEBUGINFOD_URLS=''
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path.cwd()
out = Path(os.environ['PRIMO_HYBRID_DIAGNOSTICS_OUT'])
out.mkdir(parents=True, exist_ok=False)
build = root / 'build-slurm-rel'
# Compile the current documentation example in isolation against the qualified
# libraries, preserving the Release flags and strict warnings of that build.
source = Path(os.environ['PRIMO_HYBRID_EXAMPLE']).resolve()
database = json.loads((build / 'compile_commands.json').read_text())
entry = next(row for row in database if row['file'].endswith('/src/smt/solver_engines.cpp'))
compile_command = shlex.split(entry['command'])
compile_command[compile_command.index(entry['file'])] = str(source)
object_path = out / 'boolean-abstraction.o'
compile_command[compile_command.index('-o') + 1] = str(object_path)
subprocess.run(compile_command, cwd=build, check=True)
link_command = shlex.split((build / 'CMakeFiles/primo.dir/link.txt').read_text())
link_command[link_command.index('CMakeFiles/primo.dir/src/main.cpp.o')] = str(object_path)
executable = out / 'boolean-abstraction'
link_command[link_command.index('-o') + 1] = str(executable)
subprocess.run(link_command, cwd=build, check=True)
example = subprocess.run([str(executable)], capture_output=True, text=True, check=True, timeout=30)
assert example.stdout == ('unguarded active: 8 assignments checked\n'
                          'guarded active: 8 assignments checked\n'
                          'guarded inactive: 8 assignments checked\n'
                          'retired selector: reactivation is UNSAT\n'), example
(out / 'example.stdout').write_text(example.stdout)
(out / 'example.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'compile': compile_command, 'link': link_command,
    'assertions': 'enabled by source even under -DNDEBUG', 'exit_code': example.returncode,
}, indent=2) + '\n')

config = json.loads(Path(os.environ['PRIMO_HYBRID_CONFIG']).read_text())
profile = config['profiles']['hybrid']
for slot in (39, 85, 95, 198):
    case = config['cases'][slot]
    input_path = Path(config['roots'][case['logic']]) / case['file']
    assert hashlib.sha256(input_path.read_bytes()).hexdigest() == case['sha256']
    command = [sys.executable, 'scripts/profile_gdb_samples.py', '--out', str(out / f'slot-{slot}'),
               '--timeout', '12', '--interval', '0.15', '--seed', '20261112', '--',
               profile['solver'], *profile['args'], str(input_path)]
    # A deadline is expected for a bounded prefix of an expensive regression.
    subprocess.run(command, check=False, timeout=30)
    summary = json.loads((out / f'slot-{slot}' / 'summary.json').read_text())
    assert summary['samples'] > 0, summary
    assert summary['status'] == 'complete' or summary['diagnostic'] == 'profile deadline exceeded', summary
PY
