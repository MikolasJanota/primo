#!/usr/bin/env python3
"""Generate private integer/real cohort configs without duplicating inert H rows."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.profiles import (
    INTEGER_LOGICS,
    profiles,
    tasks,
)  # noqa: E402


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("selection", "protocol", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in (
        "solver",
        "git",
        "sha256",
        "remote-configs",
        "results-root",
        "mutation-candidates",
    ):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--root", action="append", required=True)
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_text())
    assert (
        hashlib.sha256(args.selection.read_bytes()).hexdigest()
        == protocol["selection_sha256"]
    )
    cases = json.loads(args.selection.read_text())["cases"]
    roots = dict(x.split("=", 1) for x in args.root)
    assert len(cases) == protocol["inputs"] == 697
    assert protocol["tasks"] == tasks()
    args.out.mkdir(parents=True, exist_ok=False)
    mapping = []
    cohorts = {"integers": [], "reals": []}
    for index, case in enumerate(cases):
        cohort = "integers" if case["logic"] in INTEGER_LOGICS else "reals"
        mapping.append({"cohort": cohort, "slot": len(cohorts[cohort])})
        cohorts[cohort].append(dict(case, campaign_slot=index))
    for rep in range(3):
        for cohort, selected in cohorts.items():
            flags = profiles(selected[0]["logic"])
            assert all(profiles(c["logic"]) == flags for c in selected)
            groups = (
                [["B", "H"], ["P", "PH"], ["R", "RH"], ["PR", "PRH"]]
                if cohort == "integers"
                else [["B", "P"], ["R", "PR"]]
            )
            config = {
                "cases": [dict(c, repetition=rep) for c in selected],
                "roots": roots,
                "out": f"{args.results_root}/full/rep-{rep}/{cohort}",
                "timeout": 120,
                "address_space_bytes": 8589934592,
                "paired_groups": groups,
                "profiles": {
                    label: {
                        "solver": args.solver,
                        "git": args.git,
                        "sha256": args.sha256,
                        "args": argv,
                    }
                    for label, argv in flags.items()
                },
            }
            write(args.out / f"{cohort}-{rep}.json", config)
    master = {
        "solver": args.solver,
        "git": args.git,
        "sha256": args.sha256,
        "roots": roots,
        "mutation_candidates": args.mutation_candidates,
        "results": args.results_root,
        "configs": args.remote_configs,
        "mapping": mapping,
        "tasks": tasks(),
        "protocol": protocol,
    }
    write(args.out / "campaign.json", master)
    write(args.out / "profile-args-integers.json", profiles("QF_LIA"))
    write(args.out / "profile-args-reals.json", profiles("QF_LRA"))


if __name__ == "__main__":
    main()
