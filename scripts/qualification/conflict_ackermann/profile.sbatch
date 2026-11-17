#!/usr/bin/env bash
#SBATCH --job-name=primo-conflict-ack-profile
#SBATCH --cpus-per-task=1
#SBATCH --mem=10G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
export DEBUGINFOD_URLS=''
python3 - <<'PY'
import hashlib,json,os,signal,socket,subprocess,time
from pathlib import Path
config=json.loads(Path(os.environ['PRIMO_ACK_PROFILE_CONFIG']).read_text())
slot=int(os.environ['SLURM_ARRAY_TASK_ID']);case=config['cases'][slot]
source=Path(config['roots'][case['logic']])/case['file']
assert hashlib.sha256(source.read_bytes()).hexdigest()==case['sha256']
out=Path(config['out'])/f'case-{slot}';out.mkdir(parents=True,exist_ok=False)
records=[]
for label,profile in config['profiles'].items():
 assert hashlib.sha256(Path(profile['solver']).read_bytes()).hexdigest()==profile['sha256']
 command=['valgrind','--enable-debuginfod=no','--tool=callgrind','--callgrind-out-file='+str(out/(label+'.callgrind')),
  profile['solver'],*profile['args'],'--max-theory-conflicts=100',str(source)]
 start=time.monotonic()
 with (out/(label+'.stdout')).open('w') as stdout,(out/(label+'.stderr')).open('w') as stderr:
  child=subprocess.Popen(command,stdout=stdout,stderr=stderr,start_new_session=True)
  truncated=False
  try: child.wait(timeout=60)
  except subprocess.TimeoutExpired:
   truncated=True
   subprocess.run(['callgrind_control','-d',str(child.pid)],stdout=stderr,stderr=stderr,timeout=15,check=True)
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=5)
   except subprocess.TimeoutExpired:
    os.killpg(child.pid,signal.SIGKILL);child.wait()
  assert truncated or child.returncode==0
 answers=[s.strip() for s in (out/(label+'.stdout')).read_text().splitlines() if s.strip() in ('sat','unsat','unknown')]
 assert len(answers)<=1 and (truncated or len(answers)==1)
 if answers and answers[0] in ('sat','unsat') and case['expected'] in ('sat','unsat'):assert answers[0]==case['expected']
 snapshots=list(out.glob(label+'.callgrind*'));snapshot=max(snapshots,key=lambda p:p.stat().st_size)
 assert snapshot.stat().st_size>0
 with (out/(label+'.annotation')).open('w') as stdout:
  subprocess.run(['callgrind_annotate','--inclusive=yes','--threshold=95',str(snapshot)],stdout=stdout,check=True)
 records.append({'profile':label,'command':command,'exit_code':child.returncode,'truncated':truncated,'answer':answers[0] if answers else None,'instrumented_seconds':time.monotonic()-start,'input_sha256':case['sha256'],'native_sha256':profile['sha256']})
(out/'metadata.json').write_text(json.dumps({'job':os.environ['SLURM_JOB_ID'],'host':socket.gethostname(),'case':case,'records':records,'scope':'Callgrind instruction diagnostic;100theory-conflict limit/60s collection window per process; explicit callgrind_control dump then termination on deadline; truncated profiles describe partial execution, instruction totals are not comparable completed work,10G allocation,no native address-space cap under Valgrind; never used as native timing.'},indent=2)+'\n')
PY
