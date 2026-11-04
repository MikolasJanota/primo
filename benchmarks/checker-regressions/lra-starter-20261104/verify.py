#!/usr/bin/env python3
"""Check bundle hashes and expected arithmetic results; optionally confirm UNSAT."""

import argparse
import hashlib
import json
from pathlib import Path

from check_lra_certificate import check_file


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sat-solver", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "manifest.json").read_text())
    for relative, record in manifest["files"].items():
        data = (root / relative).read_bytes()
        if (
            len(data) != record["bytes"]
            or hashlib.sha256(data).hexdigest() != record["sha256"]
        ):
            raise ValueError(f"file hash/size mismatch: {relative}")
    accepted = 0
    rejected = 0
    for case in [*manifest["cases"], *manifest["controls"]]:
        path = root / case["certificate"]
        try:
            result = check_file(path, args.sat_solver)
        except ValueError as error:
            if (
                case["expected"] != "reject"
                or str(error) != "invalid weighted contradiction"
            ):
                raise
            rejected += 1
        else:
            if case["expected"] == "reject":
                raise ValueError(f"invalid witness accepted: {path.name}")
            if (
                result["conflicts"] != case["check"]["conflicts"]
                or result["entries"] != case["check"]["entries"]
            ):
                raise ValueError(f"unexpected witness counts: {path.name}")
            accepted += 1
    print(
        json.dumps(
            {
                "hashes": "passed",
                "accepted": accepted,
                "rejected": rejected,
                "propositional_unsat_checked": bool(args.sat_solver),
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
