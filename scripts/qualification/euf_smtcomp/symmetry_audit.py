#!/usr/bin/env python3
"""Reject incomplete or misbound focused symmetry-repair correctness evidence."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.bound_propagation.analyze import sha  # noqa: E402


def sat_models(directory, expected, solver, flags):
    rows = list(csv.DictReader((directory / "summary.csv").open()))
    assert len(rows) == len(expected) and {r["file"] for r in rows} == expected
    assert all(
        r["answer"] == "sat"
        and r["outcome"] == "pass"
        and r["validator"] == "dolmen"
        and r["solver_exit"] == r["dolmen_exit"] == "0"
        for r in rows
    )
    meta = json.loads((directory / "metadata.json").read_text())
    assert meta["status"] == "complete" and meta["completed"] == len(expected)
    assert meta["solver"] == solver and meta["solver_args"] == flags
    assert meta["require_dolmen_pass"] and meta["expect_sat"]
    return sha(directory / "metadata.json")


def decisive(row, expected):
    assert row["status"] == "ok" and row["exit_code"] == 0
    assert row["answer"] == expected


def audit(configs, rows, source, native_sha):
    receipts = []
    for name in ("original", "reduced"):
        config_path = configs / (name + ".json")
        config = json.loads(config_path.read_text())
        directory = rows / ("replay-" + name)
        complete = json.loads((directory / "complete.json").read_text())
        assert complete["config_sha256"] == sha(config_path)
        assert complete["input_sha256"] == config["input_sha256"]
        assert complete["method_sha256"] == sha(Path(__file__).with_name("triage.py"))
        measurements = [
            json.loads(line)
            for line in (directory / "rows.jsonl").read_text().splitlines()
        ]
        assert [r["profile"] for r in measurements] == list(config["profiles"])
        for row in measurements:
            profile = config["profiles"][row["profile"]]
            assert profile["git"] == source and profile["sha256"] == native_sha
            assert row["binary_sha256"] == native_sha and row["args"] == profile["args"]
            decisive(row, "sat")
            model_sha = sat_models(
                directory / "models" / row["profile"],
                {config["input"]},
                profile["solver"],
                profile["args"] + ["--check-model"],
            )
            receipts.append(
                {
                    "input": name,
                    "profile": row["profile"],
                    "answer": "sat",
                    "model_metadata_sha256": model_sha,
                }
            )
    config_path = configs / "targeted.json"
    config = json.loads(config_path.read_text())
    assert config["git"] == source and config["sha256"] == native_sha
    target = []
    for slot, profile in enumerate(config["profiles"]):
        directory = rows / "targeted" / f"task-{slot}"
        receipt_path = directory / "receipt.json"
        receipt = json.loads(receipt_path.read_text())
        assert receipt["source"] == source and receipt["native_sha256"] == native_sha
        assert (
            receipt["profile"] == profile
            and receipt["seed"] == config["seed_base"] + slot
        )
        assert receipt["config_sha256"] == sha(config_path)
        assert receipt["generator_sha256"] == sha(
            Path(__file__).with_name("symmetry_gate.py")
        )
        assert receipt["rows_sha256"] == sha(directory / "rows.jsonl")
        measurements = [
            json.loads(line)
            for line in (directory / "rows.jsonl").read_text().splitlines()
        ]
        assert len(measurements) == receipt["count"] == receipt["PASS"] == 1000
        assert [r["index"] for r in measurements] == list(range(1000))
        assert [r["input_sha256"] for r in measurements] == receipt["input_sha256"]
        selected = set()
        for index, row in enumerate(measurements):
            expected = "sat" if index % 2 == 0 else "unsat"
            assert row["expected"] == expected and row["passed"]
            decisive(row["native"], expected)
            decisive(row["reference"], expected)
            if expected == "sat":
                assert (
                    int(
                        row["native"]["stats"].get(
                            ":preprocessing.uf-symmetry.rejected-prior-constraints", 0
                        )
                    )
                    > 0
                )
                selected.add(config["out"] + f"/task-{slot}/cases/{index:04d}.smt2")
        assert (
            receipt["SAT"]
            == receipt["UNSAT"]
            == receipt["SAT_guard_activations"]
            == receipt["SAT_Dolmen_pass"]
            == 500
        )
        assert receipt["model_summary_sha256"] == sha(directory / "models/summary.csv")
        model_sha = sat_models(
            directory / "models",
            selected,
            config["solver"],
            profile["args"] + ["--check-model"],
        )
        target.append(
            {
                "profile": profile,
                "seed": receipt["seed"],
                "PASS": 1000,
                "SAT_Dolmen_pass": 500,
                "SAT_guard_activations": 500,
                "receipt_sha256": sha(receipt_path),
                "model_metadata_sha256": model_sha,
            }
        )
    return {
        "source": source,
        "native_sha256": native_sha,
        "focused_replays": receipts,
        "targeted": target,
        "targeted_PASS": 1000 * len(target),
        "SAT_Dolmen_pass": len(receipts) + 500 * len(target),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--native-sha", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.configs, args.rows, args.source, args.native_sha)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: v
                for k, v in result.items()
                if k not in ("focused_replays", "targeted")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
