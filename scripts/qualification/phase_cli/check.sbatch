#!/usr/bin/env bash
#SBATCH --job-name=primo-phase-cli-check
#SBATCH --partition=compute
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4
out=${PRIMO_PHASE_RENAME_OUT:?}
mkdir -p "$out"
exec > >(tee "$out/check.log") 2>&1
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/identities.sha256"
python3 - <<'PY'
from pathlib import Path
import subprocess
solver=str(Path('build-slurm-rel/primo').resolve())
text=subprocess.check_output([solver,'--help'],text=True).replace(solver+' [OPTIONS]','primo [OPTIONS]',1)
Path('docs/examples/primo-help.txt').write_text('\n'.join(line.rstrip() for line in text.splitlines()).rstrip()+'\n')
PY
cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for root in ("src", "tests", "docs/examples") for p in Path(root).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
make -o contrib-required check BUILD_DIR=build-slurm-rel RUFF="$PWD/tmp/qualification-tools/bin/python -m ruff" CPP_FORMAT_FILES="$cpp_files"
python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo
for mode in auto on off; do
  case $mode in auto) count=334; seed=2026110501;; on) count=333; seed=2026110502;; off) count=333; seed=2026110503;; esac
  python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver build-slurm-rel/primo \
    --solver-arg=--quiet --solver-arg="--arithmetic-model-phase=$mode" \
    --solver-arg=--initial-theory-phase=negative \
    --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
    --count "$count" --seed "$seed" --timeout 5 --dolmen-timeout 10 --out "$out/fuzz-$mode"
done
