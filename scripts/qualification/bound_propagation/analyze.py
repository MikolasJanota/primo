#!/usr/bin/env python3
"""Audit three four-profile repetitions and report paired effects and mechanisms."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path

LABELS = ("default", "pre", "rows", "both")
ROOT = Path(__file__).resolve().parents[3]
METRICS = (
    ":lra.theory-propagations",
    ":lra.row-propagations",
    ":lra.bidirectional-row-propagations",
    ":lra.pivots",
    ":lra.pivot-row-updates",
    ":lra.pivot-term-ops",
    ":lra.online-check-calls",
    ":sat.conflicts",
    ":sat.decisions",
    ":sat.propagations",
    ":theory.conflicts",
    ":lia.branching.private-nodes",
    ":lia.branch-nodes",
    ":lia.branching.private-conflicts",
    ":time.lia.branching.private-completion-us",
    ":lra.tableau-vars",
    ":lra.rows",
    ":lra.row-nonzeros",
    ":rational.big-operations",
    ":preprocessing.bounds.emitted",
    ":preprocessing.bounds.constraints",
    ":preprocessing.bounds.newly-finite",
    ":preprocessing.bounds.contradictions",
    ":preprocessing.bounds.work",
    ":preprocessing.bounds.work-exhaustion",
    ":preprocessing.bounds.emission-truncated",
    ":preprocessing.bounds.time-us",
)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def solved(row: dict) -> bool:
    return (
        row["status"] == "ok"
        and row["exit_code"] == 0
        and row["answer"] in ("sat", "unsat")
    )


def penalty(row: dict, timeout: float) -> float:
    return float(row["seconds"]) if solved(row) else 2 * timeout


def outcome(row: dict) -> str:
    if solved(row):
        return row["answer"]
    if row["status"] == "timeout":
        return "timeout"
    if row.get("memory_out"):
        return "memory-out"
    return row["status"] if row["status"] != "ok" else row["answer"] or "missing-answer"


def audit_configs(
    configs: list[dict], selection: dict, protocol: dict, native: str
) -> None:
    assert len(configs) == protocol["repetitions"] == 3
    assert len(selection["cases"]) == protocol["inputs"]
    names = list(protocol["profiles"])
    assert set(names) == set(LABELS)
    for rep, config in enumerate(configs):
        assert config["cases"] == [dict(c, repetition=rep) for c in selection["cases"]]
        assert config["timeout"] == protocol["time_limit_seconds"]
        assert config["address_space_bytes"] == protocol["address_space_bytes"]
        assert config["roots"] == configs[0]["roots"]
        assert list(config["profiles"]) == names[rep:] + names[:rep]
        assert config["profiles"] == configs[0]["profiles"]
        for label, profile in config["profiles"].items():
            assert profile["args"] == protocol["profiles"][label]
            assert profile["sha256"] == native


def audit_row(row: dict) -> None:
    # Allocation text can accompany a cleanup abort. Only handled exit 2 is
    # resource exhaustion; a signal crash must still stop the campaign audit.
    if row["status"] == "timeout":
        assert row["exit_code"] in (0, 2, 124, -15), row
    elif row["status"] == "error":
        assert row.get("memory_out") and row["exit_code"] == 2, row
    else:
        assert row["status"] in ("ok", "unsupported") and row["exit_code"] == 0, row


def audit_models(directory: Path, selected: dict, config: dict, case: dict):
    coverage = directory / "coverage.json"
    if not coverage.is_file():
        return None
    value = json.loads(coverage.read_text())
    assert value["selected_sat"] == value["direct_dolmen_pass"] == len(selected)
    assert len(value["rows"]) == len(selected)
    assert {r["profile"] for r in value["rows"]} == set(selected)
    metadata_hashes = {}
    expected_source = str(Path(config["roots"][case["logic"]]) / case["file"])
    for item in value["rows"]:
        label = item["profile"]
        target = directory / "models" / label
        summary = target / "summary.csv"
        assert sha(summary) == item["summary_sha256"]
        with summary.open() as stream:
            (model,) = csv.DictReader(stream)
        assert model["file"] == expected_source and model["answer"] == "sat"
        assert model["outcome"] == "pass" and model["validator"] == "dolmen"
        assert model["solver_exit"] == model["dolmen_exit"] == "0"
        metadata_path = target / "metadata.json"
        metadata = json.loads(metadata_path.read_text())
        assert metadata["status"] == "complete"
        assert metadata["count"] == metadata["completed"] == 1
        assert metadata["solver"] == config["profiles"][label]["solver"]
        assert metadata["solver_args"] == selected[label]["args"] + ["--check-model"]
        assert metadata["expect_sat"] and metadata["require_dolmen_pass"]
        assert metadata["dolmen_args"] == ["--strict=false"]
        assert metadata["outcomes"] == {"pass": 1}
        assert metadata["validators"] == {"dolmen": 1}
        metadata_hashes[label] = sha(metadata_path)
    return metadata_hashes


def summarize(indices: list[int], records: dict, timeout: float) -> dict:
    result = {"cases": len(indices), "profiles": {}, "comparisons": {}}
    for label in LABELS:
        repetitions = []
        for rep in range(3):
            rows = [records[rep, slot, label] for slot in indices]
            totals = {
                key: sum(int(r["stats"].get(key, 0)) for r in rows) for key in METRICS
            }
            repetitions.append(
                {
                    "repetition": rep,
                    "solved": sum(solved(r) for r in rows),
                    "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
                    "mean_par2_seconds": statistics.mean(
                        penalty(r, timeout) for r in rows
                    ),
                    "median_peak_rss_kib": statistics.median(
                        r["peak_rss_kib"] for r in rows
                    ),
                    "max_peak_rss_kib": max(r["peak_rss_kib"] for r in rows),
                    "rows_with_printed_statistics": sum(bool(r["stats"]) for r in rows),
                    "rows_missing_statistics_by_outcome": dict(
                        collections.Counter(outcome(r) for r in rows if not r["stats"])
                    ),
                    "available_statistics_totals": totals,
                    "cases_delivering_row_propagations": sum(
                        int(r["stats"].get(":lra.row-propagations", 0)) > 0
                        for r in rows
                    ),
                    "cases_emitting_preprocessing_bounds": sum(
                        int(r["stats"].get(":preprocessing.bounds.emitted", 0)) > 0
                        for r in rows
                    ),
                }
            )
        scores = [r["mean_par2_seconds"] for r in repetitions]
        solved_counts = [r["solved"] for r in repetitions]
        stable = sum(
            all(solved(records[rep, slot, label]) for rep in range(3))
            for slot in indices
        )
        unstable = sum(
            0 < sum(solved(records[rep, slot, label]) for rep in range(3)) < 3
            for slot in indices
        )
        result["profiles"][label] = {
            "repetitions": repetitions,
            "solved_counts": solved_counts,
            "median_solved": statistics.median(solved_counts),
            "stable_solved_all_three": stable,
            "intermittent_solved": unstable,
            "median_mean_par2_seconds": statistics.median(scores),
            "range_mean_par2_seconds": [min(scores), max(scores)],
        }
    for before, after in [
        ("default", "pre"),
        ("default", "rows"),
        ("default", "both"),
        ("rows", "both"),
        ("pre", "both"),
    ]:
        common = [
            slot
            for slot in indices
            if all(
                solved(records[rep, slot, label])
                for rep in range(3)
                for label in (before, after)
            )
        ]
        ratios = [
            statistics.median(
                float(records[rep, slot, after]["seconds"])
                / max(float(records[rep, slot, before]["seconds"]), 1e-6)
                for rep in range(3)
            )
            for slot in common
        ]
        slower_cases = []
        faster_cases = []
        slow_ratios = []
        for slot, ratio in zip(common, ratios, strict=True):
            old_times = [float(records[r, slot, before]["seconds"]) for r in range(3)]
            new_times = [float(records[r, slot, after]["seconds"]) for r in range(3)]
            if statistics.median(old_times) >= 1:
                slow_ratios.append(ratio)
            slower = all(
                b >= 2 * a and b - a >= 1
                for a, b in zip(old_times, new_times, strict=True)
            )
            faster = all(
                a >= 2 * b and a - b >= 1
                for a, b in zip(old_times, new_times, strict=True)
            )
            if slower or faster:
                item = {
                    "slot": slot,
                    "before_seconds": old_times,
                    "after_seconds": new_times,
                    "median_seconds_delta": statistics.median(new_times)
                    - statistics.median(old_times),
                }
                (slower_cases if slower else faster_cases).append(item)
        per_rep = []
        for rep in range(3):
            per_rep.append(
                {
                    "repetition": rep,
                    "gained": sum(
                        not solved(records[rep, s, before])
                        and solved(records[rep, s, after])
                        for s in indices
                    ),
                    "lost": sum(
                        solved(records[rep, s, before])
                        and not solved(records[rep, s, after])
                        for s in indices
                    ),
                    "mean_par2_delta_seconds": statistics.mean(
                        penalty(records[rep, s, after], timeout)
                        - penalty(records[rep, s, before], timeout)
                        for s in indices
                    ),
                }
            )
        stable_gains = [
            s
            for s in indices
            if all(
                not solved(records[r, s, before]) and solved(records[r, s, after])
                for r in range(3)
            )
        ]
        stable_losses = [
            s
            for s in indices
            if all(
                solved(records[r, s, before]) and not solved(records[r, s, after])
                for r in range(3)
            )
        ]
        counters = {}
        for key in METRICS:
            pairs = [
                (records[r, s, before], records[r, s, after])
                for s in common
                for r in range(3)
                if records[r, s, before]["stats"] and records[r, s, after]["stats"]
            ]
            counters[key] = {
                "matched_complete_rows": len(pairs),
                "before_sum": sum(int(a["stats"].get(key, 0)) for a, _ in pairs),
                "after_sum": sum(int(b["stats"].get(key, 0)) for _, b in pairs),
            }
        result["comparisons"][before + "->" + after] = {
            "repetitions": per_rep,
            "stable_gained_slots": stable_gains,
            "stable_lost_slots": stable_losses,
            "common_solved_all_three": len(common),
            "median_of_case_paired_median_ratios": statistics.median(ratios)
            if ratios
            else None,
            "geometric_mean_case_paired_median_ratios": math.exp(
                statistics.mean(math.log(r) for r in ratios)
            )
            if ratios
            else None,
            "matched_complete_counter_totals": counters,
            "common_solved_before_median_at_least_one_second": len(slow_ratios),
            "geometric_mean_paired_median_ratio_before_at_least_one_second": math.exp(
                statistics.mean(math.log(r) for r in slow_ratios)
            )
            if slow_ratios
            else None,
            "largest_consistent_runtime_slowdowns": sorted(
                slower_cases, key=lambda x: -x["median_seconds_delta"]
            )[:20],
            "largest_consistent_runtime_improvements": sorted(
                faster_cases, key=lambda x: x["median_seconds_delta"]
            )[:20],
        }
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
    evidence = ROOT / "benchmarks/bound-propagation-20261113"
    protocol_path = evidence / "protocol.json"
    selection_path = evidence / "selection.json"
    qualification = json.loads((evidence / "qualification.json").read_text())
    audit_configs(
        configs,
        json.loads(selection_path.read_text()),
        json.loads(protocol_path.read_text()),
        qualification["native_sha256"],
    )
    cases = configs[0]["cases"]
    assert len(cases) == 697
    records = {}
    receipt_hashes = {}
    coverage_hashes = {}
    replay_metadata_hashes = {}
    selected_sat = validated_sat = 0
    models_complete = True
    runner_sha256 = sha(ROOT / "scripts/qualification/lia_hybrid/run.py")
    for rep, config in enumerate(configs):
        assert set(config["profiles"]) == set(LABELS)
        assert config["profiles"] == configs[0]["profiles"]
        assert len({p["sha256"] for p in config["profiles"].values()}) == 1
        for slot, case in enumerate(config["cases"]):
            assert (case["logic"], case["file"], case["sha256"]) == (
                cases[slot]["logic"],
                cases[slot]["file"],
                cases[slot]["sha256"],
            )
            directory = args.rows / f"rep-{rep}/slot-{slot}"
            assert (directory / "timing-complete").is_file(), directory
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["runner_sha256"] == runner_sha256
            assert provenance["config_sha256"] == sha(
                args.configs / f"config-{rep}.json"
            )
            assert (
                provenance["profiles"] == config["profiles"]
                and provenance["case"] == case
            )
            names = list(config["profiles"])
            assert provenance["order"] == names[slot % 4 :] + names[: slot % 4]
            source = directory / "rows.jsonl"
            rows = [json.loads(s) for s in source.read_text().splitlines()]
            assert len(rows) == 4 and {r["profile"] for r in rows} == set(LABELS)
            assert [r["profile"] for r in rows] == provenance["order"]
            assert len({r["answer"] for r in rows if solved(r)}) <= 1
            for row in rows:
                assert row["file"] == case["file"] and row["logic"] == case["logic"]
                assert row["args"] == config["profiles"][row["profile"]][
                    "args"
                ] + case.get("extra_args", [])
                audit_row(row)
                if solved(row) and case.get("expected") in ("sat", "unsat"):
                    assert row["answer"] == case["expected"], row
                if solved(row):
                    total = int(row["stats"].get(":lra.theory-propagations", 0))
                    forward_reverse = int(row["stats"].get(":lra.row-propagations", 0))
                    reverse = int(
                        row["stats"].get(":lra.bidirectional-row-propagations", 0)
                    )
                    assert total >= forward_reverse >= reverse
                records[rep, slot, row["profile"]] = row
            receipt_hashes[f"{rep}/{slot}"] = sha(source)
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            selected_sat += len(selected)
            metadata_hashes = audit_models(directory, selected, config, case)
            if metadata_hashes is None:
                models_complete = False
                continue
            replay_metadata_hashes.update(
                {f"{rep}/{slot}/{label}": h for label, h in metadata_hashes.items()}
            )
            validated_sat += len(selected)
            coverage_hashes[f"{rep}/{slot}"] = sha(directory / "coverage.json")
    for slot in range(len(cases)):
        assert (
            len(
                {
                    records[r, slot, label]["answer"]
                    for r in range(3)
                    for label in LABELS
                    if solved(records[r, slot, label])
                }
            )
            <= 1
        )
    indices = list(range(len(cases)))
    report = {
        "status": "timing and selected SAT-model audit complete"
        if models_complete
        else "timing complete; SAT-model audit pending",
        "scope": "697predeclared common instances;3prior abort quarantines excluded,not passes;existing settings only,no default promotion",
        "profiles": {
            label: {
                k: v for k, v in configs[0]["profiles"][label].items() if k != "solver"
            }
            for label in LABELS
        },
        "config_sha256": {
            str(r): sha(args.configs / f"config-{r}.json") for r in range(3)
        },
        "runner_sha256": runner_sha256,
        "protocol_sha256": sha(protocol_path),
        "selection_sha256": sha(selection_path),
        "timing_receipt_sha256": receipt_hashes,
        "model_coverage_sha256": coverage_hashes,
        "model_replay_metadata_sha256": replay_metadata_hashes,
        "counter_semantics": "Available printed totals only. Reverse is a subset of row; row is a subset of total arithmetic propagations. Matched-counter comparisons use common solved cases in all3repetitions,with printed statistics; absent increment-only counters on these complete rows count as zero. No full bound-survival attribution.",
        "models": {
            "complete": models_complete,
            "expected_sat": selected_sat,
            "direct_dolmen_pass": validated_sat,
        },
        "all": summarize(indices, records, configs[0]["timeout"]),
        "by_logic": {
            logic: summarize(
                [i for i in indices if cases[i]["logic"] == logic],
                records,
                configs[0]["timeout"],
            )
            for logic in sorted({c["logic"] for c in cases})
        },
        "by_family": {
            family: summarize(
                [
                    i
                    for i in indices
                    if cases[i]["logic"] + "/" + cases[i]["file"].split("/")[0]
                    == family
                ],
                records,
                configs[0]["timeout"],
            )
            for family in sorted(
                {c["logic"] + "/" + c["file"].split("/")[0] for c in cases}
            )
        },
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    with (args.out / "measurements.csv").open("w") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "repetition",
                "slot",
                "original_slot",
                "logic",
                "file",
                "profile",
                "status",
                "answer",
                "exit_code",
                "solved",
                "seconds",
                "peak_rss_kib",
                *METRICS,
            ]
        )
        for (rep, slot, label), row in sorted(records.items()):
            case = cases[slot]
            writer.writerow(
                [
                    rep,
                    slot,
                    case["original_slot"],
                    case["logic"],
                    case["file"],
                    label,
                    row["status"],
                    row["answer"],
                    row["exit_code"],
                    solved(row),
                    row["seconds"],
                    row["peak_rss_kib"],
                    *(row["stats"].get(key, "") for key in METRICS),
                ]
            )
    print(
        json.dumps(
            {
                "models": report["models"],
                "profiles": {
                    label: {k: v for k, v in value.items() if k != "repetitions"}
                    for label, value in report["all"]["profiles"].items()
                },
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
