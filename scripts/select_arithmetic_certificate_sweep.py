#!/usr/bin/env python3
"""Freeze a larger recorded-fast QF_LRA certificate sweep and disjoint shards.

Selection uses the documented pilot's deterministic size/family sampling with
reserved previous controls. See docs/reference/qf-lra-certificates.md.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from arithmetic_certificate_stress import select_cases
from micromath_stress import digest, write_json
from select_micromath_fast_cases import read_history

SWEEP_TIERS = ((4096, 32768), (32768, 131072), (131072, 524288), (524288, 8388609))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("history", "corpus", "baseline-selection", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--historical-git", required=True)
    parser.add_argument("--count", type=int, default=256)
    parser.add_argument("--max-per-family", type=int, default=256)
    parser.add_argument("--shards", type=int, default=8)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("corpus selection belongs in SLURM")
    if args.shards < 1:
        parser.error("positive shard count required")
    records = read_history(args.history, args.historical_git, 1)
    if any(row["historical_solver_args"] != ["--quiet"] for row in records):
        raise ValueError(
            "selection requires the recorded default preprocessing profile"
        )
    controls = json.loads(args.baseline_selection.read_text())["cases"]
    selected = select_cases(
        records,
        args.corpus,
        args.count,
        args.max_per_family,
        tiers=SWEEP_TIERS,
        seeds=controls,
        allow_shortfall=True,
    )
    if len(selected["cases"]) <= len(controls):
        raise ValueError("no new eligible cases beyond the baseline controls")
    args.out.mkdir(parents=True, exist_ok=False)
    selected.update(
        logic="QF_LRA",
        job_id=os.environ["SLURM_JOB_ID"],
        historical_git=args.historical_git,
        historical_seconds_max=1,
        baseline_selection_sha256=digest(args.baseline_selection),
        selector_sha256=digest(Path(__file__)),
        shards=args.shards,
    )
    write_json(args.out / "selection.json", selected)
    manifests = {}
    for shard in range(args.shards):
        filename = f"shard-{shard}.json"
        cases = selected["cases"][shard :: args.shards]
        write_json(
            args.out / filename,
            {
                "logic": "QF_LRA",
                "cases": cases,
                "selection_sha256": digest(args.out / "selection.json"),
                "shard_index": shard,
                "shard_count": args.shards,
            },
        )
        manifests[filename] = {
            "sha256": digest(args.out / filename),
            "cases": len(cases),
        }
    write_json(args.out / "manifest-hashes.json", manifests)
    print(
        json.dumps(
            {
                "selected": len(selected["cases"]),
                "requested": args.count,
                "families": selected["families"],
                "tier_selected": selected["tier_selected"],
                "baseline_controls": selected["baseline_controls"],
                "shards": args.shards,
            }
        )
    )


if __name__ == "__main__":
    main()
