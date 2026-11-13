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
            "cases_emitting_bounds": sum(
                int(row["stats"].get(":preprocessing.bounds.emitted", 0)) > 0
                for row in rows
            ),
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
    parser.add_argument(
        "--baseline-abort-receipts",
        type=Path,
        help="Separate frozen-baseline reproductions of candidate-first aborts",
    )
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    pairs = []
    regressions = []
    model_expected = model_pass = 0
    complete_models = True
    receipt_hashes = {}
    triaged_aborts = []
    baseline_aborts = {}
    baseline_abort_hashes = {}
    model_coverage_hashes = {}
    if args.baseline_abort_receipts:
        for directory in args.baseline_abort_receipts.glob("slot-*"):
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["profiles"]["baseline"] == config["profiles"]["baseline"]
            (row,) = map(
                json.loads, (directory / "rows.jsonl").read_text().splitlines()
            )
            assert row["profile"] == "baseline"
            parent = provenance["case"]["parent_slot"]
            assert parent not in baseline_aborts
            assert provenance["case"]["sha256"] == config["cases"][parent]["sha256"]
            assert row["file"] == config["cases"][parent]["file"]
            assert row["args"] == config["profiles"]["baseline"]["args"]
            baseline_aborts[parent] = row
            baseline_abort_hashes[str(parent)] = hashlib.sha256(
                (directory / "rows.jsonl").read_bytes()
            ).hexdigest()
    for slot, case in enumerate(config["cases"]):
        directory = args.rows / f"slot-{slot}"
        complete = (directory / "timing-complete").is_file()
        source = directory / "rows.jsonl"
        rows = [json.loads(line) for line in source.read_text().splitlines()]
        pair = {row["profile"]: row for row in rows}
        if not complete:
            assert (
                len(rows) == 1
                and set(pair) == {"tightened"}
                and slot in baseline_aborts
            ), slot
            pair["baseline"] = baseline_aborts[slot]
            assert all(
                r["status"] == "error"
                and r["exit_code"] == -6
                and "disconnect_external_propagator" in r.get("stderr_head", "")
                for r in pair.values()
            )
            triaged_aborts.append(slot)
            rows = list(pair.values())
        assert len(rows) == len(pair) == 2
        assert set(pair) == {"baseline", "tightened"}
        assert len({r["answer"] for r in rows if solved(r)}) <= 1
        assert slot in triaged_aborts or all(
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
            model_coverage_hashes[str(slot)] = hashlib.sha256(
                coverage.read_bytes()
            ).hexdigest()
            value = json.loads(coverage.read_text())
            assert value["selected_sat"] == value["direct_dolmen_pass"] == selected
            assert {r["profile"] for r in value["rows"]} == {
                r["profile"] for r in rows if solved(r) and r["answer"] == "sat"
            }
            for validated in value["rows"]:
                model_path = directory / "models" / validated["profile"] / "summary.csv"
                assert (
                    hashlib.sha256(model_path.read_bytes()).hexdigest()
                    == validated["summary_sha256"]
                )
                with model_path.open() as model_stream:
                    (model,) = csv.DictReader(model_stream)
                assert model["outcome"] == "pass" and model["validator"] == "dolmen"
                assert model["solver_exit"] == model["dolmen_exit"] == "0"
            model_pass += selected
        elif complete or selected:
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
        "baseline_abort_receipt_sha256": baseline_abort_hashes,
        "model_coverage_sha256": model_coverage_hashes,
        "paired_timing_complete": len(pairs) - len(triaged_aborts),
        "triaged_common_abort_slots": triaged_aborts,
        "qualification_status": "blocked by native aborts; no performance acceptance"
        if triaged_aborts
        else "check model and repeat coverage before acceptance",
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
        "by_family": {
            family: summarize(
                [
                    p
                    for p, c in zip(pairs, config["cases"], strict=True)
                    if c["logic"] + "/" + c["file"].split("/")[0] == family
                ],
                config["timeout"],
            )
            for family in sorted(
                {c["logic"] + "/" + c["file"].split("/")[0] for c in config["cases"]}
            )
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
