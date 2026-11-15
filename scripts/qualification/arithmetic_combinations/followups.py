#!/usr/bin/env python3
"""Configure and audit fresh paired repeats of predeclared regression flags."""

from __future__ import annotations

import argparse
import copy
import json
import math
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import (  # noqa: E402
    metric,
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


def targets(report: dict) -> list[dict]:
    selected = []
    for logic in ("QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA"):
        for edge, result in report["summaries"][logic]["edges"].items():
            flags = {
                item["slot"]: ["consistent_runtime_regression"]
                for item in result["regression_followups"]
            }
            for slot in result["stable_losses"]:
                flags.setdefault(slot, []).append("stable_solve_loss")
            for slot, reasons in sorted(flags.items()):
                selected.append(
                    {"logic": logic, "edge": edge, "slot": slot, "reasons": reasons}
                )
    return selected


def counterbalanced_orders(
    initial: list[list[str]], before: str, after: str
) -> list[list[str]]:
    """Give each arm three first positions across the original and extra runs."""
    assert len(initial) == 3
    required = 3 - sum(order.index(before) < order.index(after) for order in initial)
    first = {
        0: [False, False, False],
        1: [False, True, False],
        2: [True, False, True],
        3: [True, True, True],
    }[required]
    return [[before, after] if value else [after, before] for value in first]


def configure(args) -> None:
    report = json.loads(args.analysis.read_text())
    assert report["native_rows"] == 14364
    campaign = json.loads((args.configs / "campaign.json").read_text())
    assert (
        report["source"] == campaign["git"]
        and report["native_sha256"] == campaign["sha256"]
    )
    selected = targets(report)
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = []
    mapping = []
    for index, target in enumerate(selected):
        entry = campaign["mapping"][target["slot"]]
        original_path = args.configs / f"{entry['cohort']}-0.json"
        assert sha(original_path) == report["config_sha256"][f"0/{entry['cohort']}"]
        original = json.loads(original_path.read_text())
        case = original["cases"][entry["slot"]]
        assert case["logic"] == target["logic"]
        before, after = target["edge"].split("->")
        initial, original_hashes = [], {}
        for rep in range(3):
            parent = args.configs / f"{entry['cohort']}-{rep}.json"
            original_hashes[str(rep)] = sha(parent)
            assert sha(parent) == report["config_sha256"][f"{rep}/{entry['cohort']}"]
            initial.append(
                measurement_order(json.loads(parent.read_text()), entry["slot"])
            )
        extra_orders = counterbalanced_orders(initial, before, after)
        manifest.append(
            {
                **target,
                "index": index,
                "file": case["file"],
                "input_sha256": case["sha256"],
                "original_config_sha256": sha(original_path),
                "original_config_sha256_by_repetition": original_hashes,
                "original_orders": initial,
                "additional_orders": extra_orders,
                "derived_config_sha256": {},
            }
        )
        cohort = f"case-{index}"
        mapping.append({"cohort": cohort, "slot": 0})
        for rep in range(3):
            config = copy.deepcopy(original)
            config["cases"] = [dict(case, repetition=rep + 3)]
            config["profiles"] = {p: original["profiles"][p] for p in (before, after)}
            # The frozen runner reverses a pair on odd repetition markers.
            desired = extra_orders[rep]
            config["paired_groups"] = [desired[::-1] if (rep + 3) % 2 else desired]
            config["out"] = f"{args.results_root}/rep-{rep}/{cohort}"
            derived = args.out / f"{cohort}-{rep}.json"
            derived.write_text(json.dumps(config, indent=2) + "\n")
            manifest[-1]["derived_config_sha256"][str(rep)] = sha(derived)
    master = {**campaign, "configs": args.remote_configs, "mapping": mapping}
    (args.out / "campaign.json").write_text(json.dumps(master, indent=2) + "\n")
    (args.out / "manifest.json").write_text(
        json.dumps(
            {
                "parent_analysis_sha256": sha(args.analysis),
                "targets": manifest,
                "repetitions": 3,
                "timing_rows": len(manifest) * 6,
            },
            indent=2,
        )
        + "\n"
    )
    print(
        json.dumps({"paired_targets": len(manifest), "timing_rows": len(manifest) * 6})
    )


def analyze(args) -> None:
    manifest = json.loads((args.configs / "manifest.json").read_text())
    campaign = json.loads((args.configs / "campaign.json").read_text())
    results = []
    selected_sat = validated_sat = 0
    for item in manifest["targets"]:
        index, edge = item["index"], item["edge"]
        before, after = edge.split("->")
        repeats, receipts = [], []
        for rep in range(3):
            path = args.configs / f"case-{index}-{rep}.json"
            assert sha(path) == item["derived_config_sha256"][str(rep)], (
                "follow-up configuration drift"
            )
            config = json.loads(path.read_text())
            case = config["cases"][0]
            directory = args.rows / f"rep-{rep}/case-{index}/slot-0"
            assert (directory / "timing-complete").is_file()
            provenance = json.loads((directory / "provenance.json").read_text())
            assert provenance["config_sha256"] == sha(path)
            assert provenance["runner_sha256"] == sha(
                ROOT / "scripts/qualification/lia_hybrid/run.py"
            )
            assert (
                provenance["case"] == case
                and provenance["profiles"] == config["profiles"]
            )
            assert (
                case["sha256"] == item["input_sha256"] and case["file"] == item["file"]
            )
            assert set(config["profiles"]) == {before, after}
            assert (
                config["timeout"] == 120 and config["address_space_bytes"] == 8589934592
            )
            rows = [
                json.loads(s)
                for s in (directory / "rows.jsonl").read_text().splitlines()
            ]
            assert len(rows) == 2
            assert (
                [r["profile"] for r in rows]
                == provenance["order"]
                == measurement_order(config, 0)
                == item["additional_orders"][rep]
            )
            for row in rows:
                try:
                    audit_row(row)
                except AssertionError:
                    raise RuntimeError(
                        f"Native followup failure target={index} rep={rep} profile={row['profile']} exit={row['exit_code']}"
                    ) from None
                profile = config["profiles"][row["profile"]]
                assert (
                    profile["git"] == campaign["git"]
                    and profile["sha256"] == campaign["sha256"]
                )
                assert row["args"] == profile["args"] + case.get("extra_args", [])
                assert row["logic"] == case["logic"] and row["file"] == case["file"]
                row["seconds"] = float(row["seconds"])
                assert math.isfinite(row["seconds"]) and row["seconds"] > 0
                if solved(row):
                    if case.get("expected") in ("sat", "unsat"):
                        assert row["answer"] == case["expected"]
                    counters(row)
                if (
                    row["status"] == "error"
                    or row["status"] == "ok"
                    and row["answer"] == "unknown"
                ):
                    diagnostics = [
                        s
                        for s in (directory / (row["profile"] + ".stderr"))
                        .read_text()
                        .splitlines()
                        if s.strip() and not s.startswith(":")
                    ]
                    assert diagnostics == (
                        ["std::bad_alloc"]
                        if row["status"] == "error"
                        else ["integer branch-and-bound node limit"]
                    )
            assert len({r["answer"] for r in rows if solved(r)}) <= 1
            selected = {
                r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"
            }
            selected_sat += len(selected)
            hashes = audit_models(directory, selected, config, case)
            assert hashes is not None
            validated_sat += len(selected)
            receipts.append(
                {
                    "repetition": rep,
                    "rows_sha256": sha(directory / "rows.jsonl"),
                    "provenance_sha256": sha(directory / "provenance.json"),
                    "coverage_sha256": sha(directory / "coverage.json"),
                    "model_metadata_sha256": hashes,
                }
            )
            repeats.append({r["profile"]: r for r in rows})
        assert (
            len({row["answer"] for r in repeats for row in r.values() if solved(row)})
            <= 1
        )
        common = all(solved(r[p]) for r in repeats for p in (before, after))
        ratios = (
            [r[after]["seconds"] / r[before]["seconds"] for r in repeats]
            if common
            else []
        )
        results.append(
            {
                **item,
                "receipts": receipts,
                "repeats": [
                    {
                        p: {
                            "outcome": outcome(row),
                            "seconds": row["seconds"],
                            "PAR2_seconds": penalty(row, 120),
                            "private_nodes": metric(
                                row, ":lia.branching.private-nodes"
                            ),
                            "split_requests": metric(
                                row, ":lia.branching.split-requests"
                            ),
                            "row_deliveries": metric(row, ":lra.row-propagations"),
                        }
                        for p, row in r.items()
                    }
                    for r in repeats
                ],
                "paired_ratios": ratios,
                "median_paired_ratio": st.median(ratios) if ratios else None,
                "consistent_runtime_regression_reproduced": common
                and all(
                    r[after]["seconds"] >= 1.1 * r[before]["seconds"]
                    and r[after]["seconds"] - r[before]["seconds"] >= 0.5
                    for r in repeats
                ),
                "stable_loss_reproduced": all(
                    solved(r[before]) and not solved(r[after]) for r in repeats
                ),
            }
        )
    report = {
        "status": "qualified",
        "source": campaign["git"],
        "native_sha256": campaign["sha256"],
        "manifest_sha256": sha(args.configs / "manifest.json"),
        "parent_analysis_sha256": manifest["parent_analysis_sha256"],
        "timing_rows": len(results) * 6,
        "SAT_selected": selected_sat,
        "SAT_Dolmen_pass": validated_sat,
        "results": results,
        "scope": "Three additional rotated paired repetitions, exact original profiles,120s/8GiB. All selected SAT separately Dolmen checked. Diagnostic repeats do not replace or alter original factorial measurements.",
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "results"}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("configure", "analyze"))
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--analysis", type=Path)
    parser.add_argument("--remote-configs")
    parser.add_argument("--results-root")
    parser.add_argument("--rows", type=Path)
    args = parser.parse_args()
    if args.phase == "configure":
        assert args.analysis and args.remote_configs and args.results_root
        configure(args)
    else:
        assert args.rows
        analyze(args)


if __name__ == "__main__":
    main()
