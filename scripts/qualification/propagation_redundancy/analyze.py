#!/usr/bin/env python3
"""Audit paired measurements and report arithmetic redundancy without filtering."""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.bound_repairs.analyze_followups import audit  # noqa: E402


def counters(row: dict) -> dict | None:
    stats = row["stats"]
    if ":lra.bound-notifications" not in stats:
        return None

    def get(key: str) -> int:
        return int(stats.get(":lra." + key, 0))

    notifications = get("bound-notifications")
    noops = get("bound-noop-notifications")
    assert notifications == sum(
        get("bound-" + category + "-notifications")
        for category in ("noop", "tightening", "conflicting")
    )
    candidates = get("propagation-candidates")
    assert candidates == get("propagation-unique-candidates") + get(
        "propagation-duplicate-candidates"
    )
    attempts = get("propagation-candidate-attempts")
    suppressed = get("propagation-suppressed-duplicates")
    if ":lra.propagation-candidate-attempts" in stats:
        assert attempts == candidates + suppressed
    else:
        attempts = candidates  # The original audit inserts every attempt.
    events = get("row-scan-events")
    productive = get("row-scan-events-with-new-candidates")
    skipped = get("row-scan-repeated-state-skips")
    assert skipped <= get("row-scan-noop-events")
    assert productive <= events and get("row-scan-noop-events") <= events
    return {
        "bound_notifications": notifications,
        "bound_noops": noops,
        "noop_fraction": noops / notifications if notifications else None,
        "candidate_attempts": attempts,
        "suppressed_duplicates": suppressed,
        "suppressed_fraction": suppressed / attempts if attempts else None,
        "owned_reason_literals": get("propagation-reason-literals")
        if ":lra.propagation-reason-literals" in stats
        else None,
        "candidates": candidates,
        "duplicates": get("propagation-duplicate-candidates"),
        "duplicate_fraction": get("propagation-duplicate-candidates") / candidates
        if candidates
        else None,
        "assigned_discards": get("propagation-discarded-assigned"),
        "backtrack_discards": get("propagation-backtrack-discarded"),
        "row_events": events,
        "row_events_with_new_pending_literals": productive,
        "row_events_without_new_pending_literals_fraction": (events - productive)
        / events
        if events
        else None,
        "noop_row_events": get("row-scan-noop-events"),
        "repeated_state_skips": skipped,
        "repeated_state_skip_fraction": skipped / events if events else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.configs / "manifest.json").read_text())
    results = []
    for phase in manifest:
        for item in phase["cases"]:
            for repetition in range(3 if phase["phase"] == "complete" else 1):
                suffix = f"{phase['phase']}/case-{item['index']}"
                result = audit(
                    args.configs / suffix / f"config-{repetition}.json",
                    args.rows / suffix / f"rep-{repetition}/slot-0",
                )
                result |= {"phase": phase["phase"], "repetition": repetition, **item}
                result["redundancy"] = {
                    r["profile"]: counters(r) for r in result["rows"]
                }
                if item["conflict_limit"] is not None:
                    for row in result["rows"]:
                        if row["status"] == "ok" and row["answer"] == "unknown":
                            assert (
                                int(row["stats"].get(":theory.conflicts", 0))
                                >= item["conflict_limit"]
                                or int(
                                    row["stats"].get(":lia.branching.budget-exits", 0)
                                )
                                > 0
                            )
                results.append(result)
    report = {
        "status": "qualified",
        "measurements": results,
        "timing_rows": sum(len(r["rows"]) for r in results),
        "SAT_Dolmen_pass": sum(r["selected_SAT"] for r in results),
        "outcomes": {
            phase: dict(
                collections.Counter(
                    r["status"] + "/" + r["answer"]
                    for item in results
                    if item["phase"] == phase
                    for r in item["rows"]
                )
            )
            for phase in ("complete", "prefix")
        },
        "limits": "Six selected problems, rows/both profiles, control/audit adjacent pairs, three complete repetitions at120s;100/1000-conflict diagnostic prefixes at30s.8GiB address space. Not a full-suite performance qualification. Counters describe queue events, not CPU attribution or search benefit.",
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps({k: v for k, v in report.items() if k != "measurements"}, indent=2)
    )


if __name__ == "__main__":
    main()
