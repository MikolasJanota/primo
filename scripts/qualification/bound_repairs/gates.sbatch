#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-repairs-qualify
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
[[ $slot -ge 0 && $slot -lt 10 ]]
target="${PRIMO_PROP_OUT:?}/gates/gate-$slot"
base=${PRIMO_FUZZ_SEED_BASE:-2026111440}
[[ $base =~ ^[0-9]+$ ]]
seed=$((base+slot))
flags=(--solver-arg=--quiet --solver-arg=--lra-propagation --solver-arg=--lra-row-propagation --solver-arg=--lra-bidirectional-row-propagation --solver-arg=--check-model)
pre=
if [[ ( $slot -ge 4 && $slot -lt 8 ) || $slot -eq 9 ]]; then
 flags+=(--solver-arg=--preprocess-bound-tightening)
 pre=--preprocess-bound-tightening
fi
if [[ $slot -ge 8 ]]; then
 flags+=(--solver-arg=--lia-round-integer-bounds)
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo "${flags[@]}" --ref z3 \
  --count 1000 --seed "$seed" --shape mixed --depth 3 --timeout 5 --out "$target"
 python3 "${PRIMO_REPAIR_MODEL_RUNNER:?}" --slot "$slot" --root "${PRIMO_PROP_OUT:?}/gates" --allow-private-node-limit
 exit 0
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
resource_flags=()
# Keep diagnosed private-node-limit unknowns separate from reference matches.
# The model auditor replays each unknown and checks its exact diagnostic/counter.
if [[ $slot == 1 || $slot == 5 ]]; then
 resource_flags+=(--allow-private-node-limit)
fi
python3 "${PRIMO_REPAIR_MODEL_RUNNER:?}" --slot "$slot" --root "${PRIMO_PROP_OUT:?}/gates" "${resource_flags[@]}"
