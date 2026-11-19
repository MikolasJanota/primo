#!/usr/bin/env python3
"""Audit the QF_LIA default-promotion comparison and every selected SAT replay."""

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
from scripts.bench_qf_uf_full import parse_smtlib_status  # noqa: E402
from scripts.qualification.arithmetic_combinations.analyze import (  # noqa: E402
    describe,
    expected_resource_diagnostic,
)
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def expected_answer(case, status_inputs):
    """Use the runner's declared-status fallback when the frozen oracle is unknown."""
    expected = case.get("expected", "unknown")
    if expected in ("sat", "unsat") or status_inputs is None:
        return expected
    source = status_inputs / case["logic"] / case["file"]
    assert sha(source) == case["sha256"]
    return parse_smtlib_status(source)


def assert_single_core(affinity, host, topology):
    """SLURM core binding may expose both SMT siblings to a single task."""
    assert affinity and len(set(affinity)) == len(affinity)
    if topology is None:
        assert len(affinity) == 1
        return
    cpus = topology[host]["cpus"]
    cores = {
        (cpus[str(cpu)]["physical_package_id"], cpus[str(cpu)]["core_id"])
        for cpu in affinity
    }
    assert len(cores) == 1


def read_rows(path):
    """Normalize the timing harness's decimal-string wire format at input."""
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    for row in rows:
        row["seconds"] = float(row["seconds"])
        assert math.isfinite(row["seconds"]) and row["seconds"] > 0
    return rows


