#!/usr/bin/env python3
"""Hold, preflight, verify and release the frozen runner's qualification arrays."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
from pathlib import Path


def execute(command):
    return subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout.strip()


def submit(a, run=execute):
    assert not a.receipt.exists()
    ssh = ["ssh", "-F", str(a.ssh_config), "-o", "ControlPath=none", a.remote]
    jobs = []
    records = []
    try:
        for phase in ("seeds", "generated", "mutations"):
            run(
                [
                    "python3",
                    "scripts/slurm_prepare_benchmark_workspace.py",
                    "--check-only",
                    "--local-dir",
                    str(a.snapshot),
                    "--remote-dir",
                    a.workdir,
                    "--remote",
                    a.remote,
                    "--ssh-config",
                    str(a.ssh_config),
                ]
            )
            parent = jobs[0] if jobs else None
            flags = [
                "sbatch",
                "--parsable",
                "--hold",
                "--exclude=node-01",
                "--array=0-31%32",
                "--time=00:45:00",
            ]
            if parent:
                flags.append("--dependency=afterok:" + parent)
            flags.append("scripts/qualification/arithmetic_combinations/qualify.sbatch")
            command = "\n".join(
                [
                    "set -eu",
                    ". " + shlex.quote(a.environment),
                    "export PRIMO_COMB_PHASE=" + phase,
                    "export PRIMO_COMB_CAMPAIGN=" + shlex.quote(a.campaign),
                    shlex.join(flags),
                ]
            )
            job = run([*ssh, command]).split(";", 1)[0]
            assert re.fullmatch("[0-9]+", job)
            jobs.append(job)
            info = run([*ssh, "scontrol show job " + job])
            assert (
                re.search(r"\bArrayJobId=" + job + r"\b", info)
                and "Reason=JobHeldUser" in info
            )
            dep = re.search(r"\bDependency=(\S+)", info)
            assert dep and re.findall(r"afterok:([0-9]+)", dep[1]) == (
                [parent] if parent else []
            )
            records.append({"phase": phase, "job": job, "parent": parent})
            a.receipt.write_text(
                json.dumps({"state": "held", "jobs": records}, indent=2) + "\n"
            )
        for job in jobs:
            run([*ssh, "scontrol release " + job])
        a.receipt.write_text(
            json.dumps({"state": "released", "jobs": records}, indent=2) + "\n"
        )
    except BaseException:
        if jobs:
            run([*ssh, "scancel " + " ".join(jobs)])
        raise
    return jobs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for x in ("ssh-config", "snapshot", "receipt"):
        p.add_argument("--" + x, type=Path, required=True)
    for x in ("remote", "workdir", "environment", "campaign"):
        p.add_argument("--" + x, required=True)
    a = p.parse_args()
    print(json.dumps({"jobs": submit(a)}))


if __name__ == "__main__":
    main()
