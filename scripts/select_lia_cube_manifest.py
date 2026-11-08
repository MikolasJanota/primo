#!/usr/bin/env python3
"""Freeze the cube development set from the audited M124-era artifacts.

Historical rows select cases only; every timing arm must be rerun. The source
provenance contains input hashes; the cluster audit must verify the full frozen
inventory before using the selected files.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict, deque
from pathlib import Path


def read_rows(paths: list[Path]) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for path in sorted(paths):
        with path.open() as stream:
            for row in csv.DictReader(stream):
                name = row["file"].removeprefix("sandbox/non-incremental/QF_LIA/")
                if name in rows:
                    raise ValueError(f"duplicate input {name}")
                rows[name] = row
    return rows


def solved(row: dict[str, str]) -> bool:
    return row["status"] == "ok" and row["answer"] in {"sat", "unsat"}


def balanced(names: list[str], count: int) -> list[str]:
    families: dict[str, deque[str]] = defaultdict(deque)
    for name in sorted(names):
        families[name.split("/")[0]].append(name)
    selected: list[str] = []
    while families and len(selected) < count:
        for family in sorted(families):
            selected.append(families[family].popleft())
            if not families[family]:
                del families[family]
            if len(selected) == count:
                break
    return selected


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    native = read_rows(list((args.artifacts / "full60/all").glob("shard-*/answers.csv")))
    reference = read_rows(list((args.artifacts / "reference-z3").glob("shard-*/answers.csv")))
    if len(native) != 13306 or native.keys() != reference.keys():
        raise ValueError("expected identical audited 13,306-file sets")
    hashes: dict[str, str] = {}
    for path in sorted((args.artifacts / "full60/all").glob("shard-*/provenance.json")):
        for name, digest in json.loads(path.read_text())["inputs"].items():
            if name in hashes and hashes[name] != digest:
                raise ValueError(f"conflicting hash for {name}")
            hashes[name] = digest
    if hashes.keys() != native.keys():
        raise ValueError("incomplete content-hash inventory")
    buckets: list[tuple[str, list[str]]] = []
    for answer in ("sat", "unsat"):
        candidates = [name for name, row in native.items()
                      if solved(reference[name]) and reference[name]["answer"] == answer
                      and float(reference[name]["seconds"]) <= 1
                      and (not solved(row) or float(row["seconds"]) >=
                           max(1, 5 * float(reference[name]["seconds"])))]
        buckets.append((f"reference-fast-native-slow-{answer}", candidates))
    buckets.append(("clean-unknown-node-limit-candidates",
                    [name for name, row in native.items()
                     if row["status"] == "ok" and row["answer"] == "unknown"]))
    buckets.append(("easy-controls", [name for name, row in native.items()
                                     if solved(row) and solved(reference[name])
                                     and float(row["seconds"]) <= .1
                                     and float(reference[name]["seconds"]) <= .1]))
    used: set[str] = set()
    entries = []
    accounting = []
    for bucket, candidates in buckets:
        available = [name for name in candidates if name not in used]
        chosen = balanced(available, 50)
        refill = balanced([name for name in native if name not in used and name not in chosen],
                          50 - len(chosen)) if len(chosen) < 50 else []
        accounting.append({"bucket": bucket, "available": len(available), "refill": len(refill)})
        for name in chosen + refill:
            entries.append({"file": name, "sha256": hashes[name], "bucket": bucket,
                            "refill": name in refill, "reference_answer": reference[name]["answer"],
                            "historical_native_status": native[name]["status"],
                            "historical_native_answer": native[name]["answer"]})
        used.update(chosen + refill)
    if len(used) != 200:
        raise ValueError("development manifest is not 200 distinct files")
    args.out.mkdir(parents=True, exist_ok=True)
    inventory = "".join(f"{name}\t{hashes[name]}\n" for name in sorted(hashes))
    (args.out / "inventory.tsv").write_text(inventory)
    (args.out / "inputs.txt").write_text("".join(entry["file"] + "\n" for entry in entries))
    (args.out / "selection.json").write_text(json.dumps({
        "recipe": "50 per ordered bucket, sorted family round-robin, disjoint; refill from remaining inventory",
        "bucket_accounting": accounting, "inputs": entries,
        "inventory_sha256": hashlib.sha256(inventory.encode()).hexdigest(),
        "corpus_files": len(hashes),
        "note": "Historical rows select only; node-limit candidates need fresh reason verification."
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
