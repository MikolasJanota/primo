#!/usr/bin/env python3
"""Audit complete paired follow-ups and conflict-bounded diagnostic prefixes."""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def audit(config_path: Path, directory: Path) -> dict:
    config = json.loads(config_path.read_text())
    case = config["cases"][0]
    assert (directory / "timing-complete").is_file()
    provenance = json.loads((directory / "provenance.json").read_text())
    assert provenance["config_sha256"] == sha(config_path)
    assert provenance["runner_sha256"] == sha(
        ROOT / "scripts/qualification/lia_hybrid/run.py"
    )
    assert provenance["profiles"] == config["profiles"] and provenance["case"] == case
    rows = [
        json.loads(line) for line in (directory / "rows.jsonl").read_text().splitlines()
    ]
    assert len(rows) == 4 and {r["profile"] for r in rows} == set(config["profiles"])
    assert (
        [r["profile"] for r in rows]
        == provenance["order"]
        == measurement_order(config, 0)
    )
    for row in rows:
        audit_row(row)
        assert row["file"] == case["file"] and row["logic"] == case["logic"]
        assert row["args"] == config["profiles"][row["profile"]]["args"] + case.get(
            "extra_args", []
        )
        if solved(row) and case.get("expected") in {"sat", "unsat"}:
            assert row["answer"] == case["expected"]
    selected = {r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"}
    models = audit_models(directory, selected, config, case)
    assert models is not None, directory
    return {
        "case": case,
        "config_sha256": sha(config_path),
        "rows_sha256": sha(directory / "rows.jsonl"),
        "provenance_sha256": sha(directory / "provenance.json"),
        "coverage_sha256": sha(directory / "coverage.json"),
        "model_metadata_sha256": models,
        "selected_SAT": len(selected),
        "profiles": {
            label: {k: v for k, v in profile.items() if k != "solver"}
            for label, profile in config["profiles"].items()
        },
        "rows": [
            {k: v for k, v in r.items() if k not in {"stdout_log", "stderr_log"}}
            for r in rows
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--rows", type=Path, required=True)
    parser.add_argument("--prefix-configs", type=Path, required=True)
    parser.add_argument("--prefix-rows", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.configs / "manifest.json").read_text())
    assert len(manifest) == 6
    followups = []
    for item in manifest:
        index = item["index"]
        followups.append(
            {
                **item,
                "repetitions": [
                    audit(
                        args.configs / f"case-{index}/config-{r}.json",
                        args.rows / f"case-{index}/rep-{r}/slot-0",
                    )
                    for r in range(3)
                ],
            }
        )
    prefixes = []
    prefix_manifest = json.loads((args.prefix_configs / "manifest.json").read_text())
    assert len(prefix_manifest) == 15
    for item in prefix_manifest:
        index = item["index"]
        result = audit(
            args.prefix_configs / f"config-{index}.json",
            args.prefix_rows / f"case-{index}/slot-0",
        )
        for row in result["rows"]:
            if row["status"] == "ok" and row["answer"] == "unknown":
                assert (
                    int(row["stats"].get(":theory.conflicts", 0))
                    >= item["max_theory_conflicts"]
                    or int(row["stats"].get(":lia.branching.budget-exits", 0)) > 0
                )
        prefixes.append({**item, **result})
    report = {
        "status": "qualified",
        "native_source": "07ec5ce5",
        "followups": followups,
        "prefixes": prefixes,
        "timing_rows": 72,
        "prefix_rows": 60,
        "followup_SAT_Dolmen_pass": sum(
            r["selected_SAT"] for f in followups for r in f["repetitions"]
        ),
        "prefix_SAT_Dolmen_pass": sum(p["selected_SAT"] for p in prefixes),
        "prefix_outcomes": dict(
            collections.Counter(
                r["status"] + "/" + r["answer"] for p in prefixes for r in p["rows"]
            )
        ),
        "limitations": "Prefixes stop at1/10/100reported theory conflicts or15s; not full-run speed comparisons. Follow-up timings keep120s/8GiB and run three paired repetitions; model replay is separate.",
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {k: v for k, v in report.items() if k not in {"followups", "prefixes"}},
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
