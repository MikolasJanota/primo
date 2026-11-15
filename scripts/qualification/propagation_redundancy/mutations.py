#!/usr/bin/env python3
"""Audit fresh real-seed mutation receipts; keep resource outcomes incomplete."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--native-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    profiles = []
    for logic in ("QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA"):
        for mode in ("rows", "both"):
            directory = args.root / (logic + "-" + mode)
            assert f"git: {args.source}\n" in (directory / "version.txt").read_text()
            hashes = (directory / "executables.sha256").read_text().splitlines()
            assert hashes[0].split()[0] == args.native_sha256
            assert not any(p.is_file() for p in (directory / "bugs").rglob("*"))
            summary = directory / "mutant-answers.csv"
            rows = list(csv.DictReader(summary.open()))
            assert len(rows) >= 100
            seed_rows = list(csv.DictReader((directory / "seed-answers.csv").open()))
            assert len(seed_rows) == 12
            for row in rows + seed_rows:
                assert row["outcome"] in ("PASS", "INCOMPLETE"), row
                if row["outcome"] == "INCOMPLETE":
                    assert row["ours"] in ("timeout", "unknown") or row[
                        "reference"
                    ] in ("timeout", "unknown"), row
            selected = {
                r["file"] for r in rows if r["ours"] == "sat" and r["ours_exit"] == "0"
            }
            models_path = directory / "models/summary.csv"
            models = (
                list(csv.DictReader(models_path.open())) if models_path.exists() else []
            )
            assert len(models) == len(selected)
            assert {r["file"] for r in models} == selected
            assert all(
                r["outcome"] == "pass"
                and r["validator"] == "dolmen"
                and r["solver_exit"] == r["dolmen_exit"] == "0"
                for r in models
            )
            profiles.append(
                {
                    "logic_profile": logic + "-" + mode,
                    "cases": len(rows),
                    "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
                    "seed_outcomes": dict(
                        collections.Counter(r["outcome"] for r in seed_rows)
                    ),
                    "SAT_Dolmen_pass": len(models),
                    "summary_sha256": sha(summary),
                    "seed_summary_sha256": sha(directory / "seed-answers.csv"),
                    "model_summary_sha256": sha(models_path) if models else None,
                    "seeds_sha256": sha(directory / "seeds.json"),
                    "executables_sha256": sha(directory / "executables.sha256"),
                    "resources": [
                        r for r in rows + seed_rows if r["outcome"] != "PASS"
                    ],
                }
            )
    total = collections.Counter()
    for profile in profiles:
        total.update(profile["outcomes"])
    report = {
        "source": args.source,
        "native_sha256": args.native_sha256,
        "status": "No decisive mismatch or invalid selected SAT model; resource outcomes incomplete.",
        "cases": sum(p["cases"] for p in profiles),
        "outcomes": dict(total),
        "SAT_Dolmen_pass": sum(p["SAT_Dolmen_pass"] for p in profiles),
        "profiles": profiles,
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "profiles"}, indent=2))


if __name__ == "__main__":
    main()
