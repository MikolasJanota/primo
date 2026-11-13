#!/usr/bin/env bash
# Reusable SLURM gate for general and finite-domain QF_UFLRA fuzzing.
#
# Submit from a prepared remote workspace, for example:
#   sbatch scripts/slurm_qf_uflra_fuzz_gate.sbatch
#
# Override PRIMO_WORKDIR, PRIMO_FUZZ_COUNT, PRIMO_FUZZ_SEED_BASE,
# PRIMO_FUZZ_SOLVER_ARGS, PRIMO_FUZZ_OUT_DIR, or
# PRIMO_FINITE_DOMAIN_QF_UFLRA_ACTIVATION_PROFILE or
# PRIMO_FINITE_DOMAIN_QF_UFLRA_REQUIRE_POST_AMO in the sbatch environment when
# needed.
# PRIMO_FUZZ_SOLVER_ARGS is split on spaces into repeated --solver-arg values;
# use it only for option tokens that contain no spaces. Set the QF_UFLRA
# activation profile to structural and its post-AMO requirement to 0 when the
# selector intentionally chooses a non-AMO profile. General and finite-domain
# QF_UFLRA cases are rerun with get-model and checked by Dolmen unless
# PRIMO_FUZZ_MODEL_VALIDATION=0 is set explicitly.
# Keep cluster hostnames, usernames, and absolute home paths out of this file.
#SBATCH --job-name=primo-qf-uflra-fuzz
#SBATCH --output=slurm_logs/primo-qf-uflra-fuzz-%A_%a.out
#SBATCH --error=slurm_logs/primo-qf-uflra-fuzz-%A_%a.err
#SBATCH --array=0-3
#SBATCH --time=01:00:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G

set -euo pipefail

if [[ -z ${SLURM_JOB_ID:-} ]]; then
  echo 'solver batches require a SLURM allocation' >&2
  exit 2
fi

WORKDIR=${PRIMO_WORKDIR:-$PWD}
COUNT=${PRIMO_FUZZ_COUNT:-750}
FINITE_COUNT=${PRIMO_FINITE_DOMAIN_FUZZ_COUNT:-125}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202607390}
OUT_ROOT=${PRIMO_FUZZ_OUT_DIR:-slurm-results/qf_uflra-fuzz-gate}
SOLVER_ARGS_TEXT=${PRIMO_FUZZ_SOLVER_ARGS:-}
MODEL_VALIDATION=${PRIMO_FUZZ_MODEL_VALIDATION:-1}
MODEL_TIMEOUT=${PRIMO_FUZZ_MODEL_TIMEOUT:-10}
MODEL_DOLMEN_TIMEOUT=${PRIMO_FUZZ_MODEL_DOLMEN_TIMEOUT:-10}
QF_UFLRA_REQUIRE_POST_AMO=${PRIMO_FINITE_DOMAIN_QF_UFLRA_REQUIRE_POST_AMO:-1}
QF_UFLRA_ACTIVATION_PROFILE=${PRIMO_FINITE_DOMAIN_QF_UFLRA_ACTIVATION_PROFILE:-amo}
QF_UF_ACTIVATION_PROFILE=${PRIMO_FINITE_DOMAIN_QF_UF_ACTIVATION_PROFILE:-amo}
uf_activation_args=(--activation-profile "$QF_UF_ACTIVATION_PROFILE")
if [[ $QF_UF_ACTIVATION_PROFILE != answers ]]; then
  uf_activation_args+=(--require-post-amo)
fi

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$HOME/.opam/default/bin:$PATH"
export PATH

TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SEED=$((SEED_BASE + TASK_ID))
OUT="$OUT_ROOT/task-${TASK_ID}"

test ! -e "$OUT"
mkdir -p "$OUT"
sha256sum ./build-slurm-rel/primo > "$OUT/binary-before.sha256"
finish_gate() {
  status=$?
  trap - EXIT
  sha256sum ./build-slurm-rel/primo > "$OUT/binary-after.sha256" || status=1
  if ! cmp -s "$OUT/binary-before.sha256" "$OUT/binary-after.sha256"; then status=1; fi
  printf '%s\n' "$status" > "$OUT/exit-code"
  exit "$status"
}
trap finish_gate EXIT
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
if [[ -n ${PRIMO_EXPECTED_REVISION:-} ]]; then
  grep -Fx "git: $PRIMO_EXPECTED_REVISION" "$OUT/version.txt"
