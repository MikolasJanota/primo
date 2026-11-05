#!/usr/bin/env bash
#SBATCH --job-name=primo-lia-export-real
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?prepared source workspace}"
: "${PRIMO_EXPECTED_GIT:?fixed source revision}"
: "${PRIMO_HISTORY:?recorded completed native QF_LIA run}"
: "${PRIMO_CORPUS:?original QF_LIA inputs}"
: "${PRIMO_SAT_SOLVER:?independent SAT executable}"
artifacts="results/lia-export-real-$SLURM_JOB_ID"
mkdir -p "$artifacts"
build-slurm-rel/primo --version > "$artifacts/version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$artifacts/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 "$PRIMO_SAT_SOLVER" > "$artifacts/binaries.sha256"
sha256sum scripts/qualify_lia_export_real.py scripts/check_lia_certificate.py > "$artifacts/workflow.sha256"
lscpu > "$artifacts/cpu.txt"
python3 scripts/qualify_lia_export_real.py --history "$PRIMO_HISTORY" \
 --source-git 5efbd02293e15a53fbaad748ceda78bc7d27bbac --corpus "$PRIMO_CORPUS" \
 --solver "$PWD/build-slurm-rel/primo" --reference "$PWD/contrib/local/bin/z3" \
 --sat-solver "$PRIMO_SAT_SOLVER" --count 8 --out "$artifacts/replay"
