#!/usr/bin/env bash
# Instruction profiles of selected frozen-binary prefixes, separate from timings.
#SBATCH --job-name=primo-uflia-equality-callgrind
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:06:00
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
input="${PRIMO_CORPUS_ROOT:?}/${fields[0]}"
solver=${PRIMO_DIAGNOSTIC_SOLVER:?}
"$solver" --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum "$solver" "$input" "${PRIMO_DIAGNOSTIC_MANIFEST:?}" > "$out/identities.sha256"
valgrind --version > "$out/valgrind-version.txt"
hostname > "$out/hostname.txt"
flags=(--quiet --stats --arithmetic-model-phase=off)
if [[ ${fields[1]} == m124 ]]; then
 flags+=(--lia-round-integer-bounds --lia-primitive-int-rows --no-lia-integer-slacks --lia-patch-integral-moves)
else
 flags+=(--no-lia-round-integer-bounds --no-lia-primitive-int-rows --no-lia-integer-slacks --no-lia-patch-integral-moves)
fi
roles=(off on)
if (( slot % 2 )); then roles=(on off); fi
for role in "${roles[@]}"; do
 extra=()
 if [[ $role == on ]]; then extra+=(--qf-uflia-tableau-equalities); fi
 python3 - "$out/$role-command.json" "$solver" "${flags[@]}" "${extra[@]}" "$input" <<'PYCODE'
import json
import sys
from pathlib import Path
Path(sys.argv[1]).write_text(json.dumps(sys.argv[2:]) + '\n')
PYCODE
 set +e
 timeout --signal=TERM --kill-after=10s 120s valgrind --tool=callgrind \
  --callgrind-out-file="$out/$role.callgrind" \
  "$solver" "${flags[@]}" "${extra[@]}" "$input" > "$out/$role.stdout" 2> "$out/$role.stderr"
 code=$?
 set -e
 printf '%s\n' "$code" > "$out/$role.exit-code"
 # 124 denotes the intentionally bounded, instrumented prefix; it is not a solve.
 [[ $code == 0 || $code == 124 ]]
 python3 - "$out/$role.stdout" <<'PYCODE'
import json
import os
import sys
from pathlib import Path
case = json.loads(Path(os.environ['PRIMO_DIAGNOSTIC_MANIFEST']).read_text())[int(os.environ['SLURM_ARRAY_TASK_ID'])]
answer = Path(sys.argv[1]).read_text().splitlines()[0]
assert answer in {'sat', 'unsat', 'unknown'}, answer
if answer in {'sat', 'unsat'} and case.get('expected_answer'):
    assert answer == case['expected_answer'], "profile answer disagrees with selected input's checked answer"
PYCODE
 test -s "$out/$role.callgrind"
 callgrind_annotate --auto=no --inclusive=no --threshold=95 "$out/$role.callgrind" > "$out/$role-self.txt"
 callgrind_annotate --auto=no --inclusive=yes --threshold=95 "$out/$role.callgrind" > "$out/$role-inclusive.txt"
done
sha256sum --check "$out/identities.sha256"
printf '%s\n' 'Instruction counts, not CPU-time samples; unequal progress under instrumentation.' > "$out/complete"
