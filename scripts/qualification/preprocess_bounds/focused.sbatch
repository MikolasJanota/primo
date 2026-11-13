#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-focused
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
ctest --test-dir build-slurm-rel -R '^primo_preprocess_bound_tightening_tests$' --output-on-failure --timeout 120
python3 - <<'PY'
import subprocess
source = '''(set-logic QF_UF)(declare-sort U 0)
(declare-const a U)(declare-const b U)(declare-fun f (U) U)
(assert (= (f a) (f b)))(check-sat)'''
result = subprocess.run(['build-slurm-rel/primo', '--quiet', '--stats',
                         '--preprocess-bound-tightening'], input=source, text=True,
                        capture_output=True, timeout=10, check=True)
assert result.stdout.strip() == 'sat', result
assert ':preprocessing.bounds.' not in result.stderr, result.stderr
print('Nontrivial pure-UF scope regression passed')
PY