def comparison(rows, before, after, limit):
    common = [r for r in rows if solved(r[before]) and solved(r[after])]
    ratios = [
        r[after]["seconds"] / r[before]["seconds"]
        for r in common
        if r[before]["seconds"] >= 1
    ]
    return {
        "gains": [
            r[after]["file"] for r in rows if not solved(r[before]) and solved(r[after])
        ],
        "losses": [
            r[before]["file"]
            for r in rows
            if solved(r[before]) and not solved(r[after])
        ],
        "mean_PAR2_delta_seconds": statistics.mean(
            penalty(r[after], limit) - penalty(r[before], limit) for r in rows
        ),
        "common_solved": len(common),
        "common_before_at_least_1s": len(ratios),
        "conditional_geomean_ratio": (
            math.exp(statistics.mean(map(math.log, ratios))) if ratios else None
        ),
        "runtime_regressions_to_repeat": [
            {
                "file": r[before]["file"],
                "before": r[before]["seconds"],
                "after": r[after]["seconds"],
            }
            for r in common
            if r[after]["seconds"] >= 1.1 * r[before]["seconds"]
            and r[after]["seconds"] - r[before]["seconds"] >= 0.5
        ],
        "limitation": "One rotated pass; runtime differences and losses are not repeat-qualified.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--followup-of", type=Path, help="Pinned parent card for a scoped repeat"
    )
    parser.add_argument(
        "--topology", type=Path, help="Recorded per-host CPU sibling identities"
    )
    parser.add_argument(
        "--status-inputs",
        type=Path,
        help="Content-pinned inputs for declared-status fallback",
    )
    args = parser.parse_args()
    topology = json.loads(args.topology.read_text()) if args.topology else None
    config = json.loads(args.config.read_text())
    if args.followup_of:
        parent = json.loads(args.followup_of.read_text())
        assert len(parent["cases"]) == 200
        assert list(parent["profiles"]) == ["OLD", "NEW", "HYBRID", "CUBE"]
        assert config["parent_sha256"] == sha(args.followup_of)
        assert config["profiles"] == parent["profiles"]
        for field in ("roots", "timeout", "address_space_bytes"):
            assert config[field] == parent[field]
        assert config["cases"] and len(
            {c["original_slot"] for c in config["cases"]}
        ) == len(config["cases"])
        for case in config["cases"]:
            assert 0 <= case["original_slot"] < len(parent["cases"])
            assert case["repetition"] in (1, 2)
            assert case == dict(
                parent["cases"][case["original_slot"]],
                original_slot=case["original_slot"],
                repetition=case["repetition"],
            )
    else:
        assert len(config["cases"]) == 200
        assert list(config["profiles"]) == ["OLD", "NEW", "HYBRID", "CUBE"]
    records, receipts, coverage = [], [], []
    status_fallbacks = {}
    for slot, case in enumerate(config["cases"]):
        expected = expected_answer(case, args.status_inputs)
        assert expected in ("sat", "unsat")
        if case.get("expected") not in ("sat", "unsat"):
            status_fallbacks[case["file"]] = {
                "input_sha256": case["sha256"],
                "declared_status": expected,
            }
        directory = args.rows / f"slot-{slot}"
        assert (directory / "timing-complete").is_file()
        provenance_path = directory / "provenance.json"
        provenance = json.loads(provenance_path.read_text())
        assert provenance["config_sha256"] == sha(args.config)
        assert provenance["runner_sha256"] == sha(
            ROOT / "scripts/qualification/lia_hybrid/run.py"
        )
        assert (
            provenance["case"] == case and provenance["profiles"] == config["profiles"]
        )
        assert_single_core(provenance["affinity"], provenance["host"], topology)
        order = measurement_order(config, slot)
        assert provenance["order"] == order
        path = directory / "rows.jsonl"
        rows = read_rows(path)
        assert [row["profile"] for row in rows] == order
        by_profile = {}
        for row in rows:
            label = row["profile"]
            assert row["args"] == config["profiles"][label]["args"] + case.get(
                "extra_args", []
            )
            assert (
                row["logic"] == case["logic"] == "QF_LIA"
                and row["file"] == case["file"]
            )
            assert math.isfinite(row["seconds"]) and row["seconds"] > 0
            audit_row(row)
            if solved(row):
                assert row["answer"] == expected
            reason = expected_resource_diagnostic(row)
            if reason:
                text = (directory / f"{label}.stderr").read_text()
                assert reason in text.splitlines(), (slot, label, reason)
            by_profile[label] = row
        assert len({r["answer"] for r in rows if solved(r)}) <= 1
        selected = {
            k: r for k, r in by_profile.items() if solved(r) and r["answer"] == "sat"
        }
        models = audit_models(directory, selected, config, case)
        assert models is not None
        coverage.append({"slot": slot, "metadata_sha256": models})
        receipts.append(
            {
                "slot": slot,
                "job": provenance["job"],
                "host": provenance["host"],
                "rows_sha256": sha(path),
                "provenance_sha256": sha(provenance_path),
            }
        )
        records.append(by_profile)
    limit = config["timeout"]

    def summarize(rows):
        profiles = {}
        for label in config["profiles"]:
            selected = [r[label] for r in rows]
            profiles[label] = describe(selected, limit)
            # Missing statistics on interrupted rows remain unavailable. These
            # totals describe only completed successful measurements.
            complete = [r for r in selected if solved(r) and r["stats"]]
            profiles[label]["successful_cube_counter_totals"] = {
                key: (
                    sum(int(r["stats"].get(key, 0)) for r in complete)
                    if complete
                    else None
                )
                for key in (":lia.cube-calls", ":lia.cube-models", ":lia.cube-pivots")
            }
        return profiles

    families = collections.defaultdict(list)
    for row in records:
        families[row["NEW"]["file"].split("/")[0]].append(row)
    profile_summary = summarize(records)
    mechanism_gate = {}
    for label, summary in profile_summary.items():
        cubes = summary["successful_cube_counter_totals"]
        splits = summary["cases_with_activity"][":lia.branching.split-requests"]
        if not args.followup_of:
            assert (cubes[":lia.cube-calls"] > 0) == (label in ("NEW", "CUBE"))
            assert (cubes[":lia.cube-models"] > 0) == (label in ("NEW", "CUBE"))
            assert (splits > 0) == (label in ("NEW", "HYBRID"))
        mechanism_gate[label] = {
            "cube_models": cubes[":lia.cube-models"],
            "successful_cases_requesting_splits": splits,
        }
    report = {
        "status": "complete, including every selected native SAT model",
        "config_sha256": sha(args.config),
        "case_measurements": len(config["cases"]),
        "declared_status_fallbacks": status_fallbacks,
        "topology_sha256": sha(args.topology) if args.topology else None,
        "followup_of_sha256": sha(args.followup_of) if args.followup_of else None,
        "profiles": profile_summary,
        "mechanism_gate": mechanism_gate,
        "comparisons": {
            f"{a}->NEW": comparison(records, a, "NEW", limit)
            for a in ("OLD", "HYBRID", "CUBE")
        },
        "families": {
            family: summarize(rows) for family, rows in sorted(families.items())
        },
        "SAT_Dolmen_pass": sum(len(x["metadata_sha256"]) for x in coverage),
        "receipt_inventory": receipts,
        "coverage": coverage,
    }
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    flat = [
        {"slot": slot, **row}
        for slot, case_rows in enumerate(records)
        for row in case_rows.values()
    ]
    with (args.out / "measurements.csv").open("w") as stream:
        fields = (
            "slot",
            "file",
            "logic",
            "profile",
            "status",
            "answer",
            "exit_code",
            "seconds",
            "peak_rss_kib",
        )
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(flat)
    print(json.dumps({k: report[k] for k in ("status", "SAT_Dolmen_pass")}))


if __name__ == "__main__":
    main()
