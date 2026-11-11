#!/usr/bin/env python3
"""Export portable timing/counter evidence and audit each SAT coverage receipt."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--slots", type=int, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    counts = Counter()
    with (args.out / "timing-rows.jsonl").open("w") as stream:
        for slot in range(args.slots):
            directory = args.root / f"slot-{slot}"
            assert (directory / "timing-complete").is_file()
            provenance = json.loads((directory / "provenance.json").read_text())
            coverage = json.loads((directory / "coverage.json").read_text())
            rows = [json.loads(s) for s in (directory / "rows.jsonl").read_text().splitlines()]
            selected = {
                row["profile"]
                for row in rows
                if row["status"] == "ok" and row["exit_code"] == 0 and row["answer"] == "sat"
            }
            assert {row["profile"] for row in rows} == {"private", "hybrid"}
            assert len(rows) == 2
            assert coverage["selected_sat"] == coverage["direct_dolmen_pass"] == len(selected)
            assert {row["profile"] for row in coverage["rows"]} == selected
            for receipt in coverage["rows"]:
                summary = directory / "models" / receipt["profile"] / "summary.csv"
                assert sha(summary) == receipt["summary_sha256"]
                with summary.open(newline="") as model_stream:
                    (model,) = csv.DictReader(model_stream)
                assert model["answer"] == "sat" and model["outcome"] == "pass"
                assert model["validator"] == "dolmen"
                assert model["solver_exit"] == model["dolmen_exit"] == "0"
                counts[receipt["profile"] + "_SAT_Dolmen_pass"] += 1
            portable = {
                "slot": slot,
                "case": provenance["case"],
                "job": provenance["job"],
                "host": provenance["host"],
                "affinity": provenance["affinity"],
                "order": provenance["order"],
                "config_sha256": provenance["config_sha256"],
                "runner_sha256": provenance["runner_sha256"],
                "raw_rows_sha256": sha(directory / "rows.jsonl"),
                "coverage": coverage,
                "rows": [],
            }
            for row in rows:
                label = row["profile"]
                counts["timing_rows"] += 1
                kept = {k: v for k, v in row.items() if k not in ("stats", "stdout_log", "stderr_log")}
                kept["binary_sha256"] = provenance["profiles"][label]["sha256"]
                kept["git"] = provenance["profiles"][label]["git"]
                kept["stats"] = {
                    key: value
                    for key, value in row.get("stats", {}).items()
                    if key.startswith((":lia.", ":time.lia.", ":sat.", ":theory."))
                }
                for kind in ("stdout", "stderr"):
                    kept[kind + "_sha256"] = sha(directory / f"{label}.{kind}")
                portable["rows"].append(kept)
            stream.write(json.dumps(portable, separators=(",", ":")) + "\n")
    (args.out / "coverage.json").write_text(json.dumps(dict(counts), indent=2) + "\n")


if __name__ == "__main__":
    main()
