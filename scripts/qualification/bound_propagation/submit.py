#!/usr/bin/env python3
"""Submit held replication arrays, verify parent dependencies, then release them."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path


def execute(command: list[str]) -> str:
    return subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout.strip()


def submit(args: argparse.Namespace, run=execute) -> list[str]:
    ssh = ["ssh", "-F", str(args.ssh_config), args.remote]
    jobs = []
    records = []
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    assert not args.receipt.exists(), "Preserve existing submission receipts"
    if args.after:
        info = run([*ssh, "scontrol show job " + args.after])
        assert re.search(r"\bArrayJobId=" + args.after + r"\b", info), (
            "Dependency must be a whole array, not a child task"
        )
    try:
        for repetition in range(3):
            run(
                [
                    sys.executable,
                    "scripts/slurm_prepare_benchmark_workspace.py",
                    "--check-only",
                    "--local-dir",
                    str(args.snapshot),
                    "--remote-dir",
                    args.workdir,
                    "--remote",
                    args.remote,
                    "--ssh-config",
                    str(args.ssh_config),
                ]
            )
            parent = jobs[-1] if args.phase == "timing" and jobs else args.after
            flags = [
                "sbatch",
                "--parsable",
                "--hold",
                "--exclude=node-01",
                f"--array=0-696%{args.concurrency}",
                "--time="
                + (
                    getattr(args, "job_time", None)
                    or ("00:10:00" if args.phase == "timing" else "00:20:00")
                ),
            ]
            if parent:
                flags.append(f"--dependency=afterok:{parent}")
            flags.append("scripts/qualification/bound_propagation/paired.sbatch")
            command = "\n".join(
                [
                    "set -eu",
                    ". " + shlex.quote(args.environment),
                    "export PRIMO_PROP_PHASE=" + shlex.quote(args.phase),
                    "export PRIMO_PROP_CONFIG="
                    + shlex.quote(f"{args.configs}/config-{repetition}.json"),
                    shlex.join(flags),
                ]
            )
            job = run([*ssh, command]).split(";", 1)[0]
            assert re.fullmatch(r"[0-9]+", job), job
            jobs.append(job)
            info = run([*ssh, "scontrol show job " + job])
            assert re.search(r"\bArrayJobId=" + job + r"\b", info), info
            dependency = re.search(r"\bDependency=(\S+)", info)
            assert dependency is not None, info
            parents = re.findall(r"afterok:([0-9]+)", dependency[1])
            assert parents == ([parent] if parent else []), (job, parent, dependency[1])
            assert "JobState=PENDING" in info and "Reason=JobHeldUser" in info, info
            records.append(
                {"repetition": repetition, "job": job, "verified_parent": parent}
            )
            args.receipt.write_text(
                json.dumps({"state": "held", "jobs": records}, indent=2) + "\n"
            )
        # No replication can run until every array and dependency has been checked.
        for job in jobs:
            run([*ssh, "scontrol release " + job])
        args.receipt.write_text(
            json.dumps(
                {"state": "released", "phase": args.phase, "jobs": records}, indent=2
            )
            + "\n"
        )
    except BaseException:
        if jobs:
            run([*ssh, "scancel " + " ".join(jobs)])
        raise
    return jobs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssh-config", type=Path, required=True)
    parser.add_argument("--remote", required=True)
    parser.add_argument("--workdir", required=True)
    parser.add_argument("--environment", required=True)
    parser.add_argument("--configs", required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--phase", choices=("timing", "models"), required=True)
    parser.add_argument(
        "--after", help="Whole timing-array ID; required for model replay"
    )
    parser.add_argument("--concurrency", type=int, default=32)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument(
        "--job-time",
        help="SLURM HH:MM:SS allocation limit; solver limits remain in config",
    )
    args = parser.parse_args()
    assert args.job_time is None or re.fullmatch(
        r"\d{2}:[0-5]\d:[0-5]\d", args.job_time
    )
    assert args.phase != "models" or args.after
    assert args.after is None or re.fullmatch(r"[0-9]+", args.after)
    print(json.dumps({"jobs": submit(args)}))


if __name__ == "__main__":
    main()
