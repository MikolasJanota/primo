#!/usr/bin/env python3
"""Bounded preprocessing and debugger samples for paired-run regressions."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / "src").is_dir())
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--slots", type=int, nargs="+", required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    args.out.mkdir(parents=True, exist_ok=False)
    receipts = []
    for slot in args.slots:
        case = config["cases"][slot]
        source = Path(config["roots"][case["logic"]]) / case["file"]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == case["sha256"]
        for label, profile in config["profiles"].items():
            assert (
                hashlib.sha256(Path(profile["solver"]).read_bytes()).hexdigest()
                == profile["sha256"]
            )
            command = [profile["solver"], *profile["args"]]
            row = run_solver([*command, "--preprocess-only"], source, 10)
            for kind in ("stdout", "stderr"):
                (args.out / f"{slot}-{label}-preprocess.{kind}").write_text(
                    str(row.pop(kind))
                )
            assert row["answer"] != "unsat", "Previously validated SAT case refuted"
            debugger = [
                "timeout",
                "--signal=INT",
                "--kill-after=5",
                "15",
                "gdb",
                "-nx",
                "--batch",
                "-iex",
                "set debuginfod enabled off",
                "-ex",
                "set pagination off",
                "-ex",
                "set confirm off",
                "-ex",
                "handle SIGINT stop print nopass",
                "-ex",
                "run",
                "-ex",
                "bt",
                "-ex",
                "kill",
                "--args",
                *command,
                str(source),
            ]
            sample = subprocess.run(
                debugger,
                capture_output=True,
                text=True,
                timeout=25,
                check=False,
                env={**os.environ, "DEBUGINFOD_URLS": ""},
            )
            for kind in ("stdout", "stderr"):
                (args.out / f"{slot}-{label}-gdb.{kind}").write_text(
                    getattr(sample, kind)
                )
            assert (
                any(line.startswith("#") for line in sample.stdout.splitlines())
                or "exited normally" in sample.stdout
            ), sample.stderr
            receipts.append(
                {
                    "slot": slot,
                    "profile": label,
                    "preprocess": row,
                    "debugger_command": debugger,
                    "debugger_exit": sample.returncode,
                }
            )
    (args.out / "receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")


if __name__ == "__main__":
    main()
