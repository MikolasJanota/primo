#!/usr/bin/env python3
"""Require concrete agreement/model coverage; retain only classified resource incompleteness."""
from __future__ import annotations

import csv
import json
import sys
from collections import Counter
from pathlib import Path


def main() -> None:
    root = Path(sys.argv[1])
    policy = sys.argv[2]
    with (root / f"answers-{policy}.csv").open(newline="") as stream:
        answers = list(csv.DictReader(stream))
    with (root / f"models-{policy}/summary.csv").open(newline="") as stream:
        models = list(csv.DictReader(stream))
    assert len(answers) == len(models) >= 100
    for row in answers:
        assert row["outcome"] == "PASS" or (row["outcome"] == "INCOMPLETE" and row["ours"] in ("timeout", "unknown") and row["reference"] in ("sat", "unsat")), row
    for row in models:
        assert row["outcome"] in ("pass", "not-sat", "solver-timeout", "solver-unknown"), row
    by_file = {Path(row["file"]).name: row for row in models}
    for row in answers:
        if row["ours"] == "sat":
            model = by_file[Path(row["file"]).name]
            assert model["outcome"] == "pass" and model["validator"] == "dolmen", model
    (root / f"coverage-{policy}.json").write_text(json.dumps({"inputs": len(answers), "answers": dict(Counter(row["outcome"] for row in answers)), "models": dict(Counter(row["outcome"] for row in models)), "selected_sat": sum(row["ours"] == "sat" for row in answers), "selected_sat_direct_dolmen": sum(row["ours"] == "sat" for row in answers)}, indent=2) + "\n")


if __name__ == "__main__":
    main()
