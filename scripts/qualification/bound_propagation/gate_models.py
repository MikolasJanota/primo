#!/usr/bin/env python3
"""Audit fresh differential rows, then independently validate their SAT models."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    out = args.root / f"gate-{args.slot}"
    with (out / "summary.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1000
    assert all(r["outcome"] in ("PASS", "TIMEOUT") for r in rows)
    selected = [
        r.get("case") or r["file"]
        for r in rows
        if r["outcome"] == "PASS" and (r.get("ours") or r.get("answer")) == "sat"
    ]
    manifest = out / "sat.txt"
    manifest.write_text("".join(f"{path}\n" for path in selected))
    flags = [
        "--quiet",
        "--lra-propagation",
        "--lra-row-propagation",
        "--lra-bidirectional-row-propagation",
        "--check-model",
    ]
    if args.slot >= 4:
        flags.append("--preprocess-bound-tightening")
    if selected:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={f}" for f in flags),
                "--manifest",
                str(manifest),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out / "models"),
            ],
            check=True,
        )
        with (out / "models/summary.csv").open() as stream:
            models = list(csv.DictReader(stream))
        assert len(models) == len(selected)
        assert all(
            r["outcome"] == "pass"
            and r["validator"] == "dolmen"
            and r["solver_exit"] == r["dolmen_exit"] == "0"
            for r in models
        )
    receipt = {
        "slot": args.slot,
        "count": len(rows),
        "seed": 2026111340 + args.slot,
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "selected_sat": len(selected),
        "direct_dolmen_pass": len(selected),
        "summary_sha256": hashlib.sha256(
            (out / "summary.csv").read_bytes()
        ).hexdigest(),
        "job": os.environ["SLURM_JOB_ID"],
    }
    (out / "coverage.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
