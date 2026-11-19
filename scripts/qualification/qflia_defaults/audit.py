#!/usr/bin/env python3
"""Audit the QF_LIA default-promotion comparison and every selected SAT replay."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import (  # noqa: E402
    describe,
    expected_resource_diagnostic,
)
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def comparison(rows, before, after, limit):
    common = [r for r in rows if solved(r[before]) and solved(r[after])]
    ratios = [
        r[after]["seconds"] / r[before]["seconds"]
        for r in common
        if r[before]["seconds"] >= 1
    ]
    return {
        "gains": [
            r[after]["file"] for r in rows if not solved(r[before]) and solved(r[after])
        ],
        "losses": [
            r[before]["file"]
            for r in rows
            if solved(r[before]) and not solved(r[after])
        ],
        "mean_PAR2_delta_seconds": statistics.mean(
            penalty(r[after], limit) - penalty(r[before], limit) for r in rows
        ),
        "common_solved": len(common),
        "common_before_at_least_1s": len(ratios),
        "conditional_geomean_ratio": (
            math.exp(statistics.mean(map(math.log, ratios))) if ratios else None
        ),
        "runtime_regressions_to_repeat": [
            {
                "file": r[before]["file"],
                "before": r[before]["seconds"],
                "after": r[after]["seconds"],
            }
            for r in common
            if r[after]["seconds"] >= 1.1 * r[before]["seconds"]
            and r[after]["seconds"] - r[before]["seconds"] >= 0.5
        ],
        "limitation": "One rotated pass; runtime differences and losses are not repeat-qualified.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    assert len(config["cases"]) == 200
    assert list(config["profiles"]) == ["OLD", "NEW", "HYBRID", "CUBE"]
    records, receipts, coverage = [], [], []
    for slot, case in enumerate(config["cases"]):
        directory = args.rows / f"slot-{slot}"
        assert (directory / "timing-complete").is_file()
        provenance_path = directory / "provenance.json"
        provenance = json.loads(provenance_path.read_text())
        assert provenance["config_sha256"] == sha(args.config)
        assert provenance["runner_sha256"] == sha(
            ROOT / "scripts/qualification/lia_hybrid/run.py"
        )
        assert (
            provenance["case"] == case and provenance["profiles"] == config["profiles"]
        )
        assert len(provenance["affinity"]) == 1
        order = measurement_order(config, slot)
        assert provenance["order"] == order
        path = directory / "rows.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        assert [row["profile"] for row in rows] == order
        by_profile = {}
        for row in rows:
            label = row["profile"]
            assert row["args"] == config["profiles"][label]["args"] + case.get(
                "extra_args", []
            )
            assert (
                row["logic"] == case["logic"] == "QF_LIA"
                and row["file"] == case["file"]
            )
            assert math.isfinite(row["seconds"]) and row["seconds"] > 0
            audit_row(row)
            if solved(row):
                assert row["answer"] == case["expected"]
            reason = expected_resource_diagnostic(row)
            if reason:
                text = (directory / f"{label}.stderr").read_text()
                assert reason in text.splitlines(), (slot, label, reason)
            by_profile[label] = row
        assert len({r["answer"] for r in rows if solved(r)}) <= 1
        selected = {
            k: r for k, r in by_profile.items() if solved(r) and r["answer"] == "sat"
        }
        models = audit_models(directory, selected, config, case)
        assert models is not None
        coverage.append({"slot": slot, "metadata_sha256": models})
        receipts.append(
            {
                "slot": slot,
                "job": provenance["job"],
                "host": provenance["host"],
                "rows_sha256": sha(path),
                "provenance_sha256": sha(provenance_path),
            }
        )
        records.append(by_profile)
    limit = config["timeout"]

    def summarize(rows):
        return {
            label: describe([r[label] for r in rows], limit)
            for label in config["profiles"]
        }

    families = collections.defaultdict(list)
    for row in records:
        families[row["NEW"]["file"].split("/")[0]].append(row)
    report = {
        "status": "complete, including every selected native SAT model",
        "config_sha256": sha(args.config),
        "profiles": summarize(records),
        "comparisons": {
            f"{a}->NEW": comparison(records, a, "NEW", limit)
            for a in ("OLD", "HYBRID", "CUBE")
        },
        "families": {
            family: summarize(rows) for family, rows in sorted(families.items())
        },
        "SAT_Dolmen_pass": sum(len(x["metadata_sha256"]) for x in coverage),
        "receipt_inventory": receipts,
        "coverage": coverage,
    }
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    flat = [
        {"slot": slot, **row}
        for slot, case_rows in enumerate(records)
        for row in case_rows.values()
    ]
    with (args.out / "measurements.csv").open("w") as stream:
        fields = (
            "slot",
            "file",
            "logic",
            "profile",
            "status",
            "answer",
            "exit_code",
            "seconds",
            "peak_rss_kib",
        )
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(flat)
    print(json.dumps({k: report[k] for k in ("status", "SAT_Dolmen_pass")}))


if __name__ == "__main__":
    main()
