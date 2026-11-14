#!/usr/bin/env python3
"""Audit fresh differential rows, then independently validate their SAT models."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = (
    Path(__file__).resolve().parents[3]
    if "qualification" in str(Path(__file__))
    else Path.cwd()
)
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--allow-private-node-limit", action="store_true")
    parser.add_argument("--allow-reference-timeout", action="store_true")
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    out = args.root / f"gate-{args.slot}"
    with (out / "summary.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1000
    reference_timeouts = [
        r
        for r in rows
        if args.allow_reference_timeout
        and r["outcome"] == "REFERENCE_UNUSABLE"
        and r.get("refs") == "TIMEOUT"
        and r.get("ours") == "sat"
    ]
    unresolved = [
        r
        for r in rows
        if r["outcome"] not in ("PASS", "TIMEOUT") and r not in reference_timeouts
    ]
    assert not unresolved or args.allow_private_node_limit
    assert all(
        r["outcome"] == "UNKNOWN_INVOLVED" and r.get("ours") == "unknown"
        for r in unresolved
    )
    selected = [
        r.get("case") or r["file"]
        for r in rows
        if (r["outcome"] == "PASS" or r in reference_timeouts)
        and (r.get("ours") or r.get("answer")) == "sat"
    ]
    manifest = out / "sat.txt"
    manifest.write_text("".join(f"{path}\n" for path in selected))
    flags = [
        "--quiet",
        "--lra-propagation",
        "--lra-row-propagation",
        "--lra-bidirectional-row-propagation",
        "--check-model",
    ]
    if 4 <= args.slot < 8 or args.slot == 9:
        flags.append("--preprocess-bound-tightening")
    if args.slot >= 8:
        flags.append("--lia-round-integer-bounds")
    resource_unknowns = []
    for row in unresolved:
        source = Path(row.get("case") or row["file"])
        replay = run_solver(["build-slurm-rel/primo", *flags, "--stats"], source, 5)
        assert replay["status"] == "ok" and replay["exit_code"] == 0
        assert replay["answer"] == "unknown"
        assert "integer branch-and-bound node limit" in replay["stderr"].splitlines()
        assert int(replay["stats"].get(":lia.branching.budget-exits", 0)) > 0
        resource_unknowns.append(
            {
                "case": source.name,
                "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "reason": "integer branch-and-bound node limit",
                "private_nodes": replay["stats"].get(":lia.branching.private-nodes"),
                "budget_exits": replay["stats"].get(":lia.branching.budget-exits"),
            }
        )
    if selected:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={f}" for f in flags),
                "--manifest",
                str(manifest),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out / "models"),
            ],
            check=True,
        )
        with (out / "models/summary.csv").open() as stream:
            models = list(csv.DictReader(stream))
        assert len(models) == len(selected)
        assert all(
            r["outcome"] == "pass"
            and r["validator"] == "dolmen"
            and r["solver_exit"] == r["dolmen_exit"] == "0"
            for r in models
        )
    receipt = {
        "slot": args.slot,
        "reference_timeouts_validated_separately": [
            Path(r["file"]).name for r in reference_timeouts
        ],
        "solver_args": flags,
        "source_snapshot": os.environ["PRIMO_EXPECTED_GIT"],
        "native_sha256": hashlib.sha256(
            Path("build-slurm-rel/primo").read_bytes()
        ).hexdigest(),
        "count": len(rows),
        "seed": int(os.environ.get("PRIMO_FUZZ_SEED_BASE", "2026111440")) + args.slot,
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "resource_unknowns": resource_unknowns,
        "selected_sat": len(selected),
        "direct_dolmen_pass": len(selected),
        "summary_sha256": hashlib.sha256(
            (out / "summary.csv").read_bytes()
        ).hexdigest(),
        "job": os.environ["SLURM_JOB_ID"],
    }
    (out / "coverage.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
