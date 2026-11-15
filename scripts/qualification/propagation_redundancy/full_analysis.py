#!/usr/bin/env python3
"""Audit the two-profile full comparison and every successful SAT model replay."""

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
    audit_models,
    audit_row,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402

METRICS = (
    ":sat.decisions",
    ":sat.conflicts",
    ":sat.propagations",
    ":theory.conflicts",
    ":lra.pivots",
    ":lra.rows",
    ":lra.tableau-vars",
    ":lra.row-propagations",
    ":lra.bidirectional-row-propagations",
    ":lra.basic-variable-row-scans",
    ":lra.row-bound-calculations",
    ":lra.row-bound-candidate-comparisons",
    ":lra.row-bound-explanations",
    ":lra.bound-notifications",
    ":lra.bound-noop-notifications",
    ":lra.bound-tightening-notifications",
    ":lra.bound-conflicting-notifications",
    ":lra.propagation-untracked-candidates",
    ":lra.propagation-candidates",
    ":lra.propagation-unique-candidates",
    ":lra.propagation-duplicate-candidates",
    ":lra.propagation-discarded-assigned",
    ":lra.propagation-backtrack-discarded",
    ":lra.propagation-candidate-attempts",
    ":lra.propagation-suppressed-duplicates",
    ":lra.propagation-reason-literals",
    ":lra.row-scan-events",
    ":lra.row-scan-events-with-new-candidates",
    ":lra.row-scan-noop-events",
    ":lra.row-scan-repeated-state-skips",
    ":lia.branching.private-nodes",
    ":lia.branching.private-conflicts",
    ":time.lia.branching.private-completion-us",
    ":preprocessing.bounds.emitted",
    ":rational.big-operations",
)


def metric(row: dict, key: str) -> int | None:
    if not row["stats"]:
        return None
    if key not in row["stats"] and row["status"] != "ok":
        return None
    if row["variant"] == "baseline" and key.startswith(
        (":lra.bound-", ":lra.propagation-", ":lra.row-scan-")
    ):
        return None
    return int(row["stats"].get(key, 0))


def summary(rows: list[dict], limit: float) -> dict:
    assert rows
    return {
        "rows": len(rows),
        "solved": sum(solved(r) for r in rows),
        "outcomes": dict(
            collections.Counter(r["status"] + "/" + r["answer"] for r in rows)
        ),
        "mean_PAR2_seconds": statistics.mean(penalty(r, limit) for r in rows),
        "median_RSS_KiB": statistics.median(int(r["peak_rss_kib"]) for r in rows),
        "max_RSS_KiB": max(int(r["peak_rss_kib"]) for r in rows),
    }


