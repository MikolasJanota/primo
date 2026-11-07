#!/usr/bin/env bash
#SBATCH --job-name=primo-uflia-equality-profile
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=6G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
export DEBUGINFOD_URLS=''
out=${PRIMO_EQ_OUT:?}
test ! -e "$out"
mkdir -p "$out"
export PRIMO_PROFILE_CASE="${PRIMO_CORPUS_ROOT:?}/wisas/xs_37_57.smt2"
export PRIMO_PROFILE_FLAGS='--quiet --stats --arithmetic-model-phase=off --no-lia-round-integer-bounds --no-lia-primitive-int-rows --no-lia-integer-slacks --no-lia-patch-integral-moves'
sha256sum "$PRIMO_PROFILE_CASE" > "$out/input.sha256"
gdb --version > "$out/gdb-version.txt"
for role in baseline candidate; do
 case $role in baseline) export PRIMO_PROFILE_BINARY="${PRIMO_BASELINE_SOLVER:?}";; candidate) export PRIMO_PROFILE_BINARY="$PWD/build-slurm-rel/primo";; esac
 export PRIMO_PROFILE_JSON="$out/$role.json"
 "$PRIMO_PROFILE_BINARY" --version > "$out/$role-version.txt"
 sha256sum "$PRIMO_PROFILE_BINARY" > "$out/$role-binary.sha256"
 timeout --kill-after=5s 60s gdb -q -batch -x "${PRIMO_EQ_GDB_SCRIPT:-scripts/qualification/uflia_equalities/sample.gdb}" > "$out/$role.log" 2>&1
done
