#!/usr/bin/env bash
# Paired timing and separate replay on identical shards; private config supplies paths.
#SBATCH --job-name=primo-arithmetic-overhead-measure
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PY'
import hashlib
import json
import os
import subprocess
from pathlib import Path

path = Path(os.environ['PRIMO_COMPARE_CONFIG'])
config = json.loads(path.read_text())
out = Path(os.environ['PRIMO_COMPARE_OUT'])
task = int(os.environ['SLURM_ARRAY_TASK_ID'])
phase = os.environ['PRIMO_OVERHEAD_PHASE']
assert phase in {'benchmark', 'replay'}
for repetition in range(config['repetitions']):
    target = out / f'repetition-{repetition}'
    per_round = target / 'configs' / f'shard-{task}.json'
    if phase == 'benchmark':
        per_round.parent.mkdir(parents=True, exist_ok=True)
        profiles = config['profiles']
        offset = repetition % len(profiles)
        derived = dict(config, profiles=profiles[offset:] + profiles[:offset],
                       parent_config_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                       repetition=repetition)
        per_round.write_text(json.dumps(derived, indent=2) + '\n')
    env = dict(os.environ, PRIMO_COMPARE_CONFIG=str(per_round), PRIMO_COMPARE_OUT=str(target))
    script = ('scripts/slurm_lia_cube_compare.sbatch' if phase == 'benchmark'
              else 'scripts/qualification/lia_cubes/models.sbatch')
    subprocess.run(['bash', script], env=env, check=True)
PY
