#!/usr/bin/env python3
"""Summarize immutable paired rows and select substantial regression repeats."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path


def solved(row: dict) -> bool:
    return (
        row["status"] == "ok"
        and row["exit_code"] == 0
        and row["answer"] in ("sat", "unsat")
    )


def summarize(pairs: list, timeout: float) -> dict:
    result = {"cases": len(pairs), "profiles": {}}
    for label in ("baseline", "tightened"):
        rows = [pair[label] for pair in pairs]
        completed = [row for row in rows if solved(row)]
        outcomes = collections.Counter(
            row["answer"]
            if solved(row)
            else "memory-out"
            if row.get("memory_out")
            else row["status"]
            if row["status"] != "ok"
            else row["answer"] or "missing-answer"
            for row in rows
        )
        par2 = sum(
            float(row["seconds"]) if solved(row) else 2 * timeout for row in rows
        )
        counter_sums = collections.Counter()
        for row in rows:
            for key, value in row["stats"].items():
                if key.startswith(":preprocessing.bounds."):
                    counter_sums[key] += int(value)
        result["profiles"][label] = {
            "solved": len(completed),
            "outcomes": dict(outcomes),
            "par2_seconds_sum": par2,
            "par2_seconds_mean": par2 / len(rows),
            "median_peak_rss_kib": statistics.median(
                row["peak_rss_kib"] for row in rows
            ),
            "max_peak_rss_kib": max(row["peak_rss_kib"] for row in rows),
            "counters_sum_including_partial_timeout_statistics": dict(counter_sums),
        }
    common = [pair for pair in pairs if all(solved(r) for r in pair.values())]
    ratios = [
        float(p["tightened"]["seconds"]) / max(float(p["baseline"]["seconds"]), 1e-6)
        for p in common
    ]
    result.update(
        common_solved=len(common),
        gained=sum(solved(p["tightened"]) and not solved(p["baseline"]) for p in pairs),
        lost=sum(solved(p["baseline"]) and not solved(p["tightened"]) for p in pairs),
        median_paired_runtime_ratio=statistics.median(ratios) if ratios else None,
        geometric_mean_paired_runtime_ratio=(
            math.exp(statistics.mean(math.log(r) for r in ratios)) if ratios else None
        ),
    )
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    pairs = []
    regressions = []
    model_expected = model_pass = 0
    complete_models = True
    receipt_hashes = {}
    for slot, case in enumerate(config["cases"]):
        directory = args.rows / f"slot-{slot}"
        assert (directory / "timing-complete").is_file(), slot
        source = directory / "rows.jsonl"
        rows = [json.loads(line) for line in source.read_text().splitlines()]
        pair = {row["profile"]: row for row in rows}
        assert len(rows) == len(pair) == 2
        assert set(pair) == {"baseline", "tightened"}
        assert len({r["answer"] for r in rows if solved(r)}) <= 1
        assert all(
            r["status"] in ("ok", "timeout", "unsupported") or r.get("memory_out")
            for r in rows
        )
        assert all(r["exit_code"] == 0 for r in rows if r["status"] == "ok")
        pairs.append(pair)
        receipt_hashes[str(slot)] = hashlib.sha256(source.read_bytes()).hexdigest()
        selected = sum(solved(row) and row["answer"] == "sat" for row in rows)
        model_expected += selected
        coverage = directory / "coverage.json"
        if coverage.is_file():
            value = json.loads(coverage.read_text())
            assert value["selected_sat"] == value["direct_dolmen_pass"] == selected
            model_pass += selected
        else:
            complete_models = False
        before, after = pair["baseline"], pair["tightened"]
        lost = solved(before) and not solved(after)
        slow = (
            solved(before)
            and solved(after)
            and float(after["seconds"]) >= 2 * float(before["seconds"])
            and float(after["seconds"]) - float(before["seconds"]) >= 1
        )
        if lost or slow:
            regressions.append(
                {
                    "slot": slot,
                    "case": case,
                    "reason": "lost-solve" if lost else "at-least2x-and1s-slower",
                    "baseline_seconds": before["seconds"],
                    "tightened_seconds": after["seconds"],
                }
            )
    report = {
        "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest(),
        "receipt_sha256": receipt_hashes,
        "all": summarize(pairs, config["timeout"]),
        "by_logic": {
            logic: summarize(
                [
                    p
                    for p, c in zip(pairs, config["cases"], strict=True)
                    if c["logic"] == logic
                ],
                config["timeout"],
            )
            for logic in sorted({c["logic"] for c in config["cases"]})
        },
        "models": {
            "complete": complete_models,
            "expected_sat": model_expected,
            "direct_dolmen_pass": model_pass,
        },
        "regressions_to_repeat": regressions,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    with (args.out / "pairs.csv").open("w") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "slot",
                "logic",
                "file",
                "baseline_answer",
                "baseline_solved",
                "baseline_seconds",
                "tightened_answer",
                "tightened_solved",
                "tightened_seconds",
            ]
        )
        for slot, (pair, case) in enumerate(zip(pairs, config["cases"], strict=True)):
            writer.writerow(
                [
                    slot,
                    case["logic"],
                    case["file"],
                    *[
                        v
                        for label in ("baseline", "tightened")
                        for v in (
                            pair[label]["answer"],
                            solved(pair[label]),
                            pair[label]["seconds"],
                        )
                    ],
                ]
            )
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k not in ("receipt_sha256", "regressions_to_repeat")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
