#!/usr/bin/env python3
"""Strictly audit the frozen P/R/H matrix and report paired factorial effects."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.profiles import (  # noqa: E402
    profiles,
)
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    METRICS as BASE_METRICS,
)
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    outcome,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402
from scripts.qualification.propagation_redundancy.analyze import counters  # noqa: E402
from scripts.qualification.propagation_redundancy.full_analysis import (  # noqa: E402
    METRICS as ROW_METRICS,
)

METRICS = tuple(
    dict.fromkeys(
        (
            *BASE_METRICS,
            *ROW_METRICS,
            *(
                ":lia.branching." + k
                for k in (
                    "budget-exits",
                    "split-requests",
                    "unique-atoms",
                    "reused-atoms",
                    "sat-resumes",
                    "choice-conflicts",
                    "peak-generated-atoms",
                    "existing-coordinate-registrations",
                    "split-budget-exhaustions",
                )
            ),
            ":time.lia.branching.resumed-sat-us",
        )
    )
)
EDGES = (
    ("B", "P"),
    ("R", "PR"),
    ("H", "PH"),
    ("RH", "PRH"),
    ("B", "R"),
    ("P", "PR"),
    ("H", "RH"),
    ("PH", "PRH"),
    ("B", "H"),
    ("P", "PH"),
    ("R", "RH"),
    ("PR", "PRH"),
)
SQUARES = {
    "PxR_H0": ("B", "P", "R", "PR"),
    "PxR_H1": ("H", "PH", "RH", "PRH"),
    "PxH_R0": ("B", "P", "H", "PH"),
    "PxH_R1": ("R", "PR", "RH", "PRH"),
    "RxH_P0": ("B", "R", "H", "RH"),
    "RxH_P1": ("P", "PR", "PH", "PRH"),
}


def geomean(values: list[float]) -> float | None:
    return math.exp(st.mean(math.log(v) for v in values)) if values else None


def metric(row: dict, key: str) -> int | None:
    # These are increment-only counters in one fixed binary. Missing counts
    # are zero only on complete successful rows with a statistics block.
    return int(row["stats"].get(key, 0)) if solved(row) and row["stats"] else None


def describe(rows: list[dict], limit: float) -> dict:
    available = {
        k: [v for r in rows if (v := metric(r, k)) is not None] for k in METRICS
    }
    return {
        "solved": sum(map(solved, rows)),
        "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
        "mean_PAR2_seconds": st.mean(penalty(r, limit) for r in rows),
        "median_RSS_KiB": st.median(r["peak_rss_kib"] for r in rows),
        "max_RSS_KiB": max(r["peak_rss_kib"] for r in rows),
        "successful_rows_with_statistics": sum(
            solved(r) and bool(r["stats"]) for r in rows
        ),
        "successful_counter_totals": {
            k: sum(v) if v else None for k, v in available.items()
        },
        "successful_peak_generated_atoms": max(
            available[":lia.branching.peak-generated-atoms"], default=None
        ),
        "cases_with_activity": {
            key: sum((metric(r, key) or 0) > 0 for r in rows)
            for key in (
                ":preprocessing.bounds.emitted",
                ":lra.row-propagations",
                ":lia.branching.split-requests",
            )
        },
    }


def compare(
    records: dict, slots: list[int], before: str, after: str, limit: float
) -> dict:
    def row(s, r, p):
        return records[r, s, p]

    common = [
        s
        for s in slots
        if all(solved(row(s, r, p)) for r in range(3) for p in (before, after))
    ]
    slow, details, regressions = [], [], []
    for s in common:
        old = [row(s, r, before)["seconds"] for r in range(3)]
        new = [row(s, r, after)["seconds"] for r in range(3)]
        ratio = st.median(b / a for a, b in zip(old, new, strict=True))
        if st.median(old) >= 1:
            slow.append(ratio)
        if max(st.median(old), st.median(new)) >= 1:
            item = {
                "slot": s,
                "before_seconds": old,
                "after_seconds": new,
                "paired_median_ratio": ratio,
            }
            details.append(item)
            if all(
                b >= 1.1 * a and b - a >= 0.5 for a, b in zip(old, new, strict=True)
            ):
                regressions.append(item)
    reps = []
    for r in range(3):
        reps.append(
            {
                "gained": [
                    s
                    for s in slots
                    if not solved(row(s, r, before)) and solved(row(s, r, after))
                ],
                "lost": [
                    s
                    for s in slots
                    if solved(row(s, r, before)) and not solved(row(s, r, after))
                ],
                "mean_PAR2_delta_seconds": st.mean(
                    penalty(row(s, r, after), limit) - penalty(row(s, r, before), limit)
                    for s in slots
                ),
            }
        )
    matched = [
        (s, r)
        for s in common
        for r in range(3)
        if row(s, r, before)["stats"] and row(s, r, after)["stats"]
    ]
    return {
        "matched_complete_counter_pairs": len(matched),
        "common_solved_counter_pairs_without_statistics": len(common) * 3
        - len(matched),
        "repetitions": reps,
        "stable_gains": sorted(set.intersection(*(set(r["gained"]) for r in reps))),
        "stable_losses": sorted(set.intersection(*(set(r["lost"]) for r in reps))),
        "common_solved_all_repetitions": len(common),
        "common_before_median_at_least_1s": len(slow),
        "conditional_geomean_paired_median_ratio": geomean(slow),
        "common_case_details_either_median_at_least_1s": details,
        "regression_followups": regressions,
        "matched_complete_counter_totals": {
            k: {
                p: sum(metric(row(s, r, p), k) for s, r in matched) if matched else None
                for p in (before, after)
            }
            for k in METRICS
        },
    }


def interaction(records: dict, slots: list[int], corners: tuple[str, ...]) -> dict:
    eligible, ratios = [], []
    c00, c10, c01, c11 = corners
    for s in slots:
        if not all(solved(records[r, s, p]) for r in range(3) for p in corners):
            continue
        if st.median(records[r, s, c00]["seconds"] for r in range(3)) < 1:
            continue
        q = []
        for r in range(3):
            t = {p: records[r, s, p]["seconds"] for p in corners}
            q.append((t[c11] / t[c01]) / (t[c10] / t[c00]))
        eligible.append(
            {"slot": s, "repetition_ratios": q, "median_ratio": st.median(q)}
        )
        ratios.append(st.median(q))
    return {
        "corners_00_10_01_11": corners,
        "eligible_cases": len(eligible),
        "geomean_case_median_ratio": geomean(ratios),
        "cases": eligible,
    }


def summarize(records: dict, slots: list[int], labels: list[str], limit: float) -> dict:
    result = {"cases": len(slots), "profiles": {}, "edges": {}, "interactions": {}}
    for p in labels:
        reps = [describe([records[r, s, p] for s in slots], limit) for r in range(3)]
        result["profiles"][p] = {
            "repetitions": reps,
            "median_solved": st.median(r["solved"] for r in reps),
            "median_mean_PAR2_seconds": st.median(r["mean_PAR2_seconds"] for r in reps),
            "stable_solved": sum(
                all(solved(records[r, s, p]) for r in range(3)) for s in slots
            ),
        }
    for before, after in EDGES:
        if before in labels and after in labels:
            result["edges"][before + "->" + after] = compare(
                records, slots, before, after, limit
            )
    for name, corners in SQUARES.items():
        if set(corners) <= set(labels):
            result["interactions"][name] = interaction(records, slots, corners)
    result["oracle_union_per_repetition"] = [
        sum(any(solved(records[r, s, p]) for p in labels) for s in slots)
        for r in range(3)
    ]
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("configs", "rows", "protocol", "selection", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(
        "--partial",
        action="store_true",
        help="Audit available durable rows only; no performance report",
    )
    parser.add_argument("--allow-pending-models", action="store_true")
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_text())
    selection = json.loads(args.selection.read_text())
    campaign = json.loads((args.configs / "campaign.json").read_text())
    assert sha(args.selection) == protocol["selection_sha256"]
    cases = selection["cases"]
    assert len(cases) == len(campaign["mapping"]) == protocol["inputs"] == 697
    assert campaign["git"] == protocol["native_source_snapshot"]
    assert campaign["sha256"] == protocol["native_binary_sha256"]
    configs, records, receipts, diagnostics = {}, {}, [], []
    pending_models, selected_sat, validated_sat = 0, 0, 0
    for rep in range(3):
        for cohort in ("integers", "reals"):
            path = args.configs / f"{cohort}-{rep}.json"
            config = json.loads(path.read_text())
            configs[rep, cohort] = config
            assert config["timeout"] == protocol["seconds"]
            assert config["address_space_bytes"] == protocol["address_space_bytes"]
            for p, profile in config["profiles"].items():
                assert (
                    profile["git"] == campaign["git"]
                    and profile["sha256"] == campaign["sha256"]
                )
                assert profile["args"] == profiles(config["cases"][0]["logic"])[p]
        for slot, (case, mapping) in enumerate(
            zip(cases, campaign["mapping"], strict=True)
        ):
            cohort, local = mapping["cohort"], mapping["slot"]
            config = configs[rep, cohort]
            expected_case = dict(case, campaign_slot=slot, repetition=rep)
            assert config["cases"][local] == expected_case
            directory = args.rows / f"rep-{rep}/{cohort}/slot-{local}"
            source = directory / "rows.jsonl"
            if args.partial and not source.exists():
                continue
            complete = (directory / "timing-complete").is_file()
            assert args.partial or complete, directory
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["runner_sha256"] == sha(
                ROOT / "scripts/qualification/lia_hybrid/run.py"
            )
            assert provenance["config_sha256"] == sha(
                args.configs / f"{cohort}-{rep}.json"
            )
            assert (
                provenance["case"] == expected_case
                and provenance["profiles"] == config["profiles"]
            )
            assert provenance["order"] == measurement_order(config, local)
            lines = source.read_text().splitlines()
            rows = []
            for i, line in enumerate(lines):
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    assert args.partial and not complete and i == len(lines) - 1
            assert [r["profile"] for r in rows] == provenance["order"][: len(rows)]
            if complete and not args.partial:
                assert len(rows) == len(config["profiles"])
            for row in rows:
                row["seconds"] = float(row["seconds"])
                # Never print a complete statistics dictionary in failure output.
                try:
                    audit_row(row)
                except AssertionError:
                    raise RuntimeError(
                        f"Native failure rep={rep} slot={slot} profile={row['profile']} status={row['status']} exit={row['exit_code']}"
                    ) from None
                assert row["file"] == case["file"] and row["logic"] == case["logic"]
                assert row["args"] == config["profiles"][row["profile"]][
                    "args"
                ] + case.get("extra_args", [])
                assert math.isfinite(row["seconds"]) and row["seconds"] > 0
                assert row["peak_rss_kib"] >= 0
                if solved(row):
                    assert row["answer"] in ("sat", "unsat")
                    if case.get("expected") in ("sat", "unsat"):
                        assert row["answer"] == case["expected"], (
                            rep,
                            slot,
                            row["profile"],
                        )
                    counters(row)
                if (
                    row["status"] == "ok"
                    and row["answer"] == "unknown"
                    or row["status"] == "error"
                ):
                    stderr = directory / (row["profile"] + ".stderr")
                    if args.partial and not stderr.exists():
                        continue  # A live rsync can capture a row before its earlier-written log.
                    diagnostic = [
                        s
                        for s in stderr.read_text().splitlines()
                        if s.strip() and not s.startswith(":")
                    ]
                    expected = (
                        ["integer branch-and-bound node limit"]
                        if row["status"] == "ok"
                        else ["std::bad_alloc"]
                    )
                    assert diagnostic == expected, (
                        rep,
                        slot,
                        row["profile"],
                        diagnostic,
                    )
                    diagnostics.append(
                        {
                            "repetition": rep,
                            "slot": slot,
                            "profile": row["profile"],
                            "diagnostic": diagnostic,
                            "stderr_sha256": sha(stderr),
                        }
                    )
                records[rep, slot, row["profile"]] = row
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            selected_sat += len(selected)
            if not args.partial:
                model_hashes = audit_models(directory, selected, config, expected_case)
                if model_hashes is None:
                    pending_models += 1
                    assert args.allow_pending_models, directory
                else:
                    validated_sat += len(selected)
                receipts.append(
                    {
                        "slot": slot,
                        "repetition": rep,
                        "rows_sha256": sha(source),
                        "provenance_sha256": sha(directory / "provenance.json"),
                        "coverage_sha256": sha(directory / "coverage.json")
                        if model_hashes is not None
                        else None,
                        "model_metadata_sha256": model_hashes,
                    }
                )
    for slot in range(len(cases)):
        answers = {
            row["answer"]
            for (_, s, _), row in records.items()
            if s == slot and solved(row)
        }
        assert len(answers) <= 1, ("answer disagreement", slot)
    if args.partial:
        print(
            json.dumps(
                {
                    "audited_durable_rows": len(records),
                    "diagnosed_resource_rows": len(diagnostics),
                }
            )
        )
        return
    assert len(records) == protocol["native_measurements"] == 14364
    labels = {
        cohort: list(configs[0, cohort]["profiles"]) for cohort in ("integers", "reals")
    }
    groups = {
        cohort: [s for s, m in enumerate(campaign["mapping"]) if m["cohort"] == cohort]
        for cohort in labels
    }
    groups.update(
        {
            logic: [s for s, c in enumerate(cases) if c["logic"] == logic]
            for logic in protocol["by_logic"]
        }
    )
    groups.update(
        {
            logic + "/" + family: [
                s
                for s, c in enumerate(cases)
                if c["logic"] == logic and c["file"].split("/")[0] == family
            ]
            for logic in protocol["by_logic"]
            for family in {
                c["file"].split("/")[0] for c in cases if c["logic"] == logic
            }
        }
    )
    report = {
        "status": "qualified"
        if not pending_models
        else "timings audited; models pending",
        "source": campaign["git"],
        "native_sha256": campaign["sha256"],
        "protocol_sha256": sha(args.protocol),
        "selection_sha256": sha(args.selection),
        "config_sha256": {
            f"{r}/{c}": sha(args.configs / f"{c}-{r}.json") for r, c in configs
        },
        "native_rows": len(records),
        "SAT_selected": selected_sat,
        "SAT_Dolmen_pass": validated_sat,
        "pending_model_allocations": pending_models,
        "receipts": receipts,
        "resource_diagnostics": diagnostics,
        "counter_scope": "Successful measurements with statistics only; missing increment-only counters zero on these rows. Interrupted counts unavailable. Row is a subset of total propagation; reverse a subset of row. Queue duplicate classification unavailable for untracked candidates. Phase times overlap; no sum attribution.",
        "summaries": {
            name: summarize(
                records,
                slots,
                labels[campaign["mapping"][slots[0]]["cohort"]],
                protocol["seconds"],
            )
            for name, slots in groups.items()
        },
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    fields = [
        "repetition",
        "slot",
        "logic",
        "file",
        "profile",
        "status",
        "answer",
        "exit_code",
        "seconds",
        "peak_rss_kib",
        "solved",
        "PAR2_seconds",
        *METRICS,
    ]
    with (args.out / "measurements.csv").open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for (rep, slot, _), row in sorted(records.items()):
            writer.writerow(
                {
                    **{k: row[k] for k in fields if k in row},
                    "repetition": rep,
                    "slot": slot,
                    "solved": solved(row),
                    "PAR2_seconds": penalty(row, protocol["seconds"]),
                    **{k: metric(row, k) for k in METRICS},
                }
            )
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k not in ("receipts", "resource_diagnostics", "summaries")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
