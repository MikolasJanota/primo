#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-probe-headers
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
python3 - <<'PY'
import hashlib, json, shlex, subprocess
from pathlib import Path
entry = next(e for e in json.loads(Path('build-slurm-rel/compile_commands.json').read_text())
             if e['file'].endswith('/preprocess_bound_probe.cpp'))
flags = shlex.split(entry['command'])[1:]
i = flags.index('-o'); del flags[i:i + 2]
flags.remove('-c'); flags.remove(entry['file'])
original = Path(entry['file'])
final = Path('tmp/probe.cpp').resolve()
hashes = []
for source in [original, final]:
    run = subprocess.run(['c++', *flags, '-E', '-P', '-x', 'c++', '-'],
                         input=source.read_bytes(), capture_output=True, check=True)
    hashes.append({'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                   'preprocessed_sha256': hashlib.sha256(run.stdout).hexdigest()})
def body(source):
    return '\n'.join(line for line in source.read_text().splitlines()
                     if not line.startswith('#include '))
assert body(original) == body(final), 'Only direct includes may differ'
subprocess.run(['c++', *flags, '-fsyntax-only', str(final)], check=True)
for label, checks in [('tidy', []), ('includes', ['-checks=-*,clang-diagnostic-*,misc-include-cleaner'])]:
    with Path(f'results/probe-checks/final-{label}.log').open('w') as output:
        subprocess.run(['clang-tidy', str(final), *checks, '--', *flags],
                       stdout=output, stderr=subprocess.STDOUT, check=True, timeout=120)
Path('results/probe-checks/header-review.json').write_text(json.dumps({
    'sources': hashes, 'identical_body_after_removing_include_lines': True,
    'final_syntax_check': 'pass',
    'claim': 'Two redundant direct-provider includes; declaration order differs. Not a preprocessed identity claim.'
}, indent=2) + '\n')
PY
