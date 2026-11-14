#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-final-triage
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
if [[ ${SLURM_ARRAY_TASK_ID:?} == 0 ]]; then
 python3 scripts/qualification/bound_repairs/gate_models.py --slot 4 --root results/qualification/gates --allow-reference-timeout
 exit 0
fi
python3 - <<'PY'
import csv,json,os,sys,subprocess
from pathlib import Path
sys.path.insert(0,str(Path.cwd()))
from scripts.bench_qf_uf_full import run_solver
out=Path('results/resource-triage');out.mkdir()
flags=['--quiet','--stats','--check-model','--lra-propagation','--lra-row-propagation','--lra-bidirectional-row-propagation']
requests=[]
summaries=sorted(Path('results/mutations').glob('*/mutant-answers.csv'))
assert len(summaries)==8
for p in summaries:
 for row in csv.DictReader(p.open()):
  if row['outcome']!='PASS':requests.append((p.parent.name,Path(row['file']),row['reference'] if row['reference'] in {'sat','unsat'} else row['ours']))
p=Path('results/qualification/gates/gate-4/summary.csv')
for row in csv.DictReader(p.open()):
 if row['outcome']=='REFERENCE_UNUSABLE':requests.append(('generated',Path(row['file']),'sat'))
p=Path('results/qualification/gates/gate-1/summary.csv')
for row in csv.DictReader(p.open()):
 if row['outcome']=='UNKNOWN_INVOLVED':requests.append(('gate-1',Path(row['case']),row['references']))
results=[]
for group,source,expected in requests:
 if expected not in {'sat','unsat'}:expected=None
 options=flags+(['--preprocess-bound-tightening'] if group.endswith('-both') or group=='generated' else [])
 for label,binary in [('reference','contrib/local/bin/z3'),('candidate','build-slurm-rel/primo'),('baseline',os.environ['PRIMO_BASELINE_SOLVER'])]:
  command=[binary] if label=='reference' else [binary,*options]
  result=run_solver(command,source,60 if label=='reference' else 15)
  assert result['status'] in {'ok','timeout'},result
  if result['status']=='ok':
   assert result['exit_code']==0,result
   if result['answer'] in {'sat','unsat'}:
    if expected is None:expected=result['answer']
    assert result['answer']==expected,(source,result,expected)
   else:
    assert result['answer']=='unknown' and 'integer branch-and-bound node limit' in result['stderr'].splitlines(),result
    assert int(result['stats'].get(':lia.branching.budget-exits',0))>0,result
  results.append({'group':group,'file':source.name,'variant':label,'expected':expected,**result})
  (out/'rows.json').write_text(json.dumps(results,indent=2)+'\n')
  if label!='reference' and result['status']=='ok' and result['answer']=='sat':
   subprocess.run([sys.executable,'scripts/validate_model.py','--solver',binary,*['--solver-arg='+f for f in options], '--expect-sat','--require-dolmen-pass','--dolmen-arg=--strict=false','--timeout','30','--dolmen-timeout','30','--out',str(out/'models'/group/(source.stem+'-'+label)),str(source)],check=True)
(out/'complete.json').write_text(json.dumps({'requests':len(requests),'job':os.environ['SLURM_JOB_ID']})+'\n')
PY
