#!/usr/bin/env python3
"""Finish model validation from completed, durable proof or mutation receipts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    if args.slot < 6:
        number = args.slot + 1
        out = args.root / f"proof-models-{number}"
        rows = [json.loads(s) for s in (out / "proofs.jsonl").read_text().splitlines()]
        assert len(rows) == 1000
        proved = [r for r in rows if r.get("reference") == "unsat"]
        skipped = [
            r for r in rows if r.get("status") == "native-timeout-no-completed-batch"
        ]
        assert len(proved) + len(skipped) == 1000
        coverage = {
            "proofs": len(proved),
            "nonempty": sum(r["bounds"] > 0 for r in proved),
            "native_timeouts_without_completed_batch": len(skipped),
        }
        assert coverage["nonempty"] > 0
        hybrid = number in (5, 6)
    else:
        number = args.slot - 6
        out = args.root / f"mutation-replay-{number}"
        rows = [json.loads(s) for s in (out / "rows.jsonl").read_text().splitlines()]
        assert len(rows) == 25
        coverage = {"logic": "QF_LIA" if number < 4 else "QF_UFLIA", "cases": 25}
        hybrid = False
    flags = ["--quiet", "--preprocess-bound-tightening", "--check-model"]
    if hybrid:
        flags += ["--lia-branching=hybrid"]
    selected = (out / "sat.txt").read_text().splitlines()
    if selected:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={flag}" for flag in flags),
                "--manifest",
                str(out / "sat.txt"),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out / "models-final"),
            ],
            check=True,
        )
        with (out / "models-final/summary.csv").open() as stream:
            models = list(csv.DictReader(stream))
        assert len(models) == len(selected)
        assert all(
            r["outcome"] == "pass"
            and r["validator"] == "dolmen"
            and r["solver_exit"] == r["dolmen_exit"] == "0"
            for r in models
        )
        coverage["model_summary_sha256"] = hashlib.sha256(
            (out / "models-final/summary.csv").read_bytes()
        ).hexdigest()
    coverage.update(
        sat_expected=len(selected),
        direct_dolmen_pass=len(selected),
        model_stage="direct Dolmen completed",
        job=os.environ["SLURM_JOB_ID"],
    )
    (out / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\n")


if __name__ == "__main__":
    main()
