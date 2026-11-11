#!/usr/bin/env python3
"""Summarize the complete rotated diagnostic comparison and separate SAT-model coverage."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
from collections import Counter
from pathlib import Path

from profiles import TIMING_PROFILES


def solved(row: dict) -> bool:
    return row["status"] == "ok" and row["answer"] in ("sat", "unsat") and str(row["exit_code"]) == "0"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Collected results/uflia-model-unit")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--followup", action="store_true", help="Require the additional three regression pairs")
    args = parser.parse_args()
    cases = json.loads(args.manifest.read_text())["cases"]
    all_rows = []
    coverage = []
    identities = {}
    complete_slots = 0
    coverage_slots = 0
    for slot in range(36):
        directory = args.root / "timings" / f"slot-{slot}"
        path = directory / "rows.jsonl"
        if not path.exists():
            continue
        rows = []
        for line in path.read_text().splitlines():
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                break
        if len(rows) == 6:
            complete_slots += 1
        all_rows.extend(rows)
        identities[str(path.relative_to(args.root))] = hashlib.sha256(path.read_bytes()).hexdigest()
        model_path = directory / "coverage.json"
        if model_path.exists():
            coverage_slots += 1
            coverage.extend(json.loads(model_path.read_text())["rows"])
            identities[str(model_path.relative_to(args.root))] = hashlib.sha256(model_path.read_bytes()).hexdigest()
    assert not any(row["wrong"] for row in all_rows)
    assert not any(row["status"] not in ("ok", "timeout") for row in all_rows)
    by_key = {(r["file"], r["repetition"], r["profile"]): r for r in all_rows}
    assert len(by_key) == len(all_rows)
    selected = {(r["file"], r["repetition"], r["profile"]) for r in all_rows if solved(r) and r["answer"] == "sat"}
    validated = {(r["file"], r["repetition"], r["profile"]) for r in coverage if r["validator"] == "dolmen"}
    missing = selected - validated
    assert not validated - selected
    summary = {}
    for profile in TIMING_PROFILES:
        rs = [r for r in all_rows if r["profile"] == profile]
        summary[profile] = {"timing_rows": len(rs), "solved": sum(solved(r) for r in rs),
                            "answers": dict(Counter(r["answer"] for r in rs if solved(r))),
                            "statuses": dict(Counter(r["status"] for r in rs)),
                            "par2_sum": sum(float(r["seconds"]) if solved(r) else 240 for r in rs),
                            "per_case_median": {c["file"]: statistics.median([float(r["seconds"]) for r in rs if r["file"] == c["file"] and solved(r)]) if any(r["file"] == c["file"] and solved(r) for r in rs) else None for c in cases}}
    comparisons = {}
    for before, after in (("baseline-off", "off"), ("legacy-all", "model-all"), ("legacy-fixed", "model-fixed")):
        pairs = []
        gains = []
        losses = []
        for case in cases:
            for rep in range(3):
                a = by_key.get((case["file"], rep, before))
                b = by_key.get((case["file"], rep, after))
                if not a or not b:
                    continue
                key = {"file": case["file"], "repetition": rep}
                if solved(b) and not solved(a):
                    gains.append(key)
                if solved(a) and not solved(b):
                    losses.append(key)
                if solved(a) and solved(b):
                    pairs.append(dict(key, before_seconds=float(a["seconds"]), after_seconds=float(b["seconds"]), ratio=float(b["seconds"]) / float(a["seconds"])))
        substantial = [p["ratio"] for p in pairs if max(p["before_seconds"], p["after_seconds"]) >= 0.05]
        comparisons[f"{after}/{before}"] = {"gains": gains, "losses": losses, "paired_successes": pairs,
                                            "common_at_least_50ms": len(substantial),
                                            "median_ratio": statistics.median(substantial) if substantial else None,
                                            "geometric_ratio": math.exp(statistics.mean(math.log(r) for r in substantial)) if substantial else None}
    counters = [":lia.equality.scans", ":lia.equality.candidates", ":lia.equality.model-unit.selected",
                ":lia.equality.legacy-scans-skipped", ":lia.equality.model-unit.rows-inspected",
                ":lia.equality.model-unit.nonzeros-inspected", ":lia.equality.model-unit.feasibility-checks",
                ":lia.equality.model-unit.feasibility-pivots", ":time.lia.equality-discovery-us",
                ":time.lia.equality-feasibility-us", ":combination.equality.fresh-pairs", ":lra.pivots",
                ":combination.model.complete-fallbacks"]
    mechanism = [{"file": r["file"], "profile": r["profile"], "repetition": r["repetition"],
                  "status": r["status"], "counters": {k: r["stats"].get(k) for k in counters}} for r in all_rows]
    default_counter_differences = []
    for case in cases:
        for rep in range(3):
            a = by_key.get((case["file"], rep, "baseline-off"))
            b = by_key.get((case["file"], rep, "off"))
            if a and b and solved(a) and solved(b):
                differences = {k: [v, b["stats"].get(k)] for k, v in a["stats"].items() if not k.startswith(":time.") and b["stats"].get(k) != v}
                if differences:
                    default_counter_differences.append({"file": case["file"], "repetition": rep, "differences": differences})
    followup = None
    followup_complete = True
    if args.followup:
        repeat_rows = []
        repeat_coverage = []
        for slot in range(9):
            directory = args.root / "followup" / f"slot-{slot}"
            path = directory / "rows.jsonl"
            model_path = directory / "coverage.json"
            assert path.is_file() and model_path.is_file(), directory
            rs = [json.loads(line) for line in path.read_text().splitlines()]
            assert len(rs) == 2 and not any(r["wrong"] or r["status"] not in ("ok", "timeout") for r in rs)
            repeat_rows.extend(rs)
            repeat_coverage.extend(json.loads(model_path.read_text())["rows"])
            for item in (path, model_path):
                identities[str(item.relative_to(args.root))] = hashlib.sha256(item.read_bytes()).hexdigest()
        repeat_selected = {(r["file"], r["repetition"], r["profile"]) for r in repeat_rows if solved(r) and r["answer"] == "sat"}
        repeat_validated = {(r["file"], r["repetition"], r["profile"]) for r in repeat_coverage if r["validator"] == "dolmen"}
        assert repeat_selected == repeat_validated
        followup = {"timing_rows": len(repeat_rows), "selected_sat_direct_dolmen": len(repeat_selected),
                    "rows": repeat_rows, "medians": {}}
        for filename in dict.fromkeys(r["file"] for r in repeat_rows):
            followup["medians"][filename] = {}
            for profile in dict.fromkeys(r["profile"] for r in repeat_rows if r["file"] == filename):
                rs = [r for r in repeat_rows if r["file"] == filename and r["profile"] == profile]
                successes = [float(r["seconds"]) for r in rs if solved(r)]
                followup["medians"][filename][profile] = {"solved": len(successes), "attempts": len(rs),
                    "median_success_seconds": statistics.median(successes) if successes else None}
        followup_complete = len(repeat_rows) == 18
    result = {"status": "complete" if complete_slots == 36 and coverage_slots == 36 and not missing and followup_complete else "incomplete",
              "scope": "12 selected diagnostics,3 rotated repetitions; not a corpus estimate or pure discovery-cost isolation",
              "complete_timing_slots": complete_slots, "complete_coverage_slots": coverage_slots,
              "timing_rows": len(all_rows), "selected_sat": len(selected), "selected_sat_direct_dolmen": len(selected & validated),
              "missing_sat": [list(k) for k in sorted(missing)], "profiles": summary, "comparisons": comparisons,
              "followup": followup, "default_existing_counter_differences": default_counter_differences, "mechanism": mechanism, "artifact_sha256": identities}
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "timings": len(all_rows), "selected_sat": len(selected), "validated": len(selected & validated), "profiles": {p: {k: d[k] for k in ("solved", "par2_sum")} for p, d in summary.items()}}, indent=2))
    return result["status"] != "complete"


if __name__ == "__main__":
    raise SystemExit(main())
