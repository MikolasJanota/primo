#!/usr/bin/env bash
#SBATCH --job-name=primo-micromath-update-build
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified native workspace}"
: "${MICROMATH_UPDATE_DIR:?isolated fetched worktree}"
: "${MICROMATH_UPDATE_GIT:?pinned update revision}"
: "${PRIMO_CXX_HEADER_MANIFEST:?provisioned compiler header roots}"
out="$PRIMO_WORKDIR/results/micromath-update-build-$SLURM_JOB_ID"
mkdir -p "$out"
cd "$MICROMATH_UPDATE_DIR"
test "$(git rev-parse HEAD)" = "$MICROMATH_UPDATE_GIT"
test -z "$(git status --porcelain --untracked-files=no)"
git show -s --format='%H%n%ci%n%s' > "$out/revision.txt"
c_headers=
while IFS= read -r root; do
  if [[ -f "$root/stdio.h" || -f "$root/bits/libc-header-start.h" || -f "$root/stdarg.h" ]]; then
    c_headers="${c_headers:+$c_headers:}$root"
  fi
done < "$PRIMO_CXX_HEADER_MANIFEST"
test -n "$c_headers"
export C_INCLUDE_PATH="$c_headers"
printf '%s\n' "$C_INCLUDE_PATH" > "$out/c-headers.txt"
sha256sum "$PRIMO_CXX_HEADER_MANIFEST" "$PRIMO_WORKDIR/contrib/local/include/gmp.h" \
  "$PRIMO_WORKDIR/contrib/local/lib/libgmp.a" > "$out/dependencies.sha256"
gcc --version > "$out/compiler.txt"
lscpu > "$out/cpu.txt"
printf '%s\n' 'make -j4 with qualified-workspace GMP CPPFLAGS/LDFLAGS; targets: build-axioms smt/build-lra-challenge smt/build-lia-challenge smt/prove-lra smt/prove-lia check-solution' > "$out/command.txt"
make clean > "$out/clean.out" 2> "$out/clean.err"
make -j4 CPPFLAGS="-I. -Isat -I$PRIMO_WORKDIR/contrib/local/include" \
  LDFLAGS="-L$PRIMO_WORKDIR/contrib/local/lib" \
  build-axioms smt/build-lra-challenge smt/build-lia-challenge \
  smt/prove-lra smt/prove-lia check-solution > "$out/build.out" 2> "$out/build.err"
sha256sum build-axioms smt/build-lra-challenge smt/build-lia-challenge \
  smt/prove-lra smt/prove-lia check-solution > "$out/binaries.sha256"
sha256sum smt/LRA.md smt/LIA.md Makefile > "$out/interface-docs.sha256"
printf '%s\n' "$SLURM_JOB_ID" > "$out/FINISHED"
