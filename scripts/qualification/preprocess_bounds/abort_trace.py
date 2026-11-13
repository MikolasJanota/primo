#!/usr/bin/env python3
"""Capture the first exception in the resource-limited UFLRA abort case."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import resource
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    case = config["cases"][653]
    source = Path(config["roots"][case["logic"]]) / case["file"]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == case["sha256"]
    args.out.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_AS, (config["address_space_bytes"],) * 2)
    receipts = []
    for label, profile in config["profiles"].items():
        assert (
            hashlib.sha256(Path(profile["solver"]).read_bytes()).hexdigest()
            == profile["sha256"]
        )
        command = [
            "timeout",
            "--signal=INT",
            "--kill-after=5",
            "150",
            "gdb",
            "-nx",
            "--batch",
            "-iex",
            "set debuginfod enabled off",
            "-ex",
            "set confirm off",
            "-ex",
            "catch throw",
            "-ex",
            "run",
            "-ex",
            "bt 25",
            "-ex",
            "continue",
            "-ex",
            "bt 25",
            "-ex",
            "kill",
            "--args",
            profile["solver"],
            *profile["args"],
            str(source),
        ]
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=160,
            env={**os.environ, "DEBUGINFOD_URLS": ""},
            check=False,
        )
        for kind in ("stdout", "stderr"):
            (args.out / f"{label}.{kind}").write_text(getattr(result, kind))
        assert (
            "SIGABRT" in result.stdout
            and "disconnect_external_propagator" in result.stdout
        )
        receipts.append(
            {"profile": label, "command": command, "exit_code": result.returncode}
        )
    (args.out / "receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")


if __name__ == "__main__":
    main()