fi
required_tools=(z3 cvc5)
case "$MODEL_VALIDATION" in
  0) ;;
  1) required_tools+=(dolmen) ;;
  *)
    echo "PRIMO_FUZZ_MODEL_VALIDATION must be 0 or 1" >&2
    exit 2
    ;;
esac
for required in "${required_tools[@]}"; do
  if ! PATH="$WORKDIR/contrib/local/bin:$PATH" command -v "$required" >/dev/null 2>&1; then
    echo "required gate tool '$required' not found" >&2
    exit 2
  fi
  sha256sum "$(command -v "$required")" >> "$OUT/references.sha256"
done
solver_args=()
solver_arg_words=()
finite_domain_activation_args=()
case "$QF_UFLRA_REQUIRE_POST_AMO" in
  0) ;;
  1) finite_domain_activation_args+=(--require-post-amo) ;;
  *)
    echo "PRIMO_FINITE_DOMAIN_QF_UFLRA_REQUIRE_POST_AMO must be 0 or 1" >&2
    exit 2
    ;;
esac
case "$QF_UFLRA_ACTIVATION_PROFILE" in
  amo|structural|answers) ;;
  *)
    echo "PRIMO_FINITE_DOMAIN_QF_UFLRA_ACTIVATION_PROFILE must be amo, structural or answers" >&2
    exit 2
    ;;
esac
if [[ -n $SOLVER_ARGS_TEXT ]]; then
  read -r -a solver_arg_words <<< "$SOLVER_ARGS_TEXT"
  for arg in "${solver_arg_words[@]}"; do
    solver_args+=(--solver-arg="$arg")
  done
fi
# Use the same effective option vectors for generation and model replay.
finite_uf_solver_args=("${solver_args[@]}" --solver-arg=--preprocessing-mode=strong)
printf 'solver options:'
printf ' %q' "${solver_arg_words[@]}"
printf '\n'
python3 scripts/qf_uflra_smoke_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${solver_args[@]}" \
    --ref z3 \
    --count "$COUNT" \
    --seed "$SEED" \
    --timeout 5 \
    --shape mixed \
    --out "$OUT/general"

python3 scripts/finite_domain_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${finite_uf_solver_args[@]}" \
    --ref z3 \
    --ref cvc5 \
    --logic QF_UF \
    --count "$FINITE_COUNT" \
    --seed "$((SEED + 100000))" \
    --timeout 5 \
    "${uf_activation_args[@]}" \
    --out "$OUT/finite-qf-uf"

python3 scripts/finite_domain_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${solver_args[@]}" \
    --ref z3 \
    --ref cvc5 \
    --logic QF_UFLRA \
    --count "$FINITE_COUNT" \
    --seed "$((SEED + 200000))" \
    --timeout 5 \
    --activation-profile "$QF_UFLRA_ACTIVATION_PROFILE" \
    "${finite_domain_activation_args[@]}" \
    --out "$OUT/finite-qf-uflra"

if ((MODEL_VALIDATION)); then
  model_status=0
  for corpus in general finite-qf-uf finite-qf-uflra; do
    manifest="$OUT/model-validation/$corpus-sat.txt"
    python3 scripts/select_fuzz_sat_inputs.py "$OUT/$corpus/summary.csv" --out "$manifest"
    if [[ ! -s $manifest ]]; then continue; fi
    replay_solver_args=(--solver-arg=--quiet "${solver_args[@]}")
    if [[ $corpus == finite-qf-uf ]]; then
      replay_solver_args=(--solver-arg=--quiet "${finite_uf_solver_args[@]}")
    fi
    python3 scripts/validate_model.py \
        --solver ./build-slurm-rel/primo \
        "${replay_solver_args[@]}" \
        --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false \
        --timeout "$MODEL_TIMEOUT" \
        --dolmen-timeout "$MODEL_DOLMEN_TIMEOUT" \
        --out "$OUT/model-validation/$corpus" \
        --manifest "$manifest" || model_status=$?
  done
  exit "$model_status"
fi
