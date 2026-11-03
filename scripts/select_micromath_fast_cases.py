#!/usr/bin/env python3
"""Select large checker-compatible inputs from recorded fast native UNSAT rows."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
from collections import Counter
from pathlib import Path

DECLARATION = re.compile(
    r"\(declare-fun\s+(\|[^|]*\||[^()\s]+)\s*\(([^()]*)\)\s*(\|[^|]*\||[^()\s]+)\s*\)"
)
TOKEN = re.compile(r"\|[^|]*\||[^\s()]+")


def read_history(directory: Path, source_git: str) -> list[dict]:
    """Join completed benchmark answers to their recorded per-input hashes."""
    records = []
    seen = set()
    for answers in sorted(directory.glob("shard-*/answers.csv")):
        provenance = json.loads(answers.with_name("provenance.json").read_text())
        if provenance["status"] != "complete" or not provenance["inputs_unchanged"]:
            continue
        version = provenance["solver"]["version"]
        revision = re.search(r"(?m)^git: ([0-9a-f]+)$", version)
        if revision is None or not source_git.startswith(revision[1]):
            raise ValueError(
                "historical source revision does not match recorded binary"
            )
        with answers.open(newline="") as stream:
            for row in csv.DictReader(stream):
                if (row["status"], row["answer"], row["wrong"], row["exit_code"]) != (
                    "ok",
                    "unsat",
                    "false",
                    "0",
                ) or not 0 <= float(row["seconds"]) <= 1:
                    continue
                if row["file"] in seen:
                    raise ValueError("duplicate historical input row")
                seen.add(row["file"])
                records.append(
                    {
                        "file": row["file"],
                        "expected": "unsat",
                        "family": row["family"],
                        "sha256": provenance["inputs"][row["file"]],
                        "historical_seconds": float(row["seconds"]),
                        "historical_source_git": source_git,
                        "historical_solver_args": provenance["solver_args"],
                        "historical_binary_sha256": provenance["solver"]["sha256"],
                        "historical_record": str(answers.relative_to(directory)),
                    }
                )
    if not records:
        raise ValueError("no completed historical fast-UNSAT rows")
    return records


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    history = parser.add_mutually_exclusive_group(required=True)
    history.add_argument("--records", type=Path)
    history.add_argument("--history", type=Path)
    parser.add_argument("--historical-source-git")
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--exclude-manifest", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--count", type=int, default=6)
    parser.add_argument("--max-per-family", type=int, default=2)
    parser.add_argument("--min-bytes", type=int, default=65536)
    parser.add_argument("--max-bytes", type=int, default=2097152)
    args = parser.parse_args()
    if args.count < 1 or args.max_per_family < 1:
        parser.error("case and family limits must be positive")
    if not 0 <= args.min_bytes <= args.max_bytes:
        parser.error("invalid source-size interval")
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("corpus selection belongs in SLURM")
    if args.history and not args.historical_source_git:
        parser.error("--history requires --historical-source-git")
    records = (
        read_history(args.history, args.historical_source_git)
        if args.history
        else json.loads(args.records.read_text())
    )
    previous = [
        row
        for manifest in args.exclude_manifest
        for row in json.loads(manifest.read_text())["cases"]
    ]
    excluded = {row["file"] for row in previous}
    excluded_hashes = {row["sha256"] for row in previous}
    selected_hashes: set[str] = set()
    candidates = []
    for row in records:
        relative = Path(row["file"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("unsafe historical benchmark path")
        if row["expected"] != "unsat" or not 0 <= row["historical_seconds"] <= 1:
            raise ValueError(
                "records must contain confirmed native UNSAT rows within one second"
            )
        path = args.corpus / relative
        if (
            row["file"] not in excluded
            and row["sha256"] not in excluded_hashes
            and path.is_file()
        ):
            size = path.stat().st_size
            if args.min_bytes <= size <= args.max_bytes:
                candidates.append({**row, "bytes": size})
    candidates.sort(key=lambda row: (-row["bytes"], row["file"]))
    selected = []
    families: Counter[str] = Counter()
    rejected: Counter[str] = Counter()
    for row in candidates:
        if families[row["family"]] >= args.max_per_family:
            continue
        if row["sha256"] in selected_hashes:
            rejected["duplicate_input_hash"] += 1
            continue
        raw = (args.corpus / row["file"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != row["sha256"]:
            rejected["historical_hash_mismatch"] += 1
            continue
        source = raw.decode()
        incompatible = False
        for declaration in DECLARATION.finditer(source):
            arguments = TOKEN.findall(declaration[2])
            if arguments and (
                declaration[3].strip("|") == "Bool"
                or any(argument.strip("|") == "Bool" for argument in arguments)
            ):
                incompatible = True
                break
        if incompatible:
            rejected["boolean_function_signature"] += 1
            continue
        selected.append(row)
        selected_hashes.add(row["sha256"])
        families[row["family"]] += 1
        if len(selected) == args.count:
            break
    result = {
        "selection": (
            f"recorded native UNSAT<=1s;{args.min_bytes}..{args.max_bytes} source bytes;"
            f"largest first,max{args.max_per_family}perfamily;historical input hashes match;"
            "exclude prior paths/hashes, duplicate hashes and Boolean function signatures"
        ),
        "requested_cases": args.count,
        "selected_cases": len(selected),
        "max_per_family": args.max_per_family,
        "family_counts": dict(families),
        "source_size_bytes": {"minimum": args.min_bytes, "maximum": args.max_bytes},
        "excluded_manifests": [str(path) for path in args.exclude_manifest],
        "native_configuration": "historical default --quiet; not a claim about current exporter timing",
        "record_count": len(records),
        "size_eligible": len(candidates),
        "rejected_before_selection": dict(rejected),
        "job_id": os.environ["SLURM_JOB_ID"],
        "cases": selected,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "cases"}))
    for row in selected:
        print(row["file"], row["bytes"], row["historical_seconds"])


if __name__ == "__main__":
    main()
