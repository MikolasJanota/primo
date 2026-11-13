#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-propagation-seeds
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
profiles=(rows both)
profile=${profiles[${SLURM_ARRAY_TASK_ID:?}]}
python3 "${PRIMO_PROP_SEED_RUNNER:-scripts/qualification/bound_propagation/seeds.py}" --profile "$profile" \
 --manifest "${PRIMO_PROP_SEED_MANIFEST:-benchmarks/bound-propagation-20261113/regression-seeds.json}" \
 --out "${PRIMO_PROP_OUT:?}/gates/${PRIMO_PROP_SEED_PREFIX:-seeds}-$profile"
