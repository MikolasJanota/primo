#!/usr/bin/env bash
#SBATCH --job-name=primo-micromath-lia
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Reuse a preflighted native producer; build this external checker only in SLURM.
set -euo pipefail
: "${PRIMO_WORKDIR:?prepared Primo workspace}"
: "${PRIMO_EXPECTED_GIT:?native producer revision}"
: "${MICROMATH_DIR:?separate pinned external checkout}"
: "${MICROMATH_GIT:?full external revision}"
: "${LIA_WORKFLOW_DIR:?directory containing this workflow and Primo helpers}"
: "${PRIMO_SAT_SOLVER:?CaDiCaL with textual LIAT support}"
: "${PRIMO_CXX_HEADER_MANIFEST:?provisioned compiler header roots}"
cd "$PRIMO_WORKDIR"
artifacts="$PWD/results/micromath-lia-$SLURM_JOB_ID"
mkdir -p "$artifacts"
build-slurm-rel/primo --version > "$artifacts/primo-version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$artifacts/primo-version.txt"
git -C "$MICROMATH_DIR" rev-parse HEAD > "$artifacts/micromath-git.txt"
grep -Fx "$MICROMATH_GIT" "$artifacts/micromath-git.txt"
test -z "$(git -C "$MICROMATH_DIR" status --porcelain --untracked-files=no)"
sha256sum "$MICROMATH_DIR/smt/LIA.md" > "$artifacts/upstream-doc.sha256"
sha256sum "$LIA_WORKFLOW_DIR/"{replay_micromath_lia.py,replay_micromath_lra.py,slurm_micromath_lia_check.sbatch,micromath_stress.py,check_lra_certificate.py} > "$artifacts/workflows.sha256"
"$PRIMO_SAT_SOLVER" --version > "$artifacts/cadical-version.txt"
sha256sum "$PRIMO_SAT_SOLVER" build-slurm-rel/primo > "$artifacts/input-binaries.sha256"
lscpu > "$artifacts/cpu.txt"
gcc --version > "$artifacts/compiler.txt"
c_header_paths=
while IFS= read -r header_root; do
  if [[ -f "$header_root/stdio.h" || -f "$header_root/bits/libc-header-start.h" || -f "$header_root/stdarg.h" ]]; then
    c_header_paths="${c_header_paths:+$c_header_paths:}$header_root"
  fi
done < "$PRIMO_CXX_HEADER_MANIFEST"
test -n "$c_header_paths"
export C_INCLUDE_PATH="$c_header_paths"
printf '%s\n' "$C_INCLUDE_PATH" > "$artifacts/c-headers.txt"
sha256sum "$PRIMO_CXX_HEADER_MANIFEST" > "$artifacts/header-manifest.sha256"
if [[ ${MICROMATH_REUSE_BUILD:-0} == 1 ]]; then
  for binary in build-axioms smt/build-lia-challenge smt/prove-lia print-dump check-solution; do
    test -x "$MICROMATH_DIR/$binary"
  done
  printf '%s\n' 'reused pinned external build' > "$artifacts/build.log"
else
  make -C "$MICROMATH_DIR" clean > "$artifacts/clean.log" 2>&1
  make -C "$MICROMATH_DIR" -j"${SLURM_CPUS_PER_TASK:-2}" \
    CPPFLAGS="-I. -Isat -I$PWD/contrib/local/include" LDFLAGS="-L$PWD/contrib/local/lib" \
    build-axioms smt/build-lia-challenge smt/prove-lia print-dump check-solution \
    > "$artifacts/build.log" 2>&1
fi
compatibility=()
if [[ ${MICROMATH_LIA_V2_ENVELOPE:-0} == 1 ]]; then compatibility=(--micromath-v2-envelope); fi
python3 "$LIA_WORKFLOW_DIR/replay_micromath_lia.py" \
  --micromath "$MICROMATH_DIR" --micromath-git "$MICROMATH_GIT" \
  --examples "$LIA_WORKFLOW_DIR/examples" \
  --cadical "$PRIMO_SAT_SOLVER" --stage-seconds 60 --out "$artifacts/replay" "${compatibility[@]}" \
  > "$artifacts/replay.log" 2>&1
printf '%s\n' 'selected LIA source/certificate proofs and controls checked' > "$artifacts/PASSED"
