#!/usr/bin/env python3
"""Audit exact-profile correctness receipts before permitting broad timings."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def model_rows(directory: Path, count: int, expected: set[str] | None = None) -> int:
    path = directory / "summary.csv"
    rows = list(csv.DictReader(path.open())) if count else []
    assert len(rows) == count
    assert all(
        r["outcome"] == "pass"
        and r["validator"] == "dolmen"
        and r["solver_exit"] == r["dolmen_exit"] == "0"
        for r in rows
    )
    if expected is not None:
        assert {r["file"] for r in rows} == expected
    return len(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    campaign = json.loads(args.campaign.read_text())
    receipts = []
    totals = collections.Counter()
    models = 0
    for phase in ("generated", "seeds", "mutations"):
        for slot, task in enumerate(campaign["tasks"]):
            directory = args.root / phase / f"task-{slot}"
            receipt = json.loads((directory / "receipt.json").read_text())
            assert receipt["task"] == task and receipt["phase"] == phase
            assert (
                receipt["source"] == campaign["git"]
                and receipt["native_sha256"] == campaign["sha256"]
            )
            assert receipt["campaign_sha256"] == sha(args.campaign)
            if phase == "generated":
                assert receipt["cases"] == 1000 and receipt["generator_exit"] in (0, 1)
                assert (
                    receipt["seed"]
                    == campaign["protocol"]["generated_seed_base"] + slot
                )
                summary = directory / "summary.csv"
                rows = list(csv.DictReader(summary.open()))
                assert len(rows) == 1000 and sha(summary) == receipt["summary_sha256"]
                assert (
                    dict(collections.Counter(r["outcome"] for r in rows))
                    == receipt["outcomes"]
                )
                assert set(receipt["outcomes"]) <= {
                    "PASS",
                    "TIMEOUT",
                    "UNKNOWN_INVOLVED",
                    "REFERENCE_UNUSABLE",
                }
                selected = {
                    r.get("case") or r["file"]
                    for r in rows
                    if (r.get("ours") or r.get("answer")) == "sat"
                }
                models += model_rows(
                    directory / "models", receipt["SAT_Dolmen_pass"], selected
                )
                assert (
                    sha(directory / "models/summary.csv")
                    == receipt["model_summary_sha256"]
                )
                totals.update({"generated_inputs": 1000})
                for key, value in receipt["outcomes"].items():
                    totals["generated_" + key] += value
            elif phase == "seeds":
                assert receipt["solver_args"] == [*task["args"], "--check-model"]
                assert receipt["logics"] == [task["logic"]]
                models += model_rows(
                    directory / "models", receipt["single_query_sat_models"]
                )
                scoped = (
                    json.loads((directory / "scoped-models/summary.json").read_text())
                    if receipt["scoped_sessions"]
                    else []
                )
                assert len(scoped) == receipt["scoped_sessions"]
                assert all(
                    s["outcome"] == "pass"
                    and all(q["outcome"] == "pass" for q in s["queries"])
                    for s in scoped
                )
                models += sum(
                    q["answer"] == "sat" for s in scoped for q in s["queries"]
                )
                totals.update({"tracked_regression_measurements": receipt["cases"]})
            else:
                summary = directory / "mutant-answers.csv"
                rows = list(csv.DictReader(summary.open()))
                assert len(rows) == receipt["cases"] >= 100
                assert sha(summary) == receipt["summary_sha256"]
                assert (
                    dict(collections.Counter(r["outcome"] for r in rows))
                    == receipt["outcomes"]
                )
                assert set(receipt["outcomes"]) <= {"PASS", "INCOMPLETE"}
                selected = {
                    r["file"]
                    for r in rows
                    if r["ours"] == "sat" and r["ours_exit"] == "0"
                }
                models += model_rows(
                    directory / "models", receipt["SAT_Dolmen_pass"], selected
                )
                originals = list(
                    csv.DictReader((directory / "seed-answers.csv").open())
                )
                assert len(originals) == 12 and {r["outcome"] for r in originals} <= {
                    "PASS",
                    "INCOMPLETE",
                }
                selected = {
                    r["file"]
                    for r in originals
                    if r["ours"] == "sat" and r["ours_exit"] == "0"
                }
                models += model_rows(
                    directory / "seed-models",
                    receipt["original_models"]["SAT_Dolmen_pass"],
                    selected,
                )
                totals.update({"mutants": len(rows)})
                for key, value in receipt["outcomes"].items():
                    totals["mutation_" + key] += value
            receipts.append(
                {
                    "phase": phase,
                    "slot": slot,
                    "logic": task["logic"],
                    "profile": task["label"],
                    "receipt_sha256": sha(directory / "receipt.json"),
                    "coverage": {
                        key: value
                        for key, value in receipt.items()
                        if key
                        in {
                            "task",
                            "phase",
                            "source",
                            "native_sha256",
                            "campaign_sha256",
                            "script_sha256",
                            "seed",
                            "cases",
                            "outcomes",
                            "summary_sha256",
                            "SAT_Dolmen_pass",
                            "model_summary_sha256",
                            "single_query_sat_models",
                            "scoped_sessions",
                            "solver_args",
                            "logics",
                            "generator_exit",
                        }
                    },
                }
            )
    assert len(receipts) == 72
    report = {
        "status": "qualified with separately recorded resource-incomplete outcomes",
        "source": campaign["git"],
        "native_sha256": campaign["sha256"],
        "counts": dict(totals),
        "SAT_Dolmen_pass": models,
        "receipts": receipts,
        "scope": "Every successful native SAT gate result checked, including original mutation seeds and reference-timeout rows. Timeout/unknown/reference-timeout cases remain incomplete, not agreement passes.",
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "receipts"}, indent=2))


if __name__ == "__main__":
    main()
