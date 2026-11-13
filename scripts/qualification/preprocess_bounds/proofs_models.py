#!/usr/bin/env python3
"""Independently refute emitted batches and replay SAT models after fuzzing."""

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
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    source = args.root / f"fuzz-{args.slot}"
    rows = list(csv.DictReader((source / "summary.csv").open()))
    assert len(rows) == 1000
    out = (
        args.root
        / f"{os.environ.get('PRIMO_BOUND_PROOF_PREFIX', 'proof-models')}-{args.slot}"
    )
    out.mkdir(exist_ok=False)
    flags = ["--quiet", "--preprocess-bound-tightening", "--check-model"]
    if args.slot in (5, 6):
        flags += ["--lia-branching=hybrid"]
    sat = []
    proved = 0
    nonempty = 0
    skipped = 0
    with (out / "proofs.jsonl").open("w") as stream:
        for index, row in enumerate(rows):
            path = Path(row.get("file") or row["case"])
            if row.get("ours") == "TIMEOUT":
                skipped += 1
                stream.write(
                    json.dumps(
                        {
                            "case": str(path),
                            "status": "native-timeout-no-completed-batch",
                        }
                    )
                    + "\n"
                )
                stream.flush()
                continue
            target = out / f"{index:04d}.smt2"
            with target.open("w") as output:
                run = subprocess.run(
                    [
                        os.environ.get(
                            "PRIMO_BOUND_PROBE",
                            "build-slurm-rel/primo_preprocess_bound_probe",
                        ),
                        str(path),
                    ],
                    stdout=output,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=30,
                    check=False,
                )
            assert run.returncode == 0, (path, run.stderr)
            check = subprocess.run(
                ["contrib/local/bin/z3", str(target)],
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            assert check.returncode == 0 and check.stdout.strip() == "unsat", (
                path,
                check,
            )
            amount = int(run.stderr.split()[1])
            nonempty += amount > 0
            proved += 1
            stream.write(
                json.dumps(
                    {
                        "case": str(path),
                        "input_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "bounds": amount,
                        "reference": "unsat",
                        "probe_sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                    }
                )
                + "\n"
            )
            stream.flush()
            if (row.get("ours") or row.get("answer")) == "sat" and row[
                "outcome"
            ] == "PASS":
                sat.append(path)
    assert nonempty > 0, "gate never exercised a nonempty emitted batch"
    manifest = out / "sat.txt"
    manifest.write_text("".join(str(p) + "\n" for p in sat))
    if sat:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={flag}" for flag in flags),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--manifest",
                str(manifest),
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out / "models"),
            ],
            check=True,
        )
    (out / "coverage.json").write_text(
        json.dumps(
            {
                "proofs": proved,
                "native_timeouts_without_completed_batch": skipped,
                "nonempty": nonempty,
                "sat_expected": len(sat),
                "model_stage": "direct Dolmen completed",
                "job": os.environ["SLURM_JOB_ID"],
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
