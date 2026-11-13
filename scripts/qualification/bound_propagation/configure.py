#!/usr/bin/env python3
"""Create three private, same-binary configurations for the propagation factorial."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument(
        "--root", action="append", required=True, help="LOGIC=corpus-root"
    )
    parser.add_argument("--solver", required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--git", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--results-root", required=True)
    args = parser.parse_args()
    selection = json.loads(args.selection.read_text())
    protocol = json.loads(args.protocol.read_text())
    roots = dict(item.split("=", 1) for item in args.root)
    assert len(selection["cases"]) == protocol["inputs"]
    args.out.mkdir(parents=True, exist_ok=True)
    names = list(protocol["profiles"])
    for repetition in range(protocol["repetitions"]):
        order = names[repetition:] + names[:repetition]
        config = {
            "cases": [dict(c, repetition=repetition) for c in selection["cases"]],
            "roots": roots,
            "out": f"{args.results_root}/rep-{repetition}",
            "timeout": protocol["time_limit_seconds"],
            "address_space_bytes": protocol["address_space_bytes"],
            "profiles": {
                label: {
                    "solver": args.solver,
                    "git": args.git,
                    "sha256": args.sha256,
                    "args": protocol["profiles"][label],
                }
                for label in order
            },
        }
        (args.out / f"config-{repetition}.json").write_text(
            json.dumps(config, indent=2) + "\n"
        )


if __name__ == "__main__":
    main()
