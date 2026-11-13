#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-propagation-qualify
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_FUZZSMT_BIN_DIR:?}:$PWD/contrib/local/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
[[ $slot -ge 0 && $slot -lt 8 ]]
target="${PRIMO_PROP_OUT:?}/gates/gate-$slot"
seed=$((2026111340+slot))
flags=(--solver-arg=--quiet --solver-arg=--lra-propagation --solver-arg=--lra-row-propagation --solver-arg=--lra-bidirectional-row-propagation --solver-arg=--check-model)
pre=
if [[ $slot -ge 4 ]]; then
 flags+=(--solver-arg=--preprocess-bound-tightening)
 pre=--preprocess-bound-tightening
fi
case $((slot%4)) in
 0)
 python3 scripts/fuzz_external.py --fuzzer fuzzsmt --logic QF_LIA --count 1000 --seed "$seed" --timeout 5 \
  --solver "build-slurm-rel/primo --quiet --lra-propagation --lra-row-propagation --lra-bidirectional-row-propagation --check-model $pre" \
  --refs z3 --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' --out "$target"
 ;;
 1)
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" --ref z3 \
  --count 1000 --seed "$seed" --shape mixed --depth 3 --timeout 5 --out "$target"
 ;;
 2)
 python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" \
  --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --count 1000 --seed "$seed" --timeout 5 --out "$target"
 ;;
 3)
 python3 scripts/qf_uflra_smoke_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" --ref z3 \
  --count 1000 --seed "$seed" --timeout 5 --out "$target"
 ;;
esac
python3 scripts/qualification/bound_propagation/gate_models.py --slot "$slot" --root "${PRIMO_PROP_OUT:?}/gates"
