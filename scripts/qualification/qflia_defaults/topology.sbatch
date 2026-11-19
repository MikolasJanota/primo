#!/usr/bin/env bash
#SBATCH --job-name=primo-qflia-default-topology
#SBATCH --nodes=3
#SBATCH --nodelist=node-02,node-03,node-04
#SBATCH --ntasks=3
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=1G
#SBATCH --time=00:02:00
#SBATCH --output=logs/%x-%A_%a.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
mkdir -p results/topology
srun --input=none python3.11 tmp/topology.py
