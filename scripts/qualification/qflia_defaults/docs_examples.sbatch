#!/usr/bin/env bash
#SBATCH --job-name=primo-qflia-default-docs
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3.11 - <<'PY'
import hashlib,json,os,subprocess
from pathlib import Path
work=Path.cwd(); fixtures=work/'tmp/docs-fixtures';out=work/'results/docs-examples';out.mkdir()
script=fixtures/'scripts/check_doc_examples.py'
assert script.read_bytes()==Path('scripts/check_doc_examples.py').read_bytes()
solver=work/'build-slurm-rel/primo'
command=['python3.11',str(script),'--solver',str(solver),'--timeout','30']
with (out/'check.log').open('w') as stream:
 subprocess.run(command,check=True,stdout=stream,stderr=subprocess.STDOUT)
(out/'provenance.json').write_text(json.dumps({'job':os.environ['SLURM_JOB_ID'],'command':command,'solver_sha256':hashlib.sha256(solver.read_bytes()).hexdigest(),'help_sha256':hashlib.sha256((fixtures/'docs/examples/primo-help.txt').read_bytes()).hexdigest(),'fixtures':{str(p.relative_to(fixtures)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(fixtures.rglob('*')) if p.is_file()}},indent=2)+'\n')
PY
