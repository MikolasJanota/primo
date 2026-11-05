#!/usr/bin/env bash
#SBATCH --job-name=primo-uniform-phase-gate
#SBATCH --partition=compute
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
unset PRIMO_PHASE_PROBE PRIMO_PHASE_PROBE_K PRIMO_PHASE_PROBE_WINDOW PRIMO_PHASE_PROBE_TRACE || true
out=${PRIMO_UNIFORM_GATE_OUT:?}
mkdir -p "$out"
build-slurm-rel/primo --version > "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 > "$out/identities.sha256"
for value in 0 1; do
 python3 scripts/qf_uflia_fuzz.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--lia-round-integer-bounds --solver-arg=--lia-primitive-int-rows \
  --solver-arg=--no-lia-integer-slacks --solver-arg=--lia-patch-integral-moves \
  --solver-arg=--arithmetic-model-phase=off --solver-arg=--cadical-option=forcephase=1 \
  --solver-arg="--cadical-option=phase=$value" --ref z3 --count 1000 \
  --seed "$((2026110520 + value))" --shape mixed --depth 3 --timeout 5 \
  --out "$out/value-$value" > "$out/value-$value.log" 2>&1
done
