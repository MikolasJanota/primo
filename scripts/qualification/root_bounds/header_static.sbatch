#!/usr/bin/env bash
#SBATCH --job-name=primo-root-bounds-header-static
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
python3 - <<'PY'
import json,shlex,subprocess
from pathlib import Path
build=Path('build-slurm-rel').resolve()
entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if e['file'].endswith('/root_bound_diagnostics.cpp'))
flags=shlex.split(entry['command'])[1:]
i=flags.index('-o'); del flags[i:i+2]
flags.remove('-c'); flags.remove(entry['file'])
source=Path('tmp/root_bounds/root-bound-headers.cpp').resolve()
for name,checks in [('tidy',[]),('includes',['-checks=-*,clang-diagnostic-*,misc-include-cleaner'])]:
 with Path('results/root-bounds-final-'+name+'.log').open('w') as out:
  subprocess.run(['clang-tidy',str(source),*checks,'--',*flags],cwd=entry['directory'],stdout=out,stderr=subprocess.STDOUT,check=True,timeout=120)
PY
