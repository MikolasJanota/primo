#!/usr/bin/env python3
"""Create content-pinned EUF milestone pilot and five-logic control run cards."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n")


def configure(a):
    campaign = json.loads(a.campaign.read_text())
    sources = json.loads(a.sources.read_text())
    pilot = json.loads(a.pilot.read_text())["cases"]
    controls = json.loads(a.controls.read_text())["cases"]
    controls = controls + [c for c in pilot if c["logic"] == "QF_UF"]
    assert len(pilot) == 80 and len(controls) == 737
    assert set(sources) == {"BASE", "MECH", "OFF", "OLDON", "ON", "DEMAND"}
    for label, source in sources.items():
        assert set(source) == {"solver", "git", "sha256", "args"}, label
    a.out.mkdir(parents=True, exist_ok=False)
    manifests = {}
    for group, cases in (("pilot", pilot), ("controls", controls)):
        profiles = (
            sources if group == "pilot" else {k: sources[k] for k in ("BASE", "OFF")}
        )
        mapping = []
        if group == "pilot":
            mapping = [{"cohort": f"case-{i}", "slot": 0} for i in range(len(cases))]
        else:
            mapping = [{"cohort": "controls", "slot": i} for i in range(len(cases))]
        for rep in range(3):
            batches = [[c] for c in cases] if group == "pilot" else [cases]
            for i, batch in enumerate(batches):
                labels = list(profiles)
                offset = (i + rep) % len(labels)
                labels = labels[offset:] + labels[:offset]
                if (i + rep) % 2:
                    labels.reverse()
                cohort = f"case-{i}" if group == "pilot" else "controls"
                card = {
                    "roots": campaign["roots"],
                    "profiles": {k: profiles[k] for k in labels},
                    "cases": [dict(c, repetition=rep) for c in batch],
                    "timeout": 30,
                    "address_space_bytes": 8589934592,
                    "out": f"{a.results}/{group}/rep-{rep}/{cohort}",
                }
                write(a.out / f"{group}-{cohort}-{rep}.json", card)
        manifests[group] = {
            "configs": a.remote_configs,
            "mapping": [dict(e, cohort=group + "-" + e["cohort"]) for e in mapping],
            "selection": cases,
            "profiles": profiles,
        }
        write(a.out / (group + "-campaign.json"), manifests[group])
    write(
        a.out / "master-campaign.json",
        {
            "configs": a.remote_configs,
            "mapping": [
                entry
                for group in ("pilot", "controls")
                for entry in manifests[group]["mapping"]
            ],
        },
    )
    write(
        a.out / "protocol.json",
        {
            "scope": "Opt-in EUF milestone pilot and default five-ground-logic controls; no default promotion",
            "pilot_selection_sha256": sha(a.pilot),
            "arithmetic_control_selection_sha256": sha(a.controls),
            "sources": sources,
            "configurer_sha256": sha(Path(__file__)),
            "native_runner_sha256": sha(
                Path("scripts/qualification/lia_hybrid/run.py")
            ),
            "timeout_seconds": 30,
            "address_space_bytes": 8589934592,
            "repetitions": 3,
            "pilot_cases": 80,
            "control_cases": 737,
            "pilot_rows": 1440,
            "control_rows": 4422,
            "models": "Every successful native SAT row replayed independently with Dolmen outside timing",
        },
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("campaign", "sources", "pilot", "controls", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    for name in ("remote-configs", "results"):
        p.add_argument("--" + name, required=True)
    configure(p.parse_args())


if __name__ == "__main__":
    main()
