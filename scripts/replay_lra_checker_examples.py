#!/usr/bin/env python3
"""Export tiny UNSAT examples in SLURM and check arithmetic and Boolean refutation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

from check_lra_certificate import check_file


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, timeout=10):
    return subprocess.run(
        list(map(str, command)),
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("inputs", "out", "solver", "reference", "sat-solver"):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--producer-git", required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("example export batches require SLURM")
    version = run([args.solver, "--version"])
    match = re.search(r"(?m)^git: ([0-9a-f]+)$", version.stdout)
    if version.returncode or not match or not args.producer_git.startswith(match[1]):
        parser.error("producer revision does not match the prepared binary")
    sources = sorted(args.inputs.glob("*.smt2"))
    if not sources:
        parser.error("no example inputs")
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "certificates").mkdir()
    (args.out / "inputs").mkdir()
    logs = args.out / "logs"
    logs.mkdir()
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "producer_git": args.producer_git,
        "version": version.stdout,
        "binary_sha256": {
            name: sha256(path)
            for name, path in (
                ("primo", args.solver),
                ("z3", args.reference),
                ("cadical", args.sat_solver),
            )
        },
        "workflow_sha256": {
            path.name: sha256(path)
            for path in (
                Path(__file__),
                Path(__file__).with_name("check_lra_certificate.py"),
            )
        },
        "limits_seconds": {"reference": 10, "export": 10, "propositional_check": 60},
        "producer_options": [
            "--quiet",
            "--stats",
            "--preprocessing-mode=off",
            "--export-cnf-reduction=core",
        ],
        "rows": [],
    }
    for source in sources:
        target = args.out / "certificates" / (source.stem + ".sexp")
        copied = args.out / "inputs" / source.name
        shutil.copyfile(source, copied)
        reference = run([args.reference, copied])
        if reference.returncode or reference.stdout.strip() != "unsat":
            raise ValueError(
                f"reference did not confirm UNSAT: {source.name}: {reference}"
            )
        command = [
            args.solver,
            *report["producer_options"],
            "--export-cnf",
            target,
            copied,
        ]
        result = run(command)
        (logs / (source.stem + ".out")).write_text(result.stdout)
        (logs / (source.stem + ".err")).write_text(result.stderr)
        if result.returncode or result.stdout.strip() != "unsat":
            raise ValueError(f"export did not complete: {source.name}: {result}")
        check = check_file(target, args.sat_solver)
        if target.stat().st_size > 4096:
            raise ValueError(
                f"example exceeds the 4KiB certificate budget: {source.name}"
            )
        if source.stem != "00-boolean" and check["conflicts"] == 0:
            raise ValueError(f"example lost its arithmetic conflict: {source.name}")
        statistics = {
            key: int(value)
            for key, value in re.findall(r"(?m)^(:export[\w.-]+) (\d+)$", result.stderr)
        }
        report["rows"].append(
            {
                "name": source.stem,
                "input_bytes": copied.stat().st_size,
                "input_sha256": sha256(copied),
                "certificate_bytes": target.stat().st_size,
                "certificate_sha256": sha256(target),
                "check": check,
                "statistics": statistics,
            }
        )
        (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
