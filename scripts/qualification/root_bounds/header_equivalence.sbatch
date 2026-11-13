#!/usr/bin/env bash
#SBATCH --job-name=primo-root-bounds-headers
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import hashlib,json,shlex,subprocess
from pathlib import Path
build=Path('build-slurm-rel').resolve()
entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if e['file'].endswith('/root_bound_diagnostics.cpp'))
command=shlex.split(entry['command'])
i=command.index('-o'); del command[i:i+2]
command.remove('-c'); command.remove(entry['file'])
command+=['-E','-P','-x','c++','-']
paths=[Path(entry['file']),Path('tmp/root_bounds/root-bound-headers.cpp').resolve()]
rows=[]
for path in paths:
 result=subprocess.run(command,input=path.read_bytes(),capture_output=True,cwd=entry['directory'],check=True)
 rows.append({'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'preprocessed_sha256':hashlib.sha256(result.stdout).hexdigest()})
assert rows[0]['preprocessed_sha256']==rows[1]['preprocessed_sha256'],rows
out=Path('results/root-bounds-header-equivalence.json')
out.write_text(json.dumps({'rows':rows,'claim':'Identical preprocessed C++ after direct-header and ownership-comment cleanup; no semantic change.'},indent=2)+'\n')
print(out.read_text())
PY
