#!/usr/bin/env python3
"""Selected SLURM placement comparisons; wall times are development diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import resource
import subprocess
import sys
from pathlib import Path

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    case = config["cases"][args.slot]
    source = Path(config["roots"][case["logic"]]) / case["file"]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == case["sha256"]
    out = Path(config["out"]) / str(case["slot"])
    out.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_AS, (8 * 1024**3,) * 2)
    variants = [
        (binary, profile)
        for binary in config["binaries"]
        for profile in ("rows", "both")
    ]
    flags = [
        "--quiet",
        "--stats",
        "--lra-propagation",
        "--lra-row-propagation",
        "--lra-bidirectional-row-propagation",
    ]
    for repetition in range(2):
        for offset in range(len(variants)):
            label, profile = variants[(offset + repetition) % len(variants)]
            binary = config["binaries"][label]
            assert (
                hashlib.sha256(Path(binary["path"]).read_bytes()).hexdigest()
                == binary["sha256"]
            )
            options = [
                *flags,
                *(["--preprocess-bound-tightening"] if profile == "both" else []),
            ]
            row = run_solver([binary["path"], *options], source, 120)
            name = f"{label}-{profile}-{repetition}"
            for kind in ("stdout", "stderr"):
                (out / f"{name}.{kind}").write_text(row.pop(kind))
            row.update(
                variant=label,
                profile=profile,
                repetition=repetition,
                args=options,
                input_sha256=case["sha256"],
                binary_sha256=binary["sha256"],
                job=os.environ["SLURM_JOB_ID"],
            )
            with (out / "rows.jsonl").open("a") as stream:
                stream.write(json.dumps(row) + "\n")
            assert row["status"] in ("ok", "timeout"), row
            assert row["answer"] != "unsat", row
            if row["status"] == "ok" and row["answer"] == "sat":
                subprocess.run(
                    [
                        sys.executable,
                        "scripts/validate_model.py",
                        "--solver",
                        binary["path"],
                        *(f"--solver-arg={f}" for f in options),
                        "--solver-arg=--check-model",
                        "--expect-sat",
                        "--require-dolmen-pass",
                        "--dolmen-arg=--strict=false",
                        "--timeout",
                        "180",
                        "--dolmen-timeout",
                        "30",
                        "--out",
                        str(out / "models" / name),
                        str(source),
                    ],
                    check=True,
                )
            print(
                case["slot"],
                name,
                row["status"],
                row["answer"],
                row["seconds"],
                flush=True,
            )


if __name__ == "__main__":
    main()
