#!/usr/bin/env python3
"""Summarize immutable paired timing rows and their separate model coverage."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
from collections import Counter
from pathlib import Path


def solved(row: dict) -> bool:
    return (
        row["status"] == "ok"
        and row["exit_code"] == 0
        and row["answer"] in ("sat", "unsat")
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    groups = {}
    regressions = []
    incomplete_models = []
    comparisons = []
    for slot, case in enumerate(config["cases"]):
        directory = args.root / f"slot-{slot}"
        assert (directory / "timing-complete").is_file(), f"missing timing slot {slot}"
        rows = [
            json.loads(line)
            for line in (directory / "rows.jsonl").read_text().splitlines()
        ]
        assert len(rows) == len(config["profiles"])
        profiles = {row["profile"]: row for row in rows}
        assert set(profiles) == set(config["profiles"])
        selected = sum(solved(row) and row["answer"] == "sat" for row in rows)
        coverage_path = directory / "coverage.json"
        coverage = (
            json.loads(coverage_path.read_text()) if coverage_path.is_file() else {}
        )
        if (
            coverage.get("selected_sat") != selected
            or coverage.get("direct_dolmen_pass") != selected
        ):
            incomplete_models.append(slot)
        group = groups.setdefault(case["group"], {})
        for label, row in profiles.items():
            data = group.setdefault(
                label,
                {
                    "inputs": 0,
                    "solved": 0,
                    "sat": 0,
                    "unsat": 0,
                    "statuses": Counter(),
                    "unknown": 0,
                    "memory_out": 0,
                    "par2_sum": 0.0,
                    "peak_rss_kib": 0,
                    "counters": Counter(),
                },
            )
            data["inputs"] += 1
            data["statuses"][row["status"]] += 1
            data["solved"] += int(solved(row))
            data["sat"] += int(solved(row) and row["answer"] == "sat")
            data["unsat"] += int(solved(row) and row["answer"] == "unsat")
            data["unknown"] += int(row["status"] == "ok" and row["answer"] == "unknown")
            data["memory_out"] += int(row.get("memory_out", False))
            data["par2_sum"] += (
                float(row["seconds"]) if solved(row) else 2 * config["timeout"]
            )
            data["peak_rss_kib"] = max(data["peak_rss_kib"], row["peak_rss_kib"])
            for key, value in row.get("stats", {}).items():
                if key.startswith(
                    (":lia.branching.", ":time.lia.branching.", ":lia.branch-nodes")
                ):
                    if key == ":lia.branching.peak-generated-atoms":
                        data["counters"][key] = max(data["counters"][key], int(value))
                    else:
                        data["counters"][key] += int(value)
        private, hybrid = profiles["private"], profiles["hybrid"]
        ratio = (
            float(hybrid["seconds"]) / float(private["seconds"])
            if solved(private) and solved(hybrid)
            else None
        )
        comparison = {
            "slot": slot,
            "group": case["group"],
            "file": case["file"],
            "repetition": case["repetition"],
            "private": {
                key: private[key]
                for key in ("status", "answer", "seconds", "peak_rss_kib")
            },
            "hybrid": {
                key: hybrid[key]
                for key in ("status", "answer", "seconds", "peak_rss_kib")
            },
            "ratio": ratio,
            "gain": solved(hybrid) and not solved(private),
            "loss": solved(private) and not solved(hybrid),
        }
        comparisons.append(comparison)
        if comparison["loss"] or (
            ratio
            and ratio > 1.2
            and float(hybrid["seconds"]) - float(private["seconds"]) > 0.1
        ):
            regressions.append(comparison)
    for group_name, group in groups.items():
        paired = [
            row["ratio"]
            for row in comparisons
            if row["group"] == group_name and row["ratio"] is not None
        ]
        group["paired"] = {
            "both_solved": len(paired),
            "median_hybrid_over_private": statistics.median(paired) if paired else None,
            "geomean_hybrid_over_private": math.exp(
                sum(map(math.log, paired)) / len(paired)
            )
            if paired
            else None,
            "gains": sum(
                row["gain"] for row in comparisons if row["group"] == group_name
            ),
            "losses": sum(
                row["loss"] for row in comparisons if row["group"] == group_name
            ),
        }
    result = {
        "status": "timing complete; model coverage incomplete"
        if incomplete_models
        else "complete and all successful SAT rows Dolmen-validated",
        "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest(),
        "groups": groups,
        "model_coverage_incomplete_slots": incomplete_models,
        "regressions": regressions,
        "comparisons": comparisons,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
