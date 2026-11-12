#!/usr/bin/env python3
"""Draw cactus plots from qualified paired metadata without running solvers."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.plot_cactus import order_series_by_solved, svg  # noqa: E402
from scripts.qualification.lia_hybrid.summarize import solved  # noqa: E402

LABELS = {"hybrid": "Hybrid (opt-in)", "private": "Private baseline"}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / "benchmarks/lia-hybrid-20261111")
    parser.add_argument("--out", type=Path, default=ROOT / "docs/_static/lia-hybrid-20261111")
    args = parser.parse_args()
    selection_path = args.data / "selection.json"
    rows_path = args.data / "paired/timing-rows.jsonl"
    summary_path = args.data / "paired/summary.json"
    qualification_path = args.data / "qualification.json"
    selection = json.loads(selection_path.read_text())
    summary = json.loads(summary_path.read_text())
    qualification = json.loads(qualification_path.read_text())
    for path in (selection_path, rows_path, summary_path):
        assert sha(path) == qualification["artifact_sha256"][str(path.relative_to(args.data))]
    assert not summary["model_coverage_incomplete_slots"]
    records = [json.loads(line) for line in rows_path.read_text().splitlines()]
    assert len(records) == len(selection["cases"]) == 512
    for slot, record in enumerate(records):
        assert record["slot"] == slot and record["case"] == selection["cases"][slot]
        assert len(record["rows"]) == 2
        assert {row["profile"] for row in record["rows"]} == set(LABELS)
        selected = {row["profile"] for row in record["rows"] if solved(row) and row["answer"] == "sat"}
        assert record["coverage"]["selected_sat"] == record["coverage"]["direct_dolmen_pass"] == len(selected)
        assert {row["profile"] for row in record["coverage"]["rows"]} == selected
        for row in record["rows"]:
            assert row["binary_sha256"] == qualification["binaries"][row["profile"]]
            assert math.isfinite(float(row["seconds"])) and float(row["seconds"]) >= 0
            assert not solved(row) or float(row["seconds"]) <= selection["timeout"]

    args.out.mkdir(parents=True, exist_ok=True)
    report = {
        "kind": "derived from existing qualified timings; no new solver execution",
        "baseline_git": qualification["baseline_commit"],
        "implementation_git": qualification["implementation_commit"],
        "qualified_snapshot": qualification["qualified_snapshot"],
        "timing_job": qualification["jobs"]["main_timing"],
        "models_job": qualification["jobs"]["main_models"],
        "timeout_seconds": selection["timeout"],
        "source_sha256": {str(p.relative_to(args.data)): sha(p) for p in (selection_path, rows_path, summary_path, qualification_path)},
        "script_sha256": sha(Path(__file__)),
        "plotter_sha256": sha(ROOT / "scripts/plot_cactus.py"),
        "plots": {},
    }
    for name, groups, title in (
        ("lia", {"lia"}, "QF_LIA selection: hybrid vs private"),
        ("uflia", {"uflia"}, "QF_UFLIA selection: hybrid vs private"),
        ("combined", {"lia", "uflia"}, "LIA + UFLIA selections: hybrid vs private"),
    ):
        selected_records = [record for record in records if record["case"]["group"] in groups]
        metrics = {}
        curves = []
        breakdown = {}
        for profile, label in LABELS.items():
            rows = [row for record in selected_records for row in record["rows"] if row["profile"] == profile]
            times = sorted(float(row["seconds"]) for row in rows if solved(row))
            expected = sum(summary["groups"][group][profile]["solved"] for group in groups)
            assert len(times) == expected
            metrics[profile] = {"inputs": len(rows), "solved": len(times)}
            curves.append((label, times))
            breakdown[label] = tuple(sum(solved(row) and row["answer"] == answer for row in rows) for answer in ("sat", "unsat"))
        caption = [
            f"{len(selected_records)} selected inputs; 120s per instance; no easy-input filtering. Higher is better.",
            "Private baseline c49585a0 vs hybrid 9e01197e; factor 1; no new measurements.",
            "Only successful SAT/UNSAT rows plotted. Every successful SAT row separately Dolmen-validated.",
            "Times below 0.01s share the left edge. Certora diagnostic repetitions and regression repeats excluded.",
        ]
        path = args.out / f"{name}-120.svg"
        path.write_text(svg(order_series_by_solved(curves), selection["timeout"], len(selected_records),
                            title, caption, highlighted=LABELS["hybrid"], breakdown=breakdown))
        report["plots"][path.name] = {"sha256": sha(path), "series": metrics}
        print(name, metrics)
    (args.out / "plots.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
