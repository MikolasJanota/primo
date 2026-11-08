#!/usr/bin/env bash
# Submit as a separate correctness stage after the paired timing array ends.
#SBATCH --job-name=primo-lia-cube-models
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PY'
import json
import os
import subprocess
from pathlib import Path

config = json.loads(Path(os.environ['PRIMO_COMPARE_CONFIG']).read_text())
root = Path(os.environ['PRIMO_COMPARE_OUT'])
task = int(os.environ['SLURM_ARRAY_TASK_ID'])
failed = []
for profile in config['profiles']:
    if profile['binary'] == 'z3':
        continue
    command = ['python3', 'scripts/benchmark_model_replay.py', 'replay',
               '--out', str(root / profile['name'] / f'shard-{task}'),
               '--model-timeout', os.environ.get('PRIMO_MODEL_TIMEOUT', '90'),
               '--dolmen-timeout', os.environ.get('PRIMO_DOLMEN_TIMEOUT', '30'),
               '--dolmen-arg=--strict=false',
               '--attempt', os.environ.get('PRIMO_MODEL_ATTEMPT', 'model-validation')]
    if subprocess.run(command, check=False).returncode:
        failed.append(profile['name'])
if failed:
    raise SystemExit('incomplete/failed model profiles: ' + ', '.join(failed))
PY
