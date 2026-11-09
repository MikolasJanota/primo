#!/usr/bin/env bash
# Correctness-only generated inputs; no reference corpus benchmark is rerun.
#SBATCH --job-name=primo-uniform120-uf-gate
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 scripts/qf_uf_boolean_fuzz.py --solver build-slurm-rel/primo --solver-arg=--quiet \
 --ref contrib/local/bin/z3 --count 250 --seed "$((202611400 + SLURM_ARRAY_TASK_ID))" \
 --timeout 5 --keep-passing --validate-models --dolmen contrib/local/bin/dolmen \
 --out "${PRIMO_LONG_OUT:?}/gates/fuzz/uf-default/task-$SLURM_ARRAY_TASK_ID"
