#!/usr/bin/env bash
#SBATCH --job-name=primo-qflia-default-include
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3.11 - <<'PY'
import hashlib,json,os,re,shlex,subprocess
from pathlib import Path
out=Path('results/include-hygiene-final-3');out.mkdir()
db=json.loads(Path('build-slurm-rel/compile_commands.json').read_text())
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
baseline=json.loads(Path('results/static/tidy-summary.json').read_text())
allowed={(d['file'],d['message'],d['check']) for d in baseline['diagnostics']}
pattern=re.compile(r'^(.+?):\d+:\d+: warning: (.+) \[([^\]]+)\]$',re.M)
records=[]
for relative in ('src/smt/lia_settings_policy.cpp','tests/unit/tactic_tests.cpp'):
 source=Path('tmp/hygiene',Path(relative).name).resolve()
 entry,=(r for r in db if r['file'].endswith('/'+relative))
 flags=[];original=shlex.split(entry['command']);i=1;old=None
 while i<len(original):
  if original[i]=='-o':old=Path(entry['directory'])/original[i+1];i+=2
  elif original[i]=='-c':i+=2
  else:flags.append(original[i]);i+=1
 assert old is not None
 rebuilt=out/(source.stem+'.o')
 command=[original[0],*flags,'-c',str(source),'-o',str(rebuilt)]
 subprocess.run(command,check=True);assert old.read_bytes()==rebuilt.read_bytes()
 log=out/(source.stem+'-clang-tidy.log')
 with log.open('w') as stream:
  subprocess.run([os.environ['PRIMO_VERIFY_BIN']+'/clang-tidy',str(source),'-checks=-*,clang-analyzer-*,misc-include-cleaner','--',*flags],check=True,stdout=stream,stderr=subprocess.STDOUT)
 text=log.read_text();assert ': error:' not in text,text
 diagnostics=[(file.removeprefix(str(Path.cwd())+'/'),message,check) for file,message,check in pattern.findall(text)]
 assert all(d in allowed and d[2]!='misc-include-cleaner' for d in diagnostics),diagnostics
 records.append({'source':relative,'command':command,'source_sha256':sha(source),'object_sha256':sha(old),'rebuilt_object_identical':True,'clang_tidy_sha256':sha(log),'existing_advisory_signatures':diagnostics})
(out/'provenance.json').write_text(json.dumps({'job':os.environ['SLURM_JOB_ID'],'native_sha256':sha('build-slurm-rel/primo'),'records':records},indent=2)+'\n')
PY
