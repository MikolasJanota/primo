#!/usr/bin/env bash
# Independent printed-model checks of the regression and original failing seed.
#SBATCH --job-name=primo-model-repair-repro
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:08:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
out=${PRIMO_REPAIR_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
 --solver-arg=--preprocessing-mode=strong --solver-arg=--check-model \
 --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false --timeout 180 \
 --dolmen-timeout 60 --out "$out/models" \
 benchmarks/regressions/qf_uflia_finite_domain_eliminated_argument.smt2 \
 "${PRIMO_REPAIR_INPUT:?}"
