#!/usr/bin/env python3
"""Select all recorded losses and deterministic family controls, without timing new solvers."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path


def load(root: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for path in sorted(root.rglob("answers.csv")):
        for row in csv.DictReader(path.open()):
            name = row["file"]
            if name in rows:
                raise ValueError(f"duplicate input {name}")
            rows[name] = row
    if not rows:
        raise ValueError(f"empty source {root}")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--historical", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    baseline, candidate = load(args.historical / "off"), load(args.historical / "m124")
    losses = json.loads(args.metadata.read_text())["summary"]["comparison"]["losses"]
    assert len(losses) == len(set(losses)) == 30
    groups: dict[str, list[str]] = defaultdict(list)
    for name, old in baseline.items():
        new = candidate[name]
        if name not in losses and all(row["status"] == "ok" and row["answer"] in {"sat", "unsat"}
                                      and float(row["seconds"]) <= 5 for row in (old, new)):
            groups[old["family"]].append(name)
    controls = []
    for names in groups.values():
        names.sort(key=lambda name: hashlib.sha256(f"20261105:phase-control:{name}".encode()).hexdigest())
        controls.extend(names[:4])
    assert len(groups) == 4 and len(controls) == 14
    args.out.mkdir(parents=True, exist_ok=True)
    for label, names in (("losses", losses), ("controls", sorted(controls)),
                         ("evaluation", losses + sorted(controls))):
        (args.out / f"{label}.txt").write_text("".join(name + "\n" for name in names))
    record = {"source": "benchmarks/qf-uflia-m124-review-20261104.json",
              "source_solver_commit": json.loads(args.metadata.read_text())["solver_commit"],
              "losses": losses, "controls": sorted(controls),
              "control_selection": "up to four per family (two Certora); both historical arms solved within5s; SHA256(20261105:phase-control:path)",
              "development_losses": ["wisas/xs_24_44.smt2", "wisas/xs_28_48.smt2",
                                     "wisas/xs_33_53.smt2", "wisas/xs_40_60.smt2"],
              "evaluation_manifest_sha256": hashlib.sha256((args.out / "evaluation.txt").read_bytes()).hexdigest()}
    (args.out / "selection.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
