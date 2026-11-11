#!/usr/bin/env python3
"""Audit all generated-case shards and exact successful-SAT Dolmen coverage."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


def read(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--slots", type=int, nargs="+", default=list(range(16)))
    args = parser.parse_args()
    profiles = {}
    artifacts = {}
    for slot in args.slots:
        base = args.root / "fuzz" / f"slot-{slot}"
        summary = base / "answers/summary.csv"
        rows = read(summary)
        assert len(rows) == 250, summary
        outcomes = Counter(row["outcome"] for row in rows)
        assert not set(outcomes) - {"PASS", "TIMEOUT", "UNKNOWN_INVOLVED"}, (
            slot,
            outcomes,
        )
        selected = {
            row.get("case") or row["file"]
            for row in rows
            if row["outcome"] == "PASS"
            and row["ours"] == "sat"
            and row.get("ours_code", "0") == "0"
        }
        models = read(base / "models/summary.csv") if selected else []
        passed = {
            row["file"]
            for row in models
            if row["outcome"] == "pass"
            and row["validator"] == "dolmen"
            and row["solver_exit"] == row["dolmen_exit"] == "0"
        }
        assert selected == passed and len(models) == len(passed), (
            slot,
            selected - passed,
        )
        key = f"{'QF_LIA' if (slot // 4) % 2 == 0 else 'QF_UFLIA'}/factor-{slot // 8}"
        profile = profiles.setdefault(
            key, {"cases": 0, "outcomes": Counter(), "sat_models": 0}
        )
        profile["cases"] += len(rows)
        profile["outcomes"].update(outcomes)
        profile["sat_models"] += len(passed)
        for path in (summary, base / "models/summary.csv"):
            if path.is_file():
                artifacts[str(path.relative_to(args.root))] = hashlib.sha256(
                    path.read_bytes()
                ).hexdigest()
    assert len(profiles) == len(args.slots) // 4 and all(
        row["cases"] == 1000 for row in profiles.values()
    )
    args.out.write_text(
        json.dumps(
            {"status": "complete", "profiles": profiles, "artifacts": artifacts},
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
