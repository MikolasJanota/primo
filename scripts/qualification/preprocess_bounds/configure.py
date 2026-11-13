#!/usr/bin/env python3
"""Create private paired-run configuration from a frozen public selection."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument(
        "--root", action="append", required=True, help="LOGIC=remote-corpus-root"
    )
    for profile in ("baseline", "candidate"):
        parser.add_argument(f"--{profile}-solver", required=True)
        parser.add_argument(f"--{profile}-sha256", required=True)
        parser.add_argument(f"--{profile}-git", required=True)
    parser.add_argument(
        "--repeat-report", type=Path, help="Select each reported regression twice"
    )
    args = parser.parse_args()
    roots = dict(item.split("=", 1) for item in args.root)
    cases = json.loads(args.selection.read_text())["cases"]
    if args.repeat_report:
        report = json.loads(args.repeat_report.read_text())
        cases = [
            dict(cases[item["slot"]], parent_slot=item["slot"], repetition=repetition)
            for item in report["regressions_to_repeat"]
            for repetition in (1, 2)
        ]
    assert cases and all(case["logic"] in roots for case in cases)
    profiles = {}
    for label, profile in (("baseline", "baseline"), ("tightened", "candidate")):
        profiles[label] = {
            "solver": getattr(args, f"{profile}_solver"),
            "git": getattr(args, f"{profile}_git"),
            "sha256": getattr(args, f"{profile}_sha256"),
            "args": ["--quiet", "--stats"]
            + (["--preprocess-bound-tightening"] if label == "tightened" else []),
        }
    config = {
        "cases": cases,
        "roots": roots,
        "out": args.output_root,
        "timeout": 120,
        "address_space_bytes": 8 * 2**30,
        "profiles": profiles,
    }
    args.output.write_text(json.dumps(config, indent=2) + "\n")


if __name__ == "__main__":
    main()
