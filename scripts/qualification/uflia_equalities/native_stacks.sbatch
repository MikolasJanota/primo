#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-native-stacks
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:04:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_EQ_OUT:?}/case-$slot"
test ! -e "$out"
mkdir -p "$out"
export DEBUGINFOD_URLS=''
mapfile -t fields < <(python3 - <<'PYCODE'
import json
import os
from pathlib import Path
case = json.loads(Path(os.environ['PRIMO_DIAGNOSTIC_MANIFEST']).read_text())[int(os.environ['SLURM_ARRAY_TASK_ID'])]
assert case['mode'] in {'native', 'm124'}
print(case['file'])
print(case['mode'])
PYCODE
)
[[ ${#fields[@]} == 2 ]]
export PRIMO_PROFILE_BINARY=${PRIMO_DIAGNOSTIC_SOLVER:?}
export PRIMO_PROFILE_CASE="${PRIMO_CORPUS_ROOT:?}/${fields[0]}"
export PRIMO_PROFILE_SAMPLES=120 PRIMO_PROFILE_INTERVAL=0.5
"$PRIMO_PROFILE_BINARY" --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum "$PRIMO_PROFILE_BINARY" "$PRIMO_PROFILE_CASE" "${PRIMO_EQ_GDB_SCRIPT:?}" > "$out/identities.sha256"
gdb --version > "$out/gdb-version.txt"
base='--quiet --stats --arithmetic-model-phase=off'
if [[ ${fields[1]} == m124 ]]; then
 base+=' --lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves'
else
 base+=' --no-lia-round-integer-bounds --no-lia-primitive-int-rows --no-lia-integer-slacks --no-lia-patch-integral-moves'
fi
for role in off on; do
 export PRIMO_PROFILE_FLAGS=$base
 if [[ $role == on ]]; then PRIMO_PROFILE_FLAGS+=' --qf-uflia-tableau-equalities'; fi
 export PRIMO_PROFILE_JSON="$out/$role.json"
 timeout --kill-after=5s 85s gdb -nx -nh -q -batch -x "$PRIMO_EQ_GDB_SCRIPT" > "$out/$role.log" 2>&1
done
python3 - "$out/on.json" <<'PYCODE'
import json
import sys
from pathlib import Path
samples = json.loads(Path(sys.argv[1]).read_text())['samples']
assert len(samples) == 120, ('incomplete enabled stack sampling', len(samples))
PYCODE
sha256sum --check "$out/identities.sha256"
printf '%s\n' 'Native wall-time stack samples, not CPU attribution or benchmark timing.' > "$out/complete"
