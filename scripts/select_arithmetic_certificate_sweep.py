#!/usr/bin/env python3
"""Freeze a larger recorded-fast arithmetic certificate sweep and disjoint shards.

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
    parser.add_argument("--logic", choices=("QF_LRA", "QF_LIA"), default="QF_LRA")
    parser.add_argument("--count", type=int, default=256)
    parser.add_argument("--max-per-family", type=int, default=256)
    parser.add_argument("--shards", type=int, default=8)
    parser.add_argument("--cases-per-shard", type=int)
    parser.add_argument(
        "--all-known-unsat",
        action="store_true",
        help="remove historical timing, source-size, family and case-count caps",
    )
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("corpus selection belongs in SLURM")
    if args.shards < 1 or (
        args.cases_per_shard is not None and args.cases_per_shard < 1
    ):
        parser.error("positive shard count and case bound required")
    records = read_history(
        args.history, args.historical_git, None if args.all_known_unsat else 1
    )
    if any(row["historical_solver_args"] != ["--quiet"] for row in records):
        raise ValueError(
            "selection requires the recorded default preprocessing profile"
        )
    controls = json.loads(args.baseline_selection.read_text())["cases"]
    selected = select_cases(
        records,
        args.corpus,
        len(records) if args.all_known_unsat else args.count,
        len(records) if args.all_known_unsat else args.max_per_family,
        tiers=((0, 2**63),) if args.all_known_unsat else SWEEP_TIERS,
        seeds=controls,
        allow_shortfall=True,
    )
    if not args.all_known_unsat and len(selected["cases"]) <= len(controls):
        raise ValueError("no new eligible cases beyond the baseline controls")
    if args.all_known_unsat:
        selected["selection"] = (
            "all recorded native UNSAT, no timing/source-size/family/count caps; "
            "hash-matched controls retained; conservative single-query/"
            "no-positive-arity admission; duplicate hashes excluded"
        )
    if not selected["cases"]:
        raise ValueError("no eligible inputs for full sweep")
    shard_count = (
        (len(selected["cases"]) + args.cases_per_shard - 1) // args.cases_per_shard
        if args.cases_per_shard
        else args.shards
    )
    args.out.mkdir(parents=True, exist_ok=False)
    selected.update(
        logic=args.logic,
        job_id=os.environ["SLURM_JOB_ID"],
        historical_git=args.historical_git,
        historical_seconds_max=None if args.all_known_unsat else 1,
        all_known_unsat=args.all_known_unsat,
        historical_record_count=len(records),
        cases_per_shard=args.cases_per_shard,
        baseline_selection_sha256=digest(args.baseline_selection),
        selector_sha256=digest(Path(__file__)),
        shards=shard_count,
    )
    write_json(args.out / "selection.json", selected)
    manifests = {}
    for shard in range(shard_count):
        filename = f"shard-{shard}.json"
        cases = selected["cases"][shard::shard_count]
        write_json(
            args.out / filename,
            {
                "logic": args.logic,
                "cases": cases,
                "selection_sha256": digest(args.out / "selection.json"),
                "shard_index": shard,
                "shard_count": shard_count,
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
                "requested": selected["requested"],
                "all_known_unsat": args.all_known_unsat,
                "families": selected["families"],
                "tier_selected": selected["tier_selected"],
                "baseline_controls": selected["baseline_controls"],
                "shards": shard_count,
            }
        )
    )


if __name__ == "__main__":
    main()
