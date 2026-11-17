#!/usr/bin/env python3
"""Create private paired-run cards from the frozen public pilot and gate campaign."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def configure(a):
    campaign = json.loads(a.campaign.read_text())
    selection = json.loads(a.selection.read_text())["cases"]
    assert len(selection) == 80
    assert campaign["git"] == campaign["protocol"]["candidate_source"]
    profiles = {
        "BASE": {
            "git": "4f02a8b7",
            "solver": a.baseline_solver,
            "sha256": campaign["protocol"]["baseline_sha256"],
            "args": ["--quiet", "--stats"],
        }
    }
    for label, flags in campaign["protocol"]["profiles"].items():
        profiles[label] = {
            "git": campaign["git"],
            "solver": campaign["solver"],
            "sha256": campaign["sha256"],
            "args": flags,
        }
    a.out.mkdir(parents=True, exist_ok=False)
    for rep in range(3):
        for i, case in enumerate(selection):
            order = list(profiles)
            offset = (i + rep) % 4
            order = order[offset:] + order[:offset]
            if (i + rep) % 2:
                order = order[::-1]
            card = {
                "roots": campaign["roots"],
                "profiles": {p: profiles[p] for p in order},
                "cases": [dict(case, pilot_slot=i, repetition=rep)],
                "timeout": 30,
                "address_space_bytes": 8589934592,
                "out": a.results + f"/rep-{rep}/case-{i}",
            }
            (a.out / f"case-{i}-{rep}.json").write_text(
                json.dumps(card, indent=2) + "\n"
            )
    routing = dict(
        campaign,
        configs=a.remote_configs,
        mapping=[{"cohort": f"case-{i}", "slot": 0} for i in range(80)],
    )
    for name in ("timing-campaign.json", "campaign.json"):
        (a.out / name).write_text(json.dumps(routing, indent=2) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("campaign", "selection", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    for name in ("baseline-solver", "remote-configs", "results"):
        p.add_argument("--" + name, required=True)
    configure(p.parse_args())


if __name__ == "__main__":
    main()
