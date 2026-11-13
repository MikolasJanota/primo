#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-headers
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
rows=[]
for build_name in ['build-slurm-rel','build-asan']:
 build=Path(build_name).resolve()
 entry=next(e for e in json.loads((build/'compile_commands.json').read_text()) if e['file'].endswith('/preprocess_bound_tightening.cpp'))
 command=shlex.split(entry['command'])
 i=command.index('-o'); del command[i:i+2]
 command.remove('-c'); command.remove(entry['file'])
 command+=['-E','-P','-x','c++','-']
 paths=[Path(entry['file']),Path('tmp/bound_tightening/headers.cpp').resolve()]
 pair=[]
 for path in paths:
  result=subprocess.run(command,input=path.read_bytes(),capture_output=True,cwd=entry['directory'],check=True)
  pair.append({'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'preprocessed_sha256':hashlib.sha256(result.stdout).hexdigest()})
 assert pair[0]['preprocessed_sha256']==pair[1]['preprocessed_sha256'],pair
 rows.append({'build':build_name,'pair':pair})
out=Path('results/bound-tightening-header-equivalence.json')
out.write_text(json.dumps({'rows':rows,'claim':'Identical preprocessed C++ in Release and ASan after direct-header and optional-engagement-comment cleanup.'},indent=2)+'\n')
print(out.read_text())
PY
