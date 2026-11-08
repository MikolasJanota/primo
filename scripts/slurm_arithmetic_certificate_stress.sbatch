#!/usr/bin/env bash
#SBATCH --job-name=primo-arithmetic-proof-stress
#SBATCH --array=0-1%2
#SBATCH --cpus-per-task=2
#SBATCH --mem=12G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
: "${PRIMO_WORKDIR:?prepared native workspace}"
: "${PRIMO_EXPECTED_GIT:?native binary revision}"
: "${ARITHMETIC_WORKFLOW_DIR:?frozen stress workflow}"
: "${ARITHMETIC_HISTORY_ROOT:?completed native timing records}"
: "${PRIMO_SAT_SOLVER:?CaDiCaL with textual LRAT support}"
cd "$PRIMO_WORKDIR"
reference=${ARITHMETIC_REFERENCE:-$PWD/contrib/local/bin/yices-smt2}
test -x "$reference"
logic=${ARITHMETIC_LOGIC:-}
if [[ -z $logic ]]; then
  if [[ $SLURM_ARRAY_TASK_ID == 0 ]]; then logic=QF_LRA; else logic=QF_LIA; fi
fi
case "$logic" in
  QF_LRA)
    : "${LRA_CORPUS:?existing QF_LRA source corpus}"
    : "${MICROMATH_LRA_DIR:?pinned LRA checkout/build}"
    : "${MICROMATH_LRA_GIT:?LRA revision}"
    arithmetic=LRA
    corpus=$LRA_CORPUS
    checker=$MICROMATH_LRA_DIR
    checker_git=$MICROMATH_LRA_GIT
    ;;
  QF_LIA)
    : "${LIA_CORPUS:?existing QF_LIA source corpus}"
    : "${MICROMATH_LIA_DIR:?pinned LIA checkout/build}"
    : "${MICROMATH_LIA_GIT:?LIA revision}"
    arithmetic=LIA
    corpus=$LIA_CORPUS
    checker=$MICROMATH_LIA_DIR
    checker_git=$MICROMATH_LIA_GIT
    ;;
  *) echo "unsupported arithmetic logic: $logic" >&2; exit 2 ;;
esac
artifacts=${ARITHMETIC_OUTPUT_DIR:-$PWD/results/arithmetic-proof-stress-$SLURM_ARRAY_JOB_ID/$logic}
mkdir -p "$artifacts"
build-slurm-rel/primo --version > "$artifacts/native-version.txt"
grep -Fx "git: $PRIMO_EXPECTED_GIT" "$artifacts/native-version.txt"
git -C "$checker" rev-parse HEAD > "$artifacts/micromath-git.txt"
grep -Fx "$checker_git" "$artifacts/micromath-git.txt"
test -z "$(git -C "$checker" status --porcelain --untracked-files=no)"
sha256sum "$checker/smt/$arithmetic.md" > "$artifacts/upstream-doc.sha256"
sha256sum "$ARITHMETIC_WORKFLOW_DIR/"{arithmetic_certificate_stress.py,certificate_cnf_statement.py,slurm_arithmetic_certificate_stress.sbatch,micromath_stress.py,replay_micromath_lra.py,select_micromath_fast_cases.py,check_lra_certificate.py} > "$artifacts/workflow.sha256"
lscpu > "$artifacts/cpu.txt"
python3 --version > "$artifacts/python.txt"
"$reference" --version > "$artifacts/reference-version.txt"
selection=()
if [[ -n ${ARITHMETIC_SELECTION_FILE:-} ]]; then
  selection=(--selection "$ARITHMETIC_SELECTION_FILE")
elif [[ -n ${ARITHMETIC_SELECTION_ROOT:-} ]]; then
  selection=(--selection "$ARITHMETIC_SELECTION_ROOT/$logic.json")
fi
python3 "$ARITHMETIC_WORKFLOW_DIR/arithmetic_certificate_stress.py" \
  --logic "$logic" --history "$ARITHMETIC_HISTORY_ROOT/$logic/primo" \
  --historical-git 5efbd02293e15a53fbaad748ceda78bc7d27bbac --corpus "$corpus" \
  --solver "$PWD/build-slurm-rel/primo" --reference "$reference" \
  --cadical "$PRIMO_SAT_SOLVER" --micromath "$checker" --micromath-git "$checker_git" \
  --count "${ARITHMETIC_STRESS_COUNT:-24}" --max-per-family 3 \
  --statement-source "${ARITHMETIC_STATEMENT_SOURCE:-exported-cnf}" "${selection[@]}" \
  --reference-seconds "${ARITHMETIC_REFERENCE_SECONDS:-15}" \
  --export-seconds "${ARITHMETIC_EXPORT_SECONDS:-30}" \
  --challenge-seconds "${ARITHMETIC_CHALLENGE_SECONDS:-30}" \
  --proof-seconds "${ARITHMETIC_PROOF_SECONDS:-60}" \
  --out "$artifacts/run" > "$artifacts/run.log" 2>&1
printf '%s\n' 'pilot finished; inspect summary counts, not this process status, for proof success' > "$artifacts/FINISHED"
