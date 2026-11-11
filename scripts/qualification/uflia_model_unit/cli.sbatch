#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-model-unit-cli
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:08:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_UNIT_CLI_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --help > "$out/help.txt"
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 - "$out" <<'PY'
from pathlib import Path
import json,subprocess,sys
from scripts.bench_qf_uf_full import parse_stats
out=Path(sys.argv[1]);case=out/'probe.smt2'
case.write_text('(set-logic QF_UFLIA)(declare-fun x () Int)(declare-fun y () Int)(declare-fun p (Int) Bool)(assert (<= 0 x))(assert (<= x 0))(assert (<= 0 y))(assert (<= y 0))(assert (p x))(assert (not (p y)))(check-sat)\n')
rows=[]
for producer in ('legacy','model-unit'):
 for policy in ('off','scan','names','fixed','all'):
  flags=['--quiet','--stats','--preprocessing-mode=off','--qf-uflia-lia-equality-producer='+producer,'--qf-uflia-tableau-equalities='+policy]
  result=subprocess.run(['build-slurm-rel/primo',*flags,str(case)],capture_output=True,text=True,timeout=30)
  assert result.returncode==0 and result.stdout.splitlines()[0]=='unsat',(flags,result.stderr)
  stats=parse_stats(result.stdout+'\n'+result.stderr)
  selected=':lia.equality.model-unit.selected' if producer=='model-unit' else ':lia.equality.legacy-producer-selected'
  assert (int(stats.get(selected,0))>0)==(policy!='off'),(flags,stats)
  if policy=='names':assert int(stats.get(':combination.equality.clauses',0))==0
  if policy=='scan':assert int(stats.get(':combination.equality.batches',0))==0
  rows.append({'flags':flags,'statistics':stats,'exit_code':result.returncode})
for invalid in ('adaptive','unknown'):
 run=subprocess.run(['build-slurm-rel/primo','--qf-uflia-lia-equality-producer='+invalid,str(case)],capture_output=True,text=True,timeout=30)
 assert run.returncode!=0
(out/'cli.json').write_text(json.dumps(rows,indent=2)+'\n')
PY
