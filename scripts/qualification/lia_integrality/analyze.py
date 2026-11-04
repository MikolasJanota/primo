#!/usr/bin/env python3
"""Analyze the paired integrality run, archived Z3 answers, and separate model replay."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from summarize_benchmark_shards import compare, is_solved, summarize

COUNTERS = (
    ":lia.patch-sweeps",
    ":lia.patch-rounds-fully-integral",
    ":lia.patch-occurrence-entries",
    ":lia.patch-candidates-scored",
    ":lia.patch-affected-rows-inspected",
    ":lia.generalized-lemmas",
    ":lra.pivots",
    ":sat.conflicts",
)


def load(directory: Path) -> tuple[dict, dict, list]:
    rows, inputs, parents = {}, {}, []
    for path in sorted(directory.glob("shard-*/answers.csv")):
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                if row["file"] in rows:
                    raise ValueError(f"duplicate row: {row['file']}")
                rows[row["file"]] = row
        metadata = json.loads(path.with_name("provenance.json").read_text())
        if metadata["status"] != "complete" or not metadata["inputs_unchanged"]:
            raise ValueError(f"incomplete or changed parent: {path.parent}")
        if inputs.keys() & metadata["inputs"].keys():
            raise ValueError("duplicate input identity")
        inputs.update(metadata["inputs"])
        parents.append(metadata)
    if rows.keys() != inputs.keys():
        raise ValueError("answer/input identity coverage differs")
    return rows, inputs, parents


def models(directory: Path, rows: dict) -> dict:
    counts, states = collections.Counter(), collections.Counter()
    expected, completed, passes = 0, 0, 0
    for path in directory.glob("shard-*/model-validation/coverage.json"):
        data = json.loads(path.read_text())
        states[data["status"]] += 1
        counts.update(data["outcomes"])
        expected += data["expected_sat"]
        completed += data["completed"]
        passes += data["dolmen_pass"]
        identity = json.loads(path.with_name("replay-provenance.json").read_text())
        parent = path.parents[1] / "provenance.json"
        if (
            identity["parent_provenance_sha256"]
            != hashlib.sha256(parent.read_bytes()).hexdigest()
        ):
            raise ValueError("replay parent identity mismatch")
    selected = sum(is_solved(row) and row["answer"] == "sat" for row in rows.values())
    return {
        "selected_sat": selected,
        "expected_sat_in_replays": expected,
        "completed_replay_rows": completed,
        "dolmen_pass": passes,
        "shard_states": dict(states),
        "outcomes": dict(counts),
        "complete": expected == completed == passes == selected
        and states == {"complete": len(list(directory.glob("shard-*/answers.csv")))},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--expected-files", type=int, default=13306)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    if args.timeout <= 0 or args.expected_files <= 0:
        parser.error("timeout and expected file count must be positive")
    reference, reference_inputs, _ = load(args.reference_root)
    data, identities, provenance = {}, {}, {}
    result = {"timeout_seconds": args.timeout, "runs": {}, "models": {}}
    for arm in ("all", "m124"):
        raw, identities[arm], parents = load(args.root / arm)
        if len(raw) != args.expected_files:
            raise ValueError(
                f"{arm}: expected {args.expected_files} files, got {len(raw)}"
            )
        if identities[arm] != reference_inputs:
            raise ValueError(
                "current corpus differs from archived reference input hashes"
            )
        binaries = {item["solver"]["sha256"] for item in parents}
        if len(binaries) != 1:
            raise ValueError("multiple binary identities in one arm")
        provenance[arm] = binaries.pop()
        result["models"][arm] = models(args.root / arm, raw)
        data[arm] = {name: dict(row) for name, row in raw.items()}
        for row in data[arm].values():
            if row["status"] == "ok" and float(row["seconds"]) > args.timeout:
                row["status"] = "timeout"
        result["runs"][arm] = summarize(data[arm], args.timeout)
        checked = [
            name for name in raw if is_solved(raw[name]) and is_solved(reference[name])
        ]
        disagreements = [
            name for name in checked if raw[name]["answer"] != reference[name]["answer"]
        ]
        result["runs"][arm]["z3_checked"] = len(checked)
        result["runs"][arm]["z3_disagreements"] = disagreements
    if provenance["all"] != provenance["m124"]:
        raise ValueError("comparison binaries differ")
    result["binary_sha256"] = provenance["all"]
    result["comparison"] = compare(data["all"], data["m124"])
    common = [
        name
        for name in data["all"]
        if is_solved(data["all"][name]) and is_solved(data["m124"][name])
    ]
    ratios = [
        float(data["m124"][name]["seconds"]) / float(data["all"][name]["seconds"])
        for name in common
        if min(float(data[a][name]["seconds"]) for a in data) >= 0.5
    ]
    result["common_runtime"] = {
        "files_both_at_least_half_second": len(ratios),
        "median_m124_over_all": statistics.median(ratios) if ratios else None,
        "geometric_mean_m124_over_all": statistics.geometric_mean(ratios)
        if ratios
        else None,
    }
    result["counters_common_solved"] = {
        arm: {
            key: sum(int(data[arm][name].get(key) or 0) for name in common)
            for key in COUNTERS
        }
        for arm in data
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    clean = all(
        not r["wrong"] and not r["error"] and not r["z3_disagreements"]
        for r in result["runs"].values()
    )
    return (
        0
        if clean
        and not result["comparison"]["decisive_answer_disagreements"]
        and all(m["complete"] for m in result["models"].values())
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
