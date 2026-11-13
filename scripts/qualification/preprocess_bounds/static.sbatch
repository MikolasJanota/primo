#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-tightening-static
#SBATCH --cpus-per-task=8
#SBATCH --mem=24G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - <<'PY'
import concurrent.futures, hashlib, json, os, shlex, subprocess, sys
from pathlib import Path
out=Path(os.environ['PRIMO_BOUND_TIGHTENING_OUT'])/'static-final'
out.mkdir(parents=True,exist_ok=False)
subprocess.run(['git','init','--quiet'],check=True)
subprocess.run(['git','add','src','tests','docs/examples'],check=True)
subprocess.run([sys.executable,'scripts/prepare_static_analysis.py','--build','build-slurm-rel','--out',str(out/'database')],check=True)
manifest=json.loads((out/'database/manifest.json').read_text())
previous=Path(os.environ['PRIMO_BOUND_PREVIOUS_STATIC'])
old=json.loads((previous/'database/manifest.json').read_text())
old_rows={r['file']:r for r in json.loads((previous/'summary.json').read_text())}
current_root=str(Path.cwd())
# Database entries contain their original root, including directory/file args.
old_db=json.loads((previous/'database/compile_commands.json').read_text())
new_db=json.loads((out/'database/compile_commands.json').read_text())
old_root=next(e['file'].split('/src/')[0] for e in old_db if '/src/' in e['file'])
def signatures(entries,root):
 result={}
 for e in entries:
  source=e['file'].removeprefix(root+'/')
  result[source]=json.dumps(e,sort_keys=True).replace(root,'<source>')
 return result
before,after=signatures(old_db,old_root),signatures(new_db,current_root)
headers_same={p:h for p,h in old['source_sha256'].items() if not p.endswith('.cpp')}=={p:h for p,h in manifest['source_sha256'].items() if not p.endswith('.cpp')}
headers=Path(os.environ['PRIMO_CXX_HEADER_MANIFEST']).read_text().splitlines()
extra=[a for root in headers for a in ('--extra-arg=-isystem','--extra-arg='+root)]
sources=sorted(p for p in manifest['source_sha256'] if p.endswith('.cpp'))
def scan(item):
 index,source=item
 prior=old_rows.get(source)
 row={'file':source,'sha256':manifest['source_sha256'][source]}
 reuse=(headers_same and prior and prior['tidy']==prior['includes']==0 and prior['sha256']==row['sha256'] and before.get(source)==after.get(source))
 if reuse:
  return dict(row,tidy=0,includes=0,reused=True)
 for label,checks in [('tidy',[]),('includes',['-checks=-*,clang-diagnostic-*,misc-include-cleaner'])]:
  with (out/f'{index:03d}-{label}.log').open('w') as stream:
   row[label]=subprocess.run(['clang-tidy','-p',str(out/'database'),*checks,*extra,source],stdout=stream,stderr=subprocess.STDOUT,timeout=900,check=False).returncode
 return dict(row,reused=False)
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ['SLURM_CPUS_PER_TASK'])) as pool:
 rows=list(pool.map(scan,enumerate(sources)))
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
(out/'reuse.json').write_text(json.dumps({'headers_identical':headers_same,'previous_summary_sha256':hashlib.sha256((previous/'summary.json').read_bytes()).hexdigest(),'current_manifest_sha256':hashlib.sha256((out/'database/manifest.json').read_bytes()).hexdigest(),'cpp_units':len(rows),'reused':sum(r['reused'] for r in rows),'rescanned':sum(not r['reused'] for r in rows),'policy':'Reuse only with identical tracked headers,source hash and full normalized compiler database entry; changed units rescanned.'},indent=2)+'\n')
assert all(r['tidy']==r['includes']==0 for r in rows),'inspect static tool failures'
PY
