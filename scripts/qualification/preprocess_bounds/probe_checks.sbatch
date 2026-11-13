#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-probe-checks
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
mkdir -p results/probe-checks
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo > results/probe-checks/doc-examples.log
python3 - <<'PY'
import json, subprocess
from pathlib import Path
source = Path('tests/tools/preprocess_bound_probe.cpp').resolve()
for label, checks in [('tidy', []), ('includes', ['-checks=-*,clang-diagnostic-*,misc-include-cleaner'])]:
    with Path(f'results/probe-checks/{label}.log').open('w') as output:
        subprocess.run(['clang-tidy', str(source), '-p', 'build-slurm-rel', *checks],
                       stdout=output, stderr=subprocess.STDOUT, check=True, timeout=120)
Path('results/probe-checks/complete.json').write_text(json.dumps({'job': __import__('os').environ['SLURM_JOB_ID']}) + '\n')
PY
