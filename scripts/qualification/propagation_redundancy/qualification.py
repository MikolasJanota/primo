#!/usr/bin/env python3
"""Validate retained correctness receipts for one frozen propagation-filter source."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gate(directory: Path, source: str, native: str) -> dict:
    value = json.loads((directory / "coverage.json").read_text())
    assert value["source_snapshot"] == source and value["native_sha256"] == native
    summary = directory / "summary.csv"
    assert sha(summary) == value["summary_sha256"]
    rows = list(csv.DictReader(summary.open()))
    assert len(rows) == value["count"] == 1000
    assert dict(collections.Counter(r["outcome"] for r in rows)) == value["outcomes"]
    assert set(value["outcomes"]) <= {"PASS", "TIMEOUT", "UNKNOWN_INVOLVED"}
    if value["outcomes"].get("UNKNOWN_INVOLVED"):
        assert len(value["resource_unknowns"]) == value["outcomes"]["UNKNOWN_INVOLVED"]
    models = directory / "models/summary.csv"
    checked = list(csv.DictReader(models.open())) if models.exists() else []
    assert len(checked) == value["selected_sat"] == value["direct_dolmen_pass"]
    assert all(
        r["outcome"] == "pass"
        and r["validator"] == "dolmen"
        and r["solver_exit"] == r["dolmen_exit"] == "0"
        for r in checked
    )
    return value | {
        "receipt_sha256": sha(directory / "coverage.json"),
        "model_summary_sha256": sha(models) if models.exists() else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--native-sha256", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    roots = [args.root / p / "gates" for p in ("qualification", "qualification-extra")]
    gates = [
        gate(root / f"gate-{slot}", args.source, args.native_sha256)
        for root, slots in zip(roots, (range(8), range(4)), strict=True)
        for slot in slots
    ]
    outcomes = collections.Counter()
    for value in gates:
        outcomes.update(value["outcomes"])
    checks = {}
    for name in ("check", "asan"):
        path = args.root / "verification" / (name + ".log")
        text = path.read_text()
        assert "100% tests passed out of 49" in text
        if name == "check":
            assert "Ran 354 tests" in text and "\nOK\n" in text
            assert "rg: command not found" not in text
        checks[name] = {"sha256": sha(path), "native_groups": 49}
    inventories = {}
    for name in ("tidy", "includes"):
        path = args.root / "static" / (name + "-summary.json")
        value = json.loads(path.read_text())
        assert (
            value["expected_translation_units"]
            == value["analyzed_translation_units"]
            == 140
        )
        assert (
            not value["missing"]
            and not value["changed_inputs"]
            and value["tool_exit_code"] == 0
        )
        assert value["unused_include_gate"]["status"] == "passed"
        inventories[name] = {
            k: v for k, v in value.items() if k not in ("diagnostics", "findings")
        } | {"receipt_sha256": sha(path)}
    report = {
        "status": "source checks and generated/model gates qualified; mutation and corpus evidence separate",
        "source_snapshot": args.source,
        "native_sha256": args.native_sha256,
        "generated_inputs": sum(v["count"] for v in gates),
        "outcomes": dict(outcomes),
        "SAT_Dolmen_pass": sum(v["direct_dolmen_pass"] for v in gates),
        "gates": gates,
        "checks": checks,
        "static": inventories,
        "scope": "Timeouts and diagnosed resource unknowns remain incomplete. Static inventories with findings are not a clean-tree claim. Every selected SAT gate case has a direct Dolmen result. Mutations, seeds and full timing-model validation are separate artifacts.",
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {k: v for k, v in report.items() if k not in ("gates", "checks", "static")},
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
