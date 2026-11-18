#!/usr/bin/env python3
"""Audit all selected repeated demand regressions, with separate SAT model coverage."""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import compare  # noqa: E402
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("configs", "rows", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    campaign = json.loads((args.configs / "campaign.json").read_text())
    assert len(campaign["selection"]) == 25
    records, receipts = {}, []
    answers = collections.defaultdict(set)
    sat = 0
    for rep in range(3):
        for slot, entry in enumerate(campaign["selection"]):
            card = args.configs / f"case-{slot}-{rep}.json"
            config = json.loads(card.read_text())
            case = dict(entry["case"], repetition=rep)
            assert config["cases"] == [case]
            assert config["profiles"] == entry["profiles"]
            assert (
                config["timeout"] == 30 and config["address_space_bytes"] == 8589934592
            )
            directory = args.rows / f"rep-{rep}" / f"case-{slot}" / "slot-0"
            assert (directory / "timing-complete").is_file()
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["config_sha256"] == sha(card)
            assert provenance["runner_sha256"] == sha(
                Path("scripts/qualification/lia_hybrid/run.py")
            )
            assert provenance["case"] == case
            assert provenance["profiles"] == config["profiles"]
            rows = [
                json.loads(line)
                for line in (directory / "rows.jsonl").read_text().splitlines()
            ]
            assert [r["profile"] for r in rows] == measurement_order(config, 0)
            assert len(rows) == 3
            for row in rows:
                audit_row(row)
                label = row["profile"]
                assert row["args"] == config["profiles"][label]["args"] + case.get(
                    "extra_args", []
                )
                assert (row["logic"], row["file"]) == (case["logic"], case["file"])
                row["seconds"] = float(row["seconds"])
                if solved(row):
                    answers[slot].add(row["answer"])
                    if case["expected"] in ("sat", "unsat"):
                        assert row["answer"] == case["expected"]
                assert not (row["status"] == "ok" and row["answer"] == "unknown"), row
                records[rep, slot, label] = row
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            models = audit_models(directory, selected, config, case)
            assert models is not None, directory
            sat += len(selected)
            receipts.append(
                {
                    "repetition": rep,
                    "slot": slot,
                    "rows_sha256": sha(directory / "rows.jsonl"),
                    "coverage_sha256": sha(directory / "coverage.json"),
                    "model_metadata_sha256": models,
                }
            )
    assert all(len(v) == 1 for v in answers.values())
    comparisons = {}
    for before in ("OFF", "ON"):
        result = compare(records, list(range(25)), before, "DEMAND", 30)
        result.pop("matched_complete_counter_totals")
        comparisons[before + "->DEMAND"] = result
    # Public selection records contain no private workspace or solver paths.
    selection = [
        {k: e[k] for k in ("case", "group", "original_index", "comparisons", "slot")}
        for e in campaign["selection"]
    ]
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "summary.json").write_text(
        json.dumps(
            {
                "native_rows": 225,
                "SAT_Dolmen_pass": sat,
                "selection": selection,
                "comparisons": comparisons,
            },
            indent=2,
        )
        + "\n"
    )
    (args.out / "receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print(json.dumps({"native_rows": 225, "SAT_Dolmen_pass": sat}))


if __name__ == "__main__":
    main()
