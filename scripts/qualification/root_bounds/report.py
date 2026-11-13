#!/usr/bin/env python3
"""Measure input-only bound opportunities on a pinned selection inside SLURM."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import statistics
import sys
from collections import Counter
from pathlib import Path

ROOT = next(
    parent
    for parent in Path(__file__).resolve().parents
    if (parent / "scripts/bench_qf_uf_full.py").is_file()
)
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402

PREFIX = ":arithmetic.root-bounds."


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument(
        "--root", action="append", required=True, help="LOGIC=directory"
    )
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("solver batches require SLURM")
    roots = dict(item.split("=", 1) for item in args.root)
    cases = [
        case
        for case in json.loads(args.selection.read_text())["cases"]
        if case["group"] in ("lia", "uflia") and case["repetition"] == 0
    ]
    assert Counter(case["logic"] for case in cases) == {
        "QF_LIA": 200,
        "QF_UFLIA": 300,
    }
    assert len({(case["logic"], case["file"]) for case in cases}) == len(cases)
    args.out.mkdir(parents=True, exist_ok=False)
    flags = [
        "--quiet",
        "--arithmetic-bound-diagnostics",
        "--preprocess-only",
        "--stats",
    ]
    rows = []
    with (args.out / "rows.jsonl").open("w") as stream:
        for case in cases:
            source = Path(roots[case["logic"]]) / case["file"]
            assert sha(source) == case["sha256"], source
            result = run_solver([str(args.solver), *flags], source, args.timeout)
            row = {
                "logic": case["logic"],
                "file": case["file"],
                "sha256": case["sha256"],
                "expected": case["expected"],
                **{
                    key: result[key]
                    for key in ("status", "answer", "exit_code", "seconds")
                },
                "counters": {
                    key.removeprefix(PREFIX): int(value)
                    for key, value in result["stats"].items()
                    if key.startswith(PREFIX)
                },
            }
            (args.out / f"{len(rows):03d}.stdout").write_text(result["stdout"])
            (args.out / f"{len(rows):03d}.stderr").write_text(result["stderr"])
            if row["status"] == "ok":
                assert row["answer"] in ("unknown", "unsat"), row
                assert row["answer"] != "unsat" or row["expected"] != "sat", row
                assert row["counters"] and row["counters"]["cancelled"] == 0, row
                assert not any(
                    key.startswith((":sat.", ":theory.")) and int(value) != 0
                    for key, value in result["stats"].items()
                ), "preprocess-only unexpectedly entered search"
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
            stream.flush()
            rows.append(row)
    summary = {}
    for logic in sorted(roots):
        selected = [row for row in rows if row["logic"] == logic]
        complete = [row for row in selected if row["status"] == "ok"]
        counts = Counter()
        for row in complete:
            counts.update(row["counters"])
        summary[logic] = {
            "selected": len(selected),
            "complete": len(complete),
            "statuses": dict(Counter(row["status"] for row in selected)),
            "answers": dict(Counter(row["answer"] for row in complete)),
            "instances_with_improvements": sum(
                row["counters"]["stronger-bounds"] > 0 for row in complete
            ),
            "counter_totals_complete_only": dict(counts),
            "median_detector_us": statistics.median(
                row["counters"]["time-us"] for row in complete
            )
            if complete
            else None,
        }
    provenance = {
        "job": os.environ["SLURM_JOB_ID"],
        "solver_sha256": sha(args.solver),
        "selection_sha256": sha(args.selection),
        "runner_sha256": sha(Path(__file__)),
        "flags": flags,
        "timeout_seconds": args.timeout,
        "rows_sha256": sha(args.out / "rows.jsonl"),
        "summary": summary,
        "interpretation": "Counts only; no bounds installed. Not a speed comparison.",
    }
    (args.out / "summary.json").write_text(json.dumps(provenance, indent=2) + "\n")
    assert all(row["status"] in ("ok", "timeout") for row in rows), "inspect errors"


if __name__ == "__main__":
    main()
