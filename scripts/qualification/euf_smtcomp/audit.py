#!/usr/bin/env python3
"""Audit complete SMT-COMP paired timings before model replay and final export."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import os
import statistics as st
import sys
from pathlib import Path

ROOT = Path(os.environ.get("PRIMO_WORKDIR", Path(__file__).resolve().parents[3]))
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import compare  # noqa: E402
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    outcome,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402

COUNTERS = (
    ":euf.incremental.merges",
    ":euf.incremental.explain-congruence-edges",
    ":euf.incremental.explain-bfs-path-edges",
    ":sat.conflicts",
    ":sat.decisions",
    ":sat.propagations",
    ":theory.conflicts",
    ":lia.branch-nodes",
)


def describe(rows):
    good = [r for r in rows if solved(r)]
    counters = {}
    for key in sorted({k for r in rows for k in r["stats"]}):
        if key.endswith("policy-version"):
            continue
        values = [int(r["stats"][key]) for r in good if key in r["stats"]]
        if values:
            counters[key] = {
                "reporting_successful_rows": len(values),
                "value": max(values)
                if any(m in key for m in ("peak-", "max-", ".max"))
                else sum(values),
            }
    return {
        "measurements": len(rows),
        "solved": len(good),
        "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
        "mean_PAR2_seconds": st.mean(penalty(r, 120) for r in rows),
        "median_RSS_KiB": st.median(r["peak_rss_kib"] for r in rows),
        "max_RSS_KiB": max(r["peak_rss_kib"] for r in rows),
        "successful_printed_counters": counters,
    }


def audit(args):
    protocol = json.loads((args.configs / "protocol.json").read_text())
    selection_path = args.configs / "selection.json"
    assert sha(selection_path) == protocol["selection_sha256"]
    selection = json.loads(selection_path.read_text())
    cases = selection["cases"]
    routing = json.loads((args.configs / "routing.json").read_text())
    profiles = routing["profiles"]
    assert set(profiles) == {"BASE", "OFF"}
    assert {
        p: {k: v for k, v in data.items() if k != "solver"}
        for p, data in profiles.items()
    } == protocol["sources"]
    assert protocol["timeout_seconds"] == 120 and protocol["repetitions"] == 3
    assert protocol["address_space_bytes"] == 8589934592
    assert protocol["cases"] == len(cases)
    assert protocol["runner_sha256"] == sha(
        ROOT / "scripts/qualification/lia_hybrid/run.py"
    )
    records, receipts, answers = {}, [], collections.defaultdict(set)
    config_cache = {}
    model_count = 0
    for rep in range(3):
        for i, case in enumerate(cases):
            chunk, slot = divmod(i, protocol["chunk_size"])
            card = args.configs / f"chunk-{chunk}-{rep}.json"
            if (chunk, rep) not in config_cache:
                config_cache[chunk, rep] = json.loads(card.read_text()), sha(card)
            config, card_sha = config_cache[chunk, rep]
            assert (
                config["profiles"] == profiles and config["roots"] == routing["roots"]
            )
            assert config["cases"][slot] == dict(case, repetition=rep)
            assert (
                config["timeout"] == 120 and config["address_space_bytes"] == 8589934592
            )
            directory = args.rows / f"rep-{rep}" / f"chunk-{chunk}" / f"slot-{slot}"
            assert (directory / "timing-complete").is_file(), directory
            provenance_path = directory / "provenance.json"
            provenance = json.loads(provenance_path.read_text())
            assert provenance["config_sha256"] == card_sha
            assert provenance["runner_sha256"] == protocol["runner_sha256"]
            assert provenance["case"] == config["cases"][slot]
            assert provenance["profiles"] == profiles
            rows_path = directory / "rows.jsonl"
            rows = [json.loads(line) for line in rows_path.read_text().splitlines()]
            assert len(rows) == 2
            assert [r["profile"] for r in rows] == measurement_order(config, slot)
            for row in rows:
                audit_row(row)
                assert row["status"] != "unsupported", row
                assert row["file"] == case["file"] and row["logic"] == case["logic"]
                assert row["args"] == profiles[row["profile"]]["args"]
                row["seconds"] = float(row["seconds"])
                assert math.isfinite(row["seconds"]) and row["seconds"] > 0
                assert row["peak_rss_kib"] >= 0
                if solved(row):
                    answers[case["logic"], case["file"]].add(row["answer"])
                    if case["expected"] in ("sat", "unsat"):
                        assert row["answer"] == case["expected"], row
                if row["status"] == "ok" and row["answer"] == "unknown":
                    diagnostic = [
                        line
                        for line in (directory / (row["profile"] + ".stderr"))
                        .read_text()
                        .splitlines()
                        if line.strip() and not line.startswith(":")
                    ]
                    assert diagnostic == ["integer branch-and-bound node limit"], (
                        diagnostic
                    )
                records[rep, i, row["profile"]] = row
            receipt = {
                "repetition": rep,
                "slot": i,
                "rows_sha256": sha(rows_path),
                "provenance_sha256": sha(provenance_path),
            }
            if args.phase == "final":
                selected = {
                    r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
                }
                models = audit_models(
                    directory, selected, config, config["cases"][slot]
                )
                assert models is not None, directory
                model_count += len(selected)
                receipt.update(
                    coverage_sha256=sha(directory / "coverage.json"),
                    model_metadata_sha256=models,
                )
            receipts.append(receipt)
    assert all(len(values) == 1 for values in answers.values()), answers
    assert len(records) == protocol["native_rows"] == len(cases) * 6
    result = {
        "phase": args.phase,
        "native_rows": len(records),
        "cases": len(cases),
        "SAT_Dolmen_pass": model_count if args.phase == "final" else None,
        "selection_sha256": sha(selection_path),
        "protocol_sha256": sha(args.configs / "protocol.json"),
        "auditor_sha256": sha(Path(__file__)),
        "counter_scope": "Only printed successful-row values; missing unavailable. Peaks/maxima use maximum; other gauge sums are not event counts. Policy-version fields are diagnostic, not work.",
        "logics": {},
    }
    for logic in sorted({c["logic"] for c in cases}):
        indices = [i for i, c in enumerate(cases) if c["logic"] == logic]
        comparison = compare(records, indices, "BASE", "OFF", 120)
        comparison.pop("matched_complete_counter_totals")
        result["logics"][logic] = {
            "cases": len(indices),
            "profiles": {
                p: {
                    "pooled": describe(
                        [records[r, i, p] for r in range(3) for i in indices]
                    ),
                    "repetitions": [
                        describe([records[r, i, p] for i in indices]) for r in range(3)
                    ],
                }
                for p in profiles
            },
            "comparison_BASE_OFF": comparison,
            "families": {
                family: {
                    p: describe(
                        [
                            records[r, i, p]
                            for r in range(3)
                            for i in indices
                            if cases[i]["family"] == family
                        ]
                    )
                    for p in profiles
                }
                for family in sorted({cases[i]["family"] for i in indices})
            },
        }
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    (args.out / "receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")
    fields = (
        "repetition",
        "slot",
        "profile",
        "logic",
        "file",
        "input_sha256",
        "source",
        "binary_sha256",
        "args_json",
        "seconds",
        "status",
        "answer",
        "exit_code",
        "peak_rss_kib",
    )
    with (args.out / "native-rows.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=[*fields, *COUNTERS])
        writer.writeheader()
        for (rep, i, label), row in sorted(records.items()):
            writer.writerow(
                {
                    **{k: row[k] for k in fields if k in row},
                    "repetition": rep,
                    "slot": i,
                    "input_sha256": cases[i]["sha256"],
                    "source": profiles[label]["git"],
                    "binary_sha256": profiles[label]["sha256"],
                    "args_json": json.dumps(row["args"]),
                    **{k: row["stats"].get(k, "") for k in COUNTERS},
                }
            )
    print(
        json.dumps({k: result[k] for k in ("phase", "native_rows", "SAT_Dolmen_pass")})
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("timing", "final"), required=True)
    for name in ("configs", "rows", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    audit(parser.parse_args())


if __name__ == "__main__":
    main()
