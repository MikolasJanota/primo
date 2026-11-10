#!/usr/bin/env bash
# Observe changed clause counts in the existing finite-domain unit fixtures.
#SBATCH --job-name=primo-model-repair-counters
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
out=${PRIMO_REPAIR_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - "$out" <<'PY'
from pathlib import Path
import json,re,subprocess,sys
out=Path(sys.argv[1]);records=[]
for file,names,mode in [
 ('tests/unit/boolean_solver_tests.cpp', ['strong_preprocessing_enables_finite_domain_amo','finite_domain_raw_amo_is_refreshed_after_forced_atom_simplification','finite_domain_post_amo_can_be_disabled','finite_domain_amo_uses_forced_disequalities_modulo_forced_equalities'], 'strong'),
 ('tests/unit/qf_lra_solver_tests.cpp', ['qf_uf_lra_does_not_restore_eliminated_finite_domain_choices','qf_uf_lra_finite_domain_amo_sat_control_remains_sat'],'auto')]:
 text=Path(file).read_text()
 for name in names:
  body=text.split('void '+name+'() {',1)[1].split('\nvoid ',1)[0]
  smt=re.search(r'R"\((.*?)\)"',body,re.S)[1]
  source=out/(name+'.smt2');source.write_text(smt)
  flags=['--quiet','--stats','--preprocessing-mode='+mode]
  if 'options.finite_domain_post_amo = false' in body:flags.append('--no-finite-domain-post-amo')
  run=subprocess.run(['build-slurm-rel/primo',*flags,str(source)],capture_output=True,text=True,timeout=30)
  (out/(name+'.stdout')).write_text(run.stdout);(out/(name+'.stderr')).write_text(run.stderr)
  records.append({'fixture':name,'arguments':flags,'exit_code':run.returncode,'answer':run.stdout.splitlines()[0],'statistics':dict(re.findall(r'^(:preprocessing\.[\w.-]+)\s+(\S+)$',run.stdout+run.stderr,re.M))})
  assert run.returncode==0,(name,run.stderr)
(out/'counters.json').write_text(json.dumps(records,indent=2)+'\n')
PY
