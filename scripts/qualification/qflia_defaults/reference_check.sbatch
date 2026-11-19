#!/usr/bin/env bash
#SBATCH --job-name=primo-qflia-default-reference
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3.11 - <<'PY'
import hashlib,json,os,socket
from pathlib import Path
from scripts.bench_qf_uf_full import run_solver
card=json.loads(Path('tmp/timing.json').read_text());case=card['cases'][123]
source=Path(card['roots'][case['logic']])/case['file']
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(source)==case['sha256']
label=os.environ.get('PRIMO_REFERENCE_ONLY') or ('z3','cvc5')[int(os.environ['SLURM_ARRAY_TASK_ID'])]
binary=Path(os.environ.get('PRIMO_CVC5_REFERENCE','contrib/local/bin/cvc5')) if label=='cvc5' else Path('contrib/local/bin/z3')
command=[str(binary.resolve())]
if label=='cvc5':command+=['--lang=smt2']
if os.environ.get('PRIMO_Z3_ARITH_SOLVER'):command+=['smt.arith.solver='+os.environ['PRIMO_Z3_ARITH_SOLVER']]
row=run_solver(command,source,120)
out=Path('results/reference-check');out.mkdir(exist_ok=True)
for kind in ('stdout','stderr'):(out/(label+'.'+kind)).write_text(row.pop(kind))
row.update(file=case['file'],input_sha256=case['sha256'],command=command,solver_sha256=sha(command[0]),job=os.environ['SLURM_JOB_ID'],host=socket.gethostname())
(out/(label+'.json')).write_text(json.dumps(row,indent=2)+'\n')
assert row['status']=='ok' and row['exit_code']==0 and row['answer']=='unsat',row
PY
