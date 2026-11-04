#!/usr/bin/env python3
"""Bounded certificate replays selected from recorded fast native LRA UNSAT results."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import time
from collections import Counter
from pathlib import Path

from check_lra_certificate import check_file
from select_micromath_fast_cases import read_history


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("history", "corpus", "solver", "reference", "sat-solver", "out"):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--source-git", required=True)
    parser.add_argument("--count", type=int, default=12)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("real-corpus certificate qualification requires SLURM")
    args.out.mkdir(parents=True, exist_ok=False)
    records = read_history(args.history, args.source_git, 1)
    candidates = []
    for row in records:
        path = args.corpus / row["file"]
        if path.is_file() and 4096 <= path.stat().st_size <= 524288:
            candidates.append({**row, "bytes": path.stat().st_size})
    candidates.sort(key=lambda row: (-row["bytes"], row["file"]))
    selected = []
    families = Counter()
    for row in candidates:
        if families[row["family"]] >= 3:
            continue
        raw = (args.corpus / row["file"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != row["sha256"]:
            raise ValueError("historical input hash mismatch")
        source = raw.decode()
        if source.count("(check-sat)") != 1 or any(
            token in source
            for token in ("(push", "(pop", "(get-", "(reset", "(set-option")
        ):
            continue
        selected.append(row)
        families[row["family"]] += 1
        if len(selected) == args.count:
            break
    if len(selected) != args.count:
        raise ValueError("insufficient compatible recorded fast LRA inputs")
    (args.out / "selection.json").write_text(json.dumps(selected, indent=2) + "\n")
    rows = []
    for index, case in enumerate(selected):
        path = args.corpus / case["file"]
        directory = args.out / f"{index:02d}"
        directory.mkdir()
        certificate = directory / "input.sexp"
        row = {"case": case, "status": "not_started"}
        command = [
            str(args.solver),
            "--quiet",
            "--stats",
            "--export-cnf",
            str(certificate),
            "--export-cnf-reduction=core",
            str(path),
        ]
        row["command"] = command
        started = time.monotonic()
        try:
            reference = subprocess.run(
                [str(args.reference), str(path)],
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            if reference.returncode or reference.stdout.strip() != "unsat":
                raise ValueError("reference did not confirm recorded UNSAT")
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=120, check=False
            )
            (directory / "solver.out").write_text(result.stdout)
            (directory / "solver.err").write_text(result.stderr)
            answers = [
                s
                for s in result.stdout.splitlines()
                if s in ("sat", "unsat", "unknown")
            ]
            if result.returncode or answers != ["unsat"]:
                raise ValueError(
                    f"export not UNSAT: {result.returncode}, {answers}, {result.stderr[-1000:]}"
                )
            row["certificate_bytes"] = certificate.stat().st_size
            row["certificate_sha256"] = hashlib.sha256(
                certificate.read_bytes()
            ).hexdigest()
            row["check"] = check_file(certificate, args.sat_solver)
            row["status"] = "verified"
        except subprocess.TimeoutExpired as error:
            row["status"] = "timeout"
            row["error"] = str(error)
        except (ValueError, OSError) as error:
            row["status"] = "error"
            row["error"] = str(error)
        row["seconds"] = time.monotonic() - started
        rows.append(row)
        report = {
            "job_id": os.environ["SLURM_JOB_ID"],
            "historical_source_git": args.source_git,
            "selection": "recorded native UNSAT<=1s;4096..524288bytes;max3/family;largest first;single-query lexical command screen",
            "limits_seconds": {
                "reference": 30,
                "export": 120,
                "propositional_check": 60,
            },
            "counts": dict(Counter(r["status"] for r in rows)),
            "expected": args.count,
            "completed": len(rows),
            "rows": rows,
        }
        (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
        if row["status"] == "error":
            raise SystemExit(1)


if __name__ == "__main__":
    main()
