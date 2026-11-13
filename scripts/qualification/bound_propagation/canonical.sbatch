#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-propagation-canonical
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PRIMO_EXPECTED_REVISION=${PRIMO_EXPECTED_GIT:?}
export PRIMO_FUZZ_COUNT=250 PRIMO_FINITE_DOMAIN_FUZZ_COUNT=25
export PRIMO_FUZZ_SEED_BASE=2026111380
export PRIMO_FUZZ_SOLVER_ARGS='--quiet --lra-propagation --lra-row-propagation --lra-bidirectional-row-propagation --check-model'
profile=rows
if [[ ${SLURM_ARRAY_TASK_ID:?} -ge 4 ]]; then
 profile=both
 export PRIMO_FUZZ_SOLVER_ARGS="$PRIMO_FUZZ_SOLVER_ARGS --preprocess-bound-tightening"
fi
export PRIMO_FUZZ_OUT_DIR="${PRIMO_PROP_OUT:?}/gates/canonical-$profile"
export PRIMO_FINITE_DOMAIN_QF_UF_ACTIVATION_PROFILE=answers
export PRIMO_FINITE_DOMAIN_QF_UFLRA_ACTIVATION_PROFILE=answers
export PRIMO_FINITE_DOMAIN_QF_UFLRA_REQUIRE_POST_AMO=0
export PRIMO_FUZZ_MODEL_TIMEOUT=30 PRIMO_FUZZ_MODEL_DOLMEN_TIMEOUT=30
bash scripts/slurm_qf_uflra_fuzz_gate.sbatch
