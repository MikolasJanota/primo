#!/usr/bin/env python3
"""Pin selected paired redundancy measurements using an existing corpus selection."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--results-root", required=True)
    for variant in ("control", "audit"):
        for field in ("solver", "git", "sha256"):
            parser.add_argument(f"--{variant}-{field}", required=True)
    args = parser.parse_args()
    template = json.loads(args.template.read_text())
    slots = [2, 17, 64, 213, 438, 597]
    profiles = {}
    for name in ("rows", "both"):
        flags = template["profiles"][f"candidate-{name}"]["args"]
        for variant in ("control", "audit"):
            profiles[f"{variant}-{name}"] = {
                field: getattr(args, f"{variant}_{field}")
                for field in ("solver", "git", "sha256")
            } | {"args": flags}
    manifest = []
    for phase in ("complete", "prefix"):
        entries = []
        for slot in slots:
            for limit in [None] if phase == "complete" else [100, 1000]:
                index = len(entries)
                entry = {"index": index, "original_slot": slot, "conflict_limit": limit}
                entries.append(entry)
                for repetition in range(3 if phase == "complete" else 1):
                    case = dict(template["cases"][slot], repetition=repetition)
                    if limit is not None:
                        case["extra_args"] = ["--max-theory-conflicts", str(limit)]
                    config = {
                        "cases": [case],
                        "roots": template["roots"],
                        "out": f"{args.results_root}/{phase}/case-{index}/rep-{repetition}",
                        "timeout": 120 if phase == "complete" else 30,
                        "address_space_bytes": 8 * 1024**3,
                        "profiles": profiles,
                        "paired_groups": [
                            [f"control-{p}", f"audit-{p}"] for p in ("rows", "both")
                        ],
                    }
                    target = (
                        args.out / phase / f"case-{index}" / f"config-{repetition}.json"
                    )
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text(json.dumps(config, indent=2) + "\n")
        manifest.append({"phase": phase, "cases": entries})
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
