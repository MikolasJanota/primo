#!/usr/bin/env python3
"""Bounded single-input isolation; preserve all outcomes, separately validate SAT."""

from __future__ import annotations

import argparse
import json
import os
import resource
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("PRIMO_WORKDIR", Path(__file__).resolve().parents[3]))
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402
from scripts.qualification.bound_propagation.analyze import sha, solved  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    source, out = Path(config["input"]), Path(config["out"])
    assert sha(source) == config["input_sha256"]
    out.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_AS, (8589934592,) * 2)
    rows = []
    with (out / "rows.jsonl").open("w") as stream:
        for label, profile in config["profiles"].items():
            assert sha(Path(profile["solver"])) == profile["sha256"]
            row = run_solver([profile["solver"], *profile["args"]], source, 30)
            for kind in ("stdout", "stderr"):
                (out / f"{label}.{kind}").write_text(str(row.pop(kind)))
            row.update(
                profile=label, args=profile["args"], binary_sha256=profile["sha256"]
            )
            rows.append(row)
            stream.write(json.dumps(row) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
            print(label, row["status"], row["answer"], row["seconds"], flush=True)
    for row in rows:
        if not solved(row) or row["answer"] != "sat":
            continue
        profile = config["profiles"][row["profile"]]
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                profile["solver"],
                *(f"--solver-arg={arg}" for arg in profile["args"]),
                "--solver-arg=--check-model",
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "180",
                "--dolmen-timeout",
                "60",
                "--out",
                str(out / "models" / row["profile"]),
                str(source),
            ],
            check=True,
        )
    (out / "complete.json").write_text(
        json.dumps(
            {
                "job": os.environ["SLURM_JOB_ID"],
                "input_sha256": sha(source),
                "config_sha256": sha(args.config),
                "method_sha256": sha(Path(__file__)),
                "outcomes": [
                    {
                        k: r[k]
                        for k in ("profile", "status", "answer", "exit_code", "seconds")
                    }
                    for r in rows
                ],
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
