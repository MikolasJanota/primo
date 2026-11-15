#!/usr/bin/env python3
"""Fixed P/R/H factorial profiles; real arithmetic has no integer H factor."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

INTEGER_LOGICS = ("QF_LIA", "QF_UFLIA")
REAL_LOGICS = ("QF_LRA", "QF_UFLRA")
LABELS = ("B", "P", "R", "PR", "H", "PH", "RH", "PRH")


def factors(label: str) -> dict[str, bool]:
    if label not in LABELS:
        raise ValueError(label)
    return {key: key in label for key in ("P", "R", "H")}


def profiles(logic: str) -> dict[str, list[str]]:
    if logic not in (*INTEGER_LOGICS, *REAL_LOGICS):
        raise ValueError(logic)
    result = {}
    for label in LABELS:
        value = factors(label)
        if value["H"] and logic in REAL_LOGICS:
            continue
        flags = ["--quiet", "--stats", "--lra-propagation"]
        if value["P"]:
            flags += ["--preprocess-bound-tightening"]
        if value["R"]:
            flags += ["--lra-row-propagation", "--lra-bidirectional-row-propagation"]
        if logic in INTEGER_LOGICS:
            flags += [
                f"--lia-branching={'hybrid' if value['H'] else 'private'}",
                "--lia-private-branch-factor=1",
                "--lia-split-atom-limit=0",
                "--lia-branch-limit=100000",
            ]
        result[label] = flags
    return result


def tasks() -> list[dict]:
    return [
        {"logic": logic, "label": label, "args": args, "factors": factors(label)}
        for logic in (*INTEGER_LOGICS, *REAL_LOGICS)
        for label, args in profiles(logic).items()
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    value = {"tasks": tasks()}
    args.out.write_text(json.dumps(value, indent=2) + "\n")


if __name__ == "__main__":
    main()
