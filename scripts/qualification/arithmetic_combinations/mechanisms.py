#!/usr/bin/env python3
"""Compare selected search counters separately from runtime variation."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path

KEYS = (
    ":sat.decisions",
    ":sat.conflicts",
    ":lra.pivots",
    ":lia.branching.private-nodes",
    ":lia.branching.split-requests",
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


def vector(row):
    if row["solved"] != "True" or any(row.get(k, "") == "" for k in KEYS):
        return None
    return tuple(int(row[k]) for k in KEYS)


def summarize(rows):
    groups = collections.defaultdict(dict)
    for row in rows:
        key = (row["logic"], int(row["slot"]), row["profile"])
        rep = int(row["repetition"])
        assert rep not in groups[key]
        groups[key][rep] = row
    assert all(set(value) == {0, 1, 2} for value in groups.values())
    result = {}
    for logic in sorted({k[0] for k in groups}):
        slots = sorted({k[1] for k in groups if k[0] == logic})
        profiles = sorted({k[2] for k in groups if k[0] == logic})
        stable = {}
        for p in profiles:
            eligible, varying = [], []
            for s in slots:
                values = [vector(groups[logic, s, p][r]) for r in range(3)]
                if None in values:
                    continue
                eligible.append(s)
                if len(set(values)) > 1:
                    varying.append(s)
            stable[p] = {
                "eligible_cases": len(eligible),
                "cases_varying_selected_counters_across_repetitions": varying,
            }
        edges = {}
        for before, after in EDGES:
            if before not in profiles or after not in profiles:
                continue
            eligible, changed, per_key = [], [], collections.Counter()
            for s in slots:
                old = [vector(groups[logic, s, before][r]) for r in range(3)]
                new = [vector(groups[logic, s, after][r]) for r in range(3)]
                if None in old or None in new:
                    continue
                eligible.append(s)
                if old != new:
                    changed.append(s)
                for i, key in enumerate(KEYS):
                    per_key[key] += any(
                        a[i] != b[i] for a, b in zip(old, new, strict=True)
                    )
            edges[before + "->" + after] = {
                "eligible_cases": len(eligible),
                "cases_with_changed_selected_counters": changed,
                "cases_with_equal_selected_counters": len(eligible) - len(changed),
                "cases_changed_by_counter": dict(per_key),
            }
        result[logic] = {"within_profile": stable, "one_switch_edges": edges}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads((args.evidence / "analysis.json").read_text())
    assert report["status"] == "qualified"
    path = args.evidence / "measurements.csv"
    rows = list(csv.DictReader(path.open()))
    assert len(rows) == report["native_rows"] == 14364
    result = {
        "source": report["source"],
        "native_sha256": report["native_sha256"],
        "measurements_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "counter_keys": KEYS,
        "scope": "Cases solved in all3repetitions with all selected counters available. Counts compare selected work counters, not complete execution traces or CPU attribution; equal counters do not prove identical search. Excludes gained/lost/cutoff/resource cases. No timing scores changed.",
        "by_logic": summarize(rows),
    }
    (args.evidence / "mechanisms.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Wrote selected-counter repeatability and one-switch change diagnostics")


if __name__ == "__main__":
    main()
