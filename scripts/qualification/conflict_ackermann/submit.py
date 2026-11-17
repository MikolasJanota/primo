#!/usr/bin/env python3
"""Preflight, hold, verify and release one bounded experiment array."""

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
    assert a.count > 0 and not a.receipt.exists()
    ssh = ["ssh", "-F", str(a.ssh_config), "-o", "ControlPath=none", a.remote]
    if a.after:
        info = run([*ssh, "scontrol show job " + a.after])
        assert re.search(r"\bArrayJobId=" + a.after + r"\b", info)
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
    flags = [
        "sbatch",
        "--parsable",
        "--hold",
        "--exclude=node-01",
        f"--array=0-{a.count - 1}%{min(a.count, a.concurrency)}",
        "--time=" + a.job_time,
    ]
    if a.after:
        flags.append("--dependency=afterok:" + a.after)
    flags.append(a.batch_script)
    command = "\n".join(
        [
            "set -eu",
            ". " + shlex.quote(a.environment),
            *["export " + k + "=" + shlex.quote(v) for k, v in a.exports.items()],
            shlex.join(flags),
        ]
    )
    job = run([*ssh, command]).split(";", 1)[0]
    assert re.fullmatch("[0-9]+", job)
    try:
        info = run([*ssh, "scontrol show job " + job])
        dependency = re.search(r"\bDependency=(\S+)", info)
        assert (
            re.search(r"\bArrayJobId=" + job + r"\b", info)
            and "Reason=JobHeldUser" in info
        )
        assert dependency and re.findall(r"afterok:([0-9]+)", dependency[1]) == (
            [a.after] if a.after else []
        )
        a.receipt.parent.mkdir(parents=True, exist_ok=True)
        receipt = {
            "state": "held",
            "job": job,
            "parent": a.after,
            "count": a.count,
            "phase": a.phase,
        }
        a.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
        run([*ssh, "scontrol release " + job])
        receipt["state"] = "released"
        a.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    except BaseException:
        run([*ssh, "scancel " + job])
        raise
    return job


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for x in ("ssh-config", "snapshot", "receipt"):
        p.add_argument("--" + x, type=Path, required=True)
    for x in ("remote", "workdir", "environment", "batch-script", "phase"):
        p.add_argument("--" + x, required=True)
    p.add_argument("--exports-json", type=Path)
    p.add_argument("--count", type=int, required=True)
    p.add_argument("--concurrency", type=int, default=16)
    p.add_argument("--job-time", default="00:30:00")
    p.add_argument("--after")
    a = p.parse_args()
    a.exports = json.loads(a.exports_json.read_text()) if a.exports_json else {}
    assert all(
        re.fullmatch("[A-Z][A-Z0-9_]*", k) and isinstance(v, str)
        for k, v in a.exports.items()
    )
    print(json.dumps({"job": submit(a)}))


if __name__ == "__main__":
    main()
