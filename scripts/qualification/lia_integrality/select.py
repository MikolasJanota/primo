#!/usr/bin/env python3
"""Select the fixed integrality diagnostic from the recorded paired 120 s run."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def load_rows(directory: Path) -> dict[str, dict[str, str]]:
    rows = {}
    for path in sorted(directory.glob("shard-*.csv")):
        if path.stem.endswith("-summary"):
            continue
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                name = row["file"]
                if name in rows:
                    raise ValueError(f"duplicate file: {name}")
                rows[name] = row
    if not rows:
        raise ValueError(f"no rows in {directory}")
    return rows


def solved(row: dict[str, str]) -> bool:
    return (
        row["status"] == "ok"
        and row["answer"] in {"sat", "unsat"}
        and float(row["seconds"]) <= 60
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--historical-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    args = parser.parse_args()
    off = load_rows(args.historical_root / "off")
    all_on = load_rows(args.historical_root / "all")
    if off.keys() != all_on.keys():
        raise ValueError("historical arms cover different files")
    reasons = {}
    groups = collections.defaultdict(list)
    for name, baseline in off.items():
        candidate = all_on[name]
        was_solved, now_solved = solved(baseline), solved(candidate)
        if was_solved != now_solved:
            reasons[name] = "gain" if now_solved else "loss"
            continue
        if not was_solved:
            category = "stable-unsolved"
        elif max(float(baseline["seconds"]), float(candidate["seconds"])) < 0.5:
            category = "stable-easy"
        else:
            category = "stable-medium"
        groups[baseline["family"], category].append(name)
    for (_, category), names in groups.items():
        names.sort(
            key=lambda name: hashlib.sha256(f"20261104:{name}".encode()).hexdigest()
        )
        for name in names[: 8 if category == "stable-medium" else 4]:
            reasons[name] = category
    prefix = "tmp/QF_LIA/"
    if any(not name.startswith(prefix) for name in reasons):
        raise ValueError(f"expected file paths prefixed by {prefix}")
    args.out.write_text(
        "".join(name.removeprefix(prefix) + "\n" for name in sorted(reasons))
    )
    metadata = {
        "files": len(reasons),
        "manifest_sha256": hashlib.sha256(args.out.read_bytes()).hexdigest(),
        "categories": dict(collections.Counter(reasons.values())),
        "families": dict(collections.Counter(off[name]["family"] for name in reasons)),
        "reason_by_file": reasons,
    }
    args.metadata.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
