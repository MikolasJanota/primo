#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-hybrid-cli
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
out=${PRIMO_HYBRID_CLI_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - "$out" <<'PY'
from pathlib import Path
import shutil
import subprocess
import sys
out = Path(sys.argv[1])
solver = Path('build-slurm-rel/primo').resolve()
text = subprocess.check_output([str(solver), '--help'], text=True)
text = text.replace(f'{solver} [OPTIONS]', 'primo [OPTIONS]', 1)
text = '\n'.join(line.rstrip() for line in text.splitlines()).rstrip()+'\n'
(out / 'help.txt').write_text(text)
# Check every existing manual example against an isolated documentation copy,
# leaving the source snapshot and other active jobs untouched.
shadow = out / 'project'
shutil.copytree('docs/examples', shadow / 'docs/examples')
(shadow / 'scripts').mkdir()
shutil.copyfile('scripts/check_doc_examples.py', shadow / 'scripts/check_doc_examples.py')
(shadow / 'docs/examples/primo-help.txt').write_text(text)
with (out / 'examples.log').open('w') as stream:
    subprocess.run([sys.executable, str(shadow / 'scripts/check_doc_examples.py'),
                    '--solver', str(solver)], stdout=stream, stderr=subprocess.STDOUT, check=True)
PY
