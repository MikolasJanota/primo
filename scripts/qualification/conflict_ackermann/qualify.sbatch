#!/usr/bin/env bash
#SBATCH --job-name=primo-conflict-ack-qualify
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - <<'PY'
import collections,csv,json,os,shlex,sys
from pathlib import Path
from scripts.qualification.arithmetic_combinations import qualify as q
campaign_path=Path(os.environ['PRIMO_ACK_CAMPAIGN'])
campaign=json.loads(campaign_path.read_text())
slot=int(os.environ['SLURM_ARRAY_TASK_ID']);task=campaign['tasks'][slot]
phase=os.environ['PRIMO_ACK_PHASE']
assert q.sha(Path(campaign['solver']))==campaign['sha256']
out=Path(campaign['results'])/phase/f'task-{slot}'
if phase=='generated' and task['logic']=='QF_UF':
 flags=[*task['args'],'--check-model'];seed=campaign['protocol']['generated_seed_base']+slot
 command=[sys.executable,'scripts/fuzz_external.py','--fuzzer','fuzzsmt','--logic','QF_UF',
  '--solver',shlex.join([campaign['solver'],*flags]),'--refs','z3',
  '--fuzzer-args','-ms 1 -Ms 3 -mv 1 -Mv 5 -mf 1 -mp 1 -ma 1 -Ma 4',
  '--count','1000','--seed',str(seed),'--timeout','5','--out',str(out)]
 code=q.execute(command,allow_failure=True)
 path=out/'summary.csv';rows=list(csv.DictReader(path.open()));assert len(rows)==1000
 assert {r['outcome'] for r in rows}<={'PASS','TIMEOUT','REFERENCE_UNUSABLE'}
 assert all(r.get('refs')=='TIMEOUT' for r in rows if r['outcome']=='REFERENCE_UNUSABLE')
 selected=[r['file'] for r in rows if r['ours']=='sat']
 result={'command':command,'seed':seed,'generator_exit':code,'cases':len(rows),
  'outcomes':dict(collections.Counter(r['outcome'] for r in rows)),
  'summary_sha256':q.sha(path),'resource_unknowns':[],
  **q.models(campaign['solver'],flags,selected,out/'models')}
elif phase=='generated':
 result=q.generated(campaign,task,slot,out)
elif phase=='seeds':
 q.execute([sys.executable,'scripts/qualification/bound_propagation/seeds.py',
  '--profile',task['label'],'--profile-args-json',str(Path(campaign['configs'])/'profile-args.json'),
  '--logic',task['logic'],'--manifest',campaign['regression_seeds'],'--out',str(out)])
 result=json.loads((out/'complete.json').read_text())
elif phase=='mutations':
 result=q.mutations(campaign,task,slot,out)
else:
 raise ValueError(phase)
receipt={'task':task,'phase':phase,'job':os.environ['SLURM_JOB_ID'],
 'native_sha256':campaign['sha256'],'source':campaign['git'],
 'campaign_sha256':q.sha(campaign_path),'script_sha256':q.sha(Path(os.environ['PRIMO_ACK_JOB_FILE'])),**result}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in receipt.items() if k not in ('command','task')},indent=2))
PY
