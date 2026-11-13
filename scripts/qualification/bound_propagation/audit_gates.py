#!/usr/bin/env python3
"""Audit collected propagation gates and their direct model-validation receipts."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path


def read_csv(path: Path) -> list[dict]:
    with path.open() as stream:
        return list(csv.DictReader(stream))


def models(path: Path, expected: int | None = None) -> int:
    rows = read_csv(path)
    if expected is not None:
        assert len(rows) == expected
    assert all(
        r["outcome"] == "pass"
        and r["validator"] == "dolmen"
        and r["solver_exit"] == r["dolmen_exit"] == "0"
        for r in rows
    ), path
    return len(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    gates = []
    for slot in range(8):
        directory = args.root / f"gate-{slot}"
        receipt = json.loads((directory / "coverage.json").read_text())
        summary = directory / "summary.csv"
        rows = read_csv(summary)
        assert len(rows) == receipt["count"] == 1000
        assert all(r["outcome"] in ("PASS", "TIMEOUT") for r in rows)
        assert (
            hashlib.sha256(summary.read_bytes()).hexdigest()
            == receipt["summary_sha256"]
        )
        assert (
            models(directory / "models/summary.csv", receipt["selected_sat"])
            == receipt["direct_dolmen_pass"]
        )
        gates.append(receipt)
    seed_manifest = json.loads(
        Path("benchmarks/bound-propagation-20261113/regression-seeds.json").read_text()
    )
    seed_cases = {c["file"]: c for c in seed_manifest["cases"]}
    seeds = []
    for profile in ["rows", "both"]:
        directory = args.root / f"seeds-v2-{profile}"
        receipt = json.loads((directory / "complete.json").read_text())
        rows = [
            json.loads(s) for s in (directory / "rows.jsonl").read_text().splitlines()
        ]
        assert len(rows) == receipt["cases"] == len(seed_cases)
        assert {r["file"] for r in rows} == set(seed_cases)
        for row in rows:
            assert row["sha256"] == seed_cases[row["file"]]["sha256"]
            assert all(r["exit_code"] == 0 for r in row["results"].values())
            answers = row["results"]["native"]["answers"]
            assert answers == row["results"]["reference"]["answers"]
            assert answers and all(a in ("sat", "unsat") for a in answers)
            source = Path(row["file"])
            assert hashlib.sha256(source.read_bytes()).hexdigest() == row["sha256"]
            match = re.search(
                r"\(set-info\s+:status\s+(sat|unsat)\s*\)", source.read_text()
            )
            if match and len(answers) == 1:
                assert answers == [match[1]], row
        models(directory / "models/summary.csv", receipt["single_query_sat_models"])
        scoped = json.loads((directory / "scoped-models/summary.json").read_text())
        assert len(scoped) == receipt["scoped_sessions"]
        assert all(r["outcome"] == "pass" for r in scoped)
        seeds.append(receipt)
    canonical = []
    for profile, slots in [("rows", range(4)), ("both", range(4, 8))]:
        for slot in slots:
            directory = args.root / f"canonical-{profile}/task-{slot}"
            assert (directory / "exit-code").read_text().strip() == "0"
            counts = {}
            model_count = 0
            for corpus, count in [
                ("general", 250),
                ("finite-qf-uf", 25),
                ("finite-qf-uflra", 25),
            ]:
                rows = read_csv(directory / corpus / "summary.csv")
                assert len(rows) == count and all(r["outcome"] == "PASS" for r in rows)
                sat = sum((r.get("ours") or r.get("answer")) == "sat" for r in rows)
                model_count += (
                    models(directory / "model-validation" / corpus / "summary.csv", sat)
                    if sat
                    else 0
                )
                counts[corpus] = count
            canonical.append(
                {
                    "profile": profile,
                    "slot": slot,
                    "counts": counts,
                    "direct_dolmen_pass": model_count,
                }
            )
    unit = (args.root / "unit.log").read_text()
    assert "100% tests passed" in unit
    assert len(re.findall(r"^\d+/3 Test #\d+: .*?Passed\s", unit, re.MULTILINE)) == 3
    files = [p for p in args.root.rglob("*") if p.is_file()]
    result = {
        "generated": gates,
        "seeds": seeds,
        "canonical": canonical,
        "units": "3/3arithmetic groups including existing forward/reverse propagation tests",
        "outcomes": dict(
            sum(
                (collections.Counter(g["outcomes"]) for g in gates),
                collections.Counter(),
            )
        ),
        "generated_direct_dolmen_pass": sum(g["direct_dolmen_pass"] for g in gates),
        "canonical_direct_dolmen_pass": sum(c["direct_dolmen_pass"] for c in canonical),
        "receipt_sha256": {
            str(p.relative_to(args.root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files)
        },
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: v
                for k, v in result.items()
                if k not in ("receipt_sha256", "generated", "canonical", "seeds")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
