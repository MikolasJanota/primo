#!/usr/bin/env python3
"""Independently replay every successful SAT diagnostic row, preserving timing data."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ENV_KEYS = ("PRIMO_PHASE_PROBE", "PRIMO_PHASE_PROBE_K", "PRIMO_PHASE_PROBE_WINDOW",
            "PRIMO_PHASE_PROBE_TRACE", "PRIMO_REGION_PHASE", "PRIMO_REGION_SEED", "PRIMO_REGION_RATE")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("fresh output required")
    args.out.mkdir(parents=True)
    provenance = json.loads((args.parent / "provenance.json").read_text())
    solver = args.solver.resolve()
    if hashlib.sha256(solver.read_bytes()).hexdigest() != provenance["solver_sha256"]:
        raise SystemExit("binary changed after timing")
    groups: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for line in (args.parent / "rows.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row["status"] == "ok" and row["answer"] == "sat" and row["exit_code"] == 0:
            path = args.root / row["file"]
            if hashlib.sha256(path.read_bytes()).hexdigest() != provenance["inputs"][row["file"]]:
                raise SystemExit("input changed after timing")
            groups[row["profile"], row["round"]].append(row)
    coverage: dict = {"parent": str(args.parent), "solver_sha256": provenance["solver_sha256"],
                      "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      "expected": sum(map(len, groups.values())), "passed": 0,
                      "groups": [], "status": "running"}
    coverage_path = args.out / "coverage.json"
    coverage_path.write_text(json.dumps(coverage, indent=2) + "\n")
    for (profile, round_index), rows in groups.items():
        out = args.out / f"r{round_index}-{profile}"
        out.mkdir()
        for key in ENV_KEYS:
            os.environ.pop(key, None)
        os.environ.update(rows[0]["env"])
        flags = rows[0]["command"][1:-1]
        if any(row["env"] != rows[0]["env"] or row["command"][1:-1] != flags for row in rows):
            raise SystemExit("inconsistent profile group")
        command = [sys.executable, "scripts/validate_model.py", "--solver", str(solver),
                   *[f"--solver-arg={flag}" for flag in flags], "--solver-arg=--check-model",
                   "--expect-sat", "--require-dolmen-pass", "--timeout", "60",
                   "--dolmen-timeout", "60", "--dolmen-arg=--strict=false",
                   "--out", str(out / "models"),
                   *[str(args.root / row["file"]) for row in rows]]
        (out / "command.json").write_text(json.dumps({"command": command, "env": rows[0]["env"]}, indent=2) + "\n")
        with (out / "driver.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        with (out / "models/summary.csv").open() as handle:
            model_rows = list(csv.DictReader(handle))
        counts = Counter(row["outcome"] for row in model_rows)
        direct = sum(row["outcome"] == "pass" and row["validator"] == "dolmen" for row in model_rows)
        complete = result.returncode == 0 and len(model_rows) == len(rows) and direct == len(rows)
        coverage["passed"] += direct
        coverage["groups"].append({"profile": profile, "round": round_index,
                                   "expected": len(rows), "direct_dolmen": direct,
                                   "outcomes": counts, "exit_code": result.returncode,
                                   "files": [row["file"] for row in rows]})
        coverage["status"] = "running" if complete else "failed"
        coverage_path.write_text(json.dumps(coverage, indent=2) + "\n")
        if not complete:
            raise SystemExit("incomplete or failed SAT replay")
    if hashlib.sha256(solver.read_bytes()).hexdigest() != provenance["solver_sha256"]:
        raise SystemExit("binary changed during validation")
    for rows in groups.values():
        for row in rows:
            path = args.root / row["file"]
            if hashlib.sha256(path.read_bytes()).hexdigest() != provenance["inputs"][row["file"]]:
                raise SystemExit("input changed during validation")
    coverage["status"] = "complete"
    coverage_path.write_text(json.dumps(coverage, indent=2) + "\n")
    print(json.dumps(coverage))


if __name__ == "__main__":
    main()
