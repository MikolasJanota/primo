#!/usr/bin/env python3
"""Audit paired repair measurements and their separate complete SAT replays."""

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
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    METRICS,
    audit_models,
    audit_row,
    outcome,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402

PROFILES = ("default", "pre", "rows", "both")
VARIANTS = ("baseline", "candidate")
NEW_COUNTERS = (
    ":lra.basic-variable-row-scans",
    ":lra.integer-rounding-entailments",
    ":lra.row-bound-calculations",
    ":lra.row-bound-candidate-comparisons",
    ":lra.row-bound-explanations",
)
COUNTERS = (*METRICS, *NEW_COUNTERS)


def summarize(slots: list[int], records: dict, timeout: float) -> dict:
    result = {"cases": len(slots), "profiles": {}}
    for profile in PROFILES:
        variants = {}
        for variant in VARIANTS:
            reps = []
            for rep in range(3):
                rows = [records[rep, s, f"{variant}-{profile}"] for s in slots]
                reps.append(
                    {
                        "solved": sum(solved(r) for r in rows),
                        "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
                        "mean_par2_seconds": statistics.mean(
                            penalty(r, timeout) for r in rows
                        ),
                        "median_peak_rss_kib": statistics.median(
                            r["peak_rss_kib"] for r in rows
                        ),
                        "max_peak_rss_kib": max(r["peak_rss_kib"] for r in rows),
                        "missing_stats": dict(
                            collections.Counter(
                                outcome(r) for r in rows if not r["stats"]
                            )
                        ),
                        "available_counter_totals": {
                            k: None
                            if variant == "baseline" and k in NEW_COUNTERS
                            else sum(int(r["stats"].get(k, 0)) for r in rows)
                            for k in COUNTERS
                        },
                    }
                )
            variants[variant] = {
                "repetitions": reps,
                "median_solved": statistics.median(r["solved"] for r in reps),
                "median_mean_par2_seconds": statistics.median(
                    r["mean_par2_seconds"] for r in reps
                ),
            }
        common, gains, losses, faster, slower = [], [], [], [], []
        ratios = []
        for slot in slots:
            pairs = [
                (
                    records[r, slot, f"baseline-{profile}"],
                    records[r, slot, f"candidate-{profile}"],
                )
                for r in range(3)
            ]
            if all(not solved(a) and solved(b) for a, b in pairs):
                gains.append(slot)
            if all(solved(a) and not solved(b) for a, b in pairs):
                losses.append(slot)
            if not all(solved(a) and solved(b) for a, b in pairs):
                continue
            common.append(slot)
            before = [float(a["seconds"]) for a, _ in pairs]
            after = [float(b["seconds"]) for _, b in pairs]
            ratio = statistics.median(
                b / max(a, 1e-6) for a, b in zip(before, after, strict=True)
            )
            ratios.append((slot, ratio, statistics.median(before)))
            item = {
                "slot": slot,
                "baseline_seconds": before,
                "candidate_seconds": after,
                "paired_median_ratio": ratio,
            }
            if all(
                b >= 2 * a and b - a >= 1 for a, b in zip(before, after, strict=True)
            ):
                slower.append(item)
            if all(
                a >= 2 * b and a - b >= 1 for a, b in zip(before, after, strict=True)
            ):
                faster.append(item)
        slow_ratios = [ratio for _, ratio, old in ratios if old >= 1]
        variants["comparison"] = {
            "stable_gained_slots": gains,
            "stable_lost_slots": losses,
            "common_solved_all_three": len(common),
            "geomean_paired_median_ratio": math.exp(
                statistics.mean(math.log(v) for _, v, _ in ratios)
            )
            if ratios
            else None,
            "common_baseline_at_least_one_second": len(slow_ratios),
            "geomean_paired_median_ratio_baseline_at_least_one_second": math.exp(
                statistics.mean(math.log(v) for v in slow_ratios)
            )
            if slow_ratios
            else None,
            "consistent_twofold_improvements": sorted(
                faster, key=lambda r: r["paired_median_ratio"]
            ),
            "consistent_twofold_regressions": sorted(
                slower, key=lambda r: -r["paired_median_ratio"]
            ),
            "paired_solved_deltas": [
                sum(
                    solved(records[r, s, f"candidate-{profile}"])
                    - solved(records[r, s, f"baseline-{profile}"])
                    for s in slots
                )
                for r in range(3)
            ],
            "matched_complete_rows": sum(
                bool(records[r, s, f"baseline-{profile}"]["stats"])
                and bool(records[r, s, f"candidate-{profile}"]["stats"])
                for s in common
                for r in range(3)
            ),
            "matched_counters": {
                k: {
                    v: None
                    if v == "baseline" and k in NEW_COUNTERS
                    else sum(
                        int(records[r, s, f"{v}-{profile}"]["stats"].get(k, 0))
                        for s in common
                        for r in range(3)
                        if records[r, s, f"baseline-{profile}"]["stats"]
                        and records[r, s, f"candidate-{profile}"]["stats"]
                    )
                    for v in VARIANTS
                }
                for k in COUNTERS
            },
        }
        result["profiles"][profile] = variants
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    configs = [
        json.loads((args.configs / f"config-{r}.json").read_text()) for r in range(3)
    ]
    source = ROOT / "benchmarks/bound-propagation-20261113"
    selection = json.loads((source / "selection.json").read_text())
    protocol = json.loads((source / "protocol.json").read_text())
    records, receipts, coverage = {}, {}, {}
    expected_sat = validated_sat = 0
    labels = {f"{v}-{p}" for v in VARIANTS for p in PROFILES}
    for rep, config in enumerate(configs):
        assert config["cases"] == [dict(c, repetition=rep) for c in selection["cases"]]
        assert config["timeout"] == protocol["time_limit_seconds"] == 120
        assert config["address_space_bytes"] == protocol["address_space_bytes"]
        assert (
            config["profiles"] == configs[0]["profiles"]
            and set(config["profiles"]) == labels
        )
        assert config["paired_groups"] == [
            [f"{v}-{p}" for v in VARIANTS] for p in PROFILES
        ]
        for label, profile in config["profiles"].items():
            assert profile["args"] == protocol["profiles"][label.split("-", 1)[1]]
        for slot, case in enumerate(config["cases"]):
            directory = args.rows / f"rep-{rep}/slot-{slot}"
            assert (directory / "timing-complete").is_file(), directory
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["runner_sha256"] == sha(
                ROOT / "scripts/qualification/lia_hybrid/run.py"
            )
            assert provenance["config_sha256"] == sha(
                args.configs / f"config-{rep}.json"
            )
            assert (
                provenance["profiles"] == config["profiles"]
                and provenance["case"] == case
            )
            rows = [
                json.loads(line)
                for line in (directory / "rows.jsonl").read_text().splitlines()
            ]
            assert len(rows) == 8 and {r["profile"] for r in rows} == labels
            assert (
                provenance["order"]
                == measurement_order(config, slot)
                == [r["profile"] for r in rows]
            )
            for row in rows:
                assert row["file"] == case["file"] and row["logic"] == case["logic"]
                assert row["args"] == config["profiles"][row["profile"]][
                    "args"
                ] + case.get("extra_args", [])
                audit_row(row)
                if solved(row) and case.get("expected") in {"sat", "unsat"}:
                    assert row["answer"] == case["expected"], row
                records[rep, slot, row["profile"]] = row
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            expected_sat += len(selected)
            models = audit_models(directory, selected, config, case)
            if models is not None:
                validated_sat += len(selected)
                coverage[f"{rep}/{slot}"] = {
                    "receipt_sha256": sha(directory / "coverage.json"),
                    "replay_metadata_sha256": models,
                }
            receipts[f"{rep}/{slot}"] = {
                "rows_sha256": sha(directory / "rows.jsonl"),
                "provenance_sha256": sha(directory / "provenance.json"),
            }
    cases = configs[0]["cases"]
    for slot in range(len(cases)):
        assert (
            len(
                {
                    records[r, slot, label]["answer"]
                    for r in range(3)
                    for label in labels
                    if solved(records[r, slot, label])
                }
            )
            <= 1
        )
    indices = list(range(len(cases)))
    groups = {"all": indices}
    for case in cases:
        groups.setdefault(case["logic"], [])
        groups.setdefault(case["logic"] + "/" + case["file"].split("/")[0], [])
    for slot, case in enumerate(cases):
        groups[case["logic"]].append(slot)
        groups[case["logic"] + "/" + case["file"].split("/")[0]].append(slot)
    report = {
        "status": "qualified" if len(coverage) == 3 * len(cases) else "models pending",
        "selection_sha256": sha(source / "selection.json"),
        "protocol_sha256": sha(source / "protocol.json"),
        "profiles": {
            k: {field: value for field, value in v.items() if field != "solver"}
            for k, v in configs[0]["profiles"].items()
        },
        "config_sha256": [sha(args.configs / f"config-{r}.json") for r in range(3)],
        "runner_sha256": sha(ROOT / "scripts/qualification/lia_hybrid/run.py"),
        "counter_semantics": "Available printed totals only; matched totals require both rows with printed statistics. Baseline lacks five new counters: null means unavailable, not zero work. Missing increment-only keys on complete instrumented candidate rows count as zero. Rounding entailments are candidate tests, not deliveries.",
        "timing_receipts": receipts,
        "model_receipts": coverage,
        "models": {"selected_sat": expected_sat, "direct_dolmen_pass": validated_sat},
        "groups": {
            name: summarize(slots, records, config["timeout"])
            for name, slots in groups.items()
        },
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    with (args.out / "measurements.csv").open("w") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            [
                "repetition",
                "slot",
                "logic",
                "file",
                "profile",
                "status",
                "answer",
                "exit_code",
                "solved",
                "seconds",
                "peak_rss_kib",
                *COUNTERS,
            ]
        )
        for (rep, slot, label), row in sorted(records.items()):
            writer.writerow(
                [
                    rep,
                    slot,
                    row["logic"],
                    row["file"],
                    label,
                    row["status"],
                    row["answer"],
                    row["exit_code"],
                    solved(row),
                    row["seconds"],
                    row["peak_rss_kib"],
                    *(row["stats"].get(k, "") for k in COUNTERS),
                ]
            )
    print(
        json.dumps(
            {
                "status": report["status"],
                "models": report["models"],
                "all": report["groups"]["all"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
