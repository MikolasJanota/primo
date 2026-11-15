#!/usr/bin/env python3
"""Route a fixed global slot to the existing pinned timing/model runner."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("timing", "models"))
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--repetition", type=int, choices=range(3), required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    campaign = json.loads(args.campaign.read_text())
    entry = campaign["mapping"][args.slot]
    config = Path(campaign["configs"]) / f"{entry['cohort']}-{args.repetition}.json"
    subprocess.run(
        [
            sys.executable,
            "scripts/qualification/lia_hybrid/run.py",
            args.phase,
            "--config",
            str(config),
            "--slot",
            str(entry["slot"]),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