def compare(records: list[dict], mode: str, slots: list[int], limit: float) -> dict:
    slot_set = set(slots)
    by = {
        (r["slot"], r["repetition"], r["variant"]): r
        for r in records
        if r["mode"] == mode and r["slot"] in slot_set
    }
    reps = []
    for rep in range(3):
        arms = {
            v: summary([by[slot, rep, v] for slot in slots], limit)
            for v in ("baseline", "candidate")
        }
        reps.append(arms)
    gains = [
        s
        for s in slots
        if all(
            not solved(by[s, r, "baseline"]) and solved(by[s, r, "candidate"])
            for r in range(3)
        )
    ]
    losses = [
        s
        for s in slots
        if all(
            solved(by[s, r, "baseline"]) and not solved(by[s, r, "candidate"])
            for r in range(3)
        )
    ]
    common = [
        s
        for s in slots
        if all(solved(by[s, r, v]) for r in range(3) for v in ("baseline", "candidate"))
    ]
    slow = [
        s
        for s in common
        if statistics.median(float(by[s, r, "baseline"]["seconds"]) for r in range(3))
        >= 1
    ]
    ratios = [
        statistics.median(
            float(by[s, r, "candidate"]["seconds"])
            / float(by[s, r, "baseline"]["seconds"])
            for r in range(3)
        )
        for s in slow
    ]
    search_keys = (
        ":sat.decisions",
        ":sat.conflicts",
        ":lra.pivots",
        ":theory.conflicts",
        ":lia.branching.private-nodes",
        ":lra.row-propagations",
    )
    search_differences = []
    checked_pairs = 0
    for slot in slots:
        for repetition in range(3):
            base, candidate = (
                by[slot, repetition, "baseline"],
                by[slot, repetition, "candidate"],
            )
            if not solved(base) or not solved(candidate):
                continue
            checked_pairs += 1
            differences = {
                key: [
                    int(base["stats"].get(key, 0)),
                    int(candidate["stats"].get(key, 0)),
                ]
                for key in search_keys
                if int(base["stats"].get(key, 0)) != int(candidate["stats"].get(key, 0))
            }
            if differences:
                search_differences.append(
                    {"slot": slot, "repetition": repetition, "counters": differences}
                )
    return {
        "search_pairs_checked": checked_pairs,
        "search_counter_differences": search_differences,
        "inputs": len(slots),
        "repetitions": reps,
        "median_solved": {
            v: statistics.median(rep[v]["solved"] for rep in reps)
            for v in ("baseline", "candidate")
        },
        "median_mean_PAR2_seconds": {
            v: statistics.median(rep[v]["mean_PAR2_seconds"] for rep in reps)
            for v in ("baseline", "candidate")
        },
        "paired_solved_deltas": [
            rep["candidate"]["solved"] - rep["baseline"]["solved"] for rep in reps
        ],
        "stable_gains": gains,
        "stable_losses": losses,
        "common_solved_all_repetitions": len(common),
        "common_baseline_at_least_1s": len(slow),
        "conditional_geomean_paired_median_ratio": math.exp(
            statistics.mean(math.log(x) for x in ratios)
        )
        if ratios
        else None,
        "ratio_scope": "Candidate/baseline paired ratios, median per case then geometric mean; excludes every gained/lost or cutoff-sensitive case and baseline medians below1s.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_text())
    selection = json.loads(args.selection.read_text())
    assert sha(args.selection) == protocol["selection_sha256"]
    assert len(selection["cases"]) == protocol["inputs"] == 697
    assert (
        list(protocol["profiles"]) == ["rows", "both"] and protocol["repetitions"] == 3
    )
    records, receipts = [], []
    runner_sha256 = sha(ROOT / "scripts/qualification/lia_hybrid/run.py")
    for rep in range(3):
        config_path = args.configs / f"config-{rep}.json"
        config = json.loads(config_path.read_text())
        config_sha256 = sha(config_path)
        assert config["cases"] == [dict(c, repetition=rep) for c in selection["cases"]]
        assert config["timeout"] == protocol["time_limit_seconds"]
        assert config["address_space_bytes"] == protocol["address_space_bytes"]
        expected_profiles = {
            f"{variant}-{mode}"
            for variant in ("baseline", "candidate")
            for mode in protocol["profiles"]
        }
        assert set(config["profiles"]) == expected_profiles
        for label, profile in config["profiles"].items():
            variant, mode = label.split("-", 1)
            assert profile["sha256"] == protocol[f"{variant}_sha256"]
            assert profile["git"] == protocol[f"{variant}_source"]
            assert profile["args"] == protocol["profiles"][mode]
        for slot, case in enumerate(config["cases"]):
            directory = args.rows / f"rep-{rep}/slot-{slot}"
            assert (directory / "timing-complete").is_file()
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["config_sha256"] == config_sha256
            assert provenance["runner_sha256"] == runner_sha256
            assert (
                provenance["profiles"] == config["profiles"]
                and provenance["case"] == case
            )
            rows = [
                json.loads(line)
                for line in (directory / "rows.jsonl").read_text().splitlines()
            ]
            assert len(rows) == 4 and {r["profile"] for r in rows} == expected_profiles
            assert (
                [r["profile"] for r in rows]
                == provenance["order"]
                == measurement_order(config, slot)
            )
            for row in rows:
                audit_row(row)
                assert (
                    math.isfinite(float(row["seconds"])) and float(row["seconds"]) > 0
                )
                assert int(row["peak_rss_kib"]) >= 0
                assert row["file"] == case["file"] and row["logic"] == case["logic"]
                assert row["args"] == config["profiles"][row["profile"]][
                    "args"
                ] + case.get("extra_args", [])
                if solved(row) and case.get("expected") in ("sat", "unsat"):
                    assert row["answer"] == case["expected"]
                variant, mode = row["profile"].split("-", 1)
                records.append(
                    {
                        **{
                            k: v
                            for k, v in row.items()
                            if k not in ("stdout_log", "stderr_log")
                        },
                        "slot": slot,
                        "repetition": rep,
                        "variant": variant,
                        "mode": mode,
                        "host": provenance["host"],
                        "affinity": provenance["affinity"],
                    }
                )
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            models = audit_models(directory, selected, config, case)
            assert models is not None, directory
            receipts.append(
                {
                    "slot": slot,
                    "repetition": rep,
                    "rows_sha256": sha(directory / "rows.jsonl"),
                    "provenance_sha256": sha(directory / "provenance.json"),
                    "coverage_sha256": sha(directory / "coverage.json"),
                    "model_metadata_sha256": models,
                    "selected_SAT": len(selected),
                }
            )
    assert len(records) == protocol["native_measurements"] == 8364
    selected_sat = sum(solved(r) and r["answer"] == "sat" for r in records)
    assert selected_sat == sum(r["selected_SAT"] for r in receipts)
    slots = list(range(697))
    limit = protocol["time_limit_seconds"]
    report = {
        "status": "qualified",
        "protocol_sha256": sha(args.protocol),
        "config_sha256": {
            str(r): sha(args.configs / f"config-{r}.json") for r in range(3)
        },
        "native_rows": len(records),
        "selected_SAT": selected_sat,
        "SAT_Dolmen_pass": selected_sat,
        "profiles": {
            m: compare(records, m, slots, limit) for m in protocol["profiles"]
        },
        "by_logic": {
            logic: {
                m: compare(
                    records,
                    m,
                    [
                        i
                        for i, c in enumerate(selection["cases"])
                        if c["logic"] == logic
                    ],
                    limit,
                )
                for m in protocol["profiles"]
            }
            for logic in protocol["by_logic"]
        },
        "by_family": {
            family: {
                m: compare(
                    records,
                    m,
                    [
                        i
                        for i, c in enumerate(selection["cases"])
                        if c["logic"] + "/" + c["file"].split("/")[0] == family
                    ],
                    limit,
                )
                for m in protocol["profiles"]
            }
            for family in sorted(
                {c["logic"] + "/" + c["file"].split("/")[0] for c in selection["cases"]}
            )
        },
        "hardware": {
            "hosts": sorted({r["host"] for r in records}),
            "observed_affinities": sorted({tuple(r["affinity"]) for r in records}),
            "scope": "One SLURMCPU requested; observed logical masks retained. Physical core mapping/isolation not established.",
        },
        "receipts": receipts,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / "measurements.csv").open("w", newline="") as stream:
        columns = [
            "slot",
            "repetition",
            "logic",
            "file",
            "mode",
            "variant",
            "status",
            "answer",
            "exit_code",
            "seconds",
            "solved",
            "PAR2_seconds",
            "peak_rss_kib",
            "stats",
        ]
        writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        for row in records:
            writer.writerow(
                {
                    **{k: row[k] for k in columns if k in row},
                    "solved": solved(row),
                    "PAR2_seconds": penalty(row, limit),
                    "stats": json.dumps(
                        row["stats"], sort_keys=True, separators=(",", ":")
                    ),
                }
            )
    report["measurements_sha256"] = sha(args.out / "measurements.csv")
    (args.out / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k
                in (
                    "status",
                    "native_rows",
                    "selected_SAT",
                    "SAT_Dolmen_pass",
                    "profiles",
                )
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
