#!/usr/bin/env bash
#SBATCH --job-name=primo-uniform120-crash
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=10G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
sha256sum results/uniform120-20261109/run1/bin/primo tmp/uniform120-corpora/QF_LIA/nec-smt/large/getoption_user/prp-42-49.smt2
export DEBUGINFOD_URLS=''
exec timeout --signal=TERM --kill-after=10s 240s stdbuf -oL -eL gdb -iex 'set debuginfod enabled off' -batch -x scripts/qualification/uniform120/crash.gdb --args results/uniform120-20261109/run1/bin/primo --quiet tmp/uniform120-corpora/QF_LIA/nec-smt/large/getoption_user/prp-42-49.smt2
