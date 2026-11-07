#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-ablation-unit
#SBATCH --partition=compute
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_EQ_OUT:?}
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/unit.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
build-slurm-rel/primo --help > "$out/help.txt"
sha256sum build-slurm-rel/primo > "$out/binary.sha256"
build-slurm-rel/primo_component_combination_tests
build-slurm-rel/primo_qf_uflia_solver_tests
export PRIMO_ABLATION_UNIT_OUT="$out"
python3 - <<'PYCODE'
import json
import os
import subprocess
from pathlib import Path
from scripts.bench_qf_uf_full import parse_stats
out = Path(os.environ['PRIMO_ABLATION_UNIT_OUT'])
source = out / 'probe.smt2'
source.write_text('''(set-logic QF_UFLIA)
(declare-fun x () Int)(declare-fun y () Int)(declare-fun p (Int) Bool)
(assert (<= 0 x))(assert (<= x 0))(assert (<= 0 y))(assert (<= y 0))
(assert (p x))(assert (not (p y)))(check-sat)
''')
records = []
for mode in ['absent', 'bare', 'off', 'scan', 'names', 'fixed', 'all', 'true', 'false', '0', '1']:
    flags = [] if mode == 'absent' else ['--qf-uflia-tableau-equalities' + ('' if mode == 'bare' else '=' + mode)]
    command = ['build-slurm-rel/primo', '--quiet', '--stats', '--preprocessing-mode=off', *flags, str(source)]
    proc = subprocess.run(command, text=True, capture_output=True, timeout=30)
    assert proc.returncode == 0 and proc.stdout.splitlines()[0] == 'unsat', (mode, proc.stdout, proc.stderr)
    stats = parse_stats(proc.stdout + '\n' + proc.stderr)
    active = mode not in ['absent', 'off', 'false', '0']
    assert (int(stats.get(':lia.equality.scans', 0)) > 0) == active, (mode, stats)
    if mode == 'names':
        assert int(stats.get(':combination.equality.named-without-proof', 0)) > 0
        assert int(stats.get(':combination.equality.clauses', 0)) == 0
    if mode == 'scan':
        assert int(stats.get(':lia.equality.scan-only-proofs', 0)) > 0
        assert int(stats.get(':combination.equality.batches', 0)) == 0
    records.append({'mode': mode, 'command': command, 'exit_code': proc.returncode, 'stats': stats})
bad = subprocess.run(['build-slurm-rel/primo', '--qf-uflia-tableau-equalities=invalid', str(source)], text=True, capture_output=True, timeout=30)
assert bad.returncode != 0
(out / 'cli.json').write_text(json.dumps(records, indent=2) + '\n')
print('CLI compatibility/mode checks:', len(records), 'plus invalid mode rejection')
PYCODE
