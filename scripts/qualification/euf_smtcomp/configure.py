#!/usr/bin/env python3
"""Freeze official UF/UFLIA selections and content-pinned paired run cards."""

from __future__ import annotations

import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path

OFFICIAL_SHA = "7abe5cadf800cc84db00506f85176378f714cdaa8c02a0e491059835f5039c58"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def configure(args):
    assert sha(args.official) == OFFICIAL_SHA
    official = json.loads(gzip.decompress(args.official.read_bytes()))["results"]
    answers = collections.defaultdict(set)
    for row in official:
        file = row["file"]
        logic = file["logic"]
        if row["track"] == "SingleQuery" and logic in ("QF_UF", "QF_UFLIA"):
            assert not file["incremental"]
            key = logic, "/".join([*file["family"], file["name"]])
            answers[key]
            if row["result"].lower() in ("sat", "unsat"):
                answers[key].add(row["result"].lower())
    assert all(len(values) <= 1 for values in answers.values())
    inventories = {
        "QF_UF": json.loads(args.uf_inventory.read_text()),
        "QF_UFLIA": json.loads(args.uflia_inventory.read_text()),
    }
    cases = []
    for (logic, file), values in sorted(answers.items()):
        data = inventories[logic]["inputs"][file]
        cases.append(
            {
                "logic": logic,
                "file": file,
                "sha256": data["sha256"],
                "bytes": data["bytes"],
                "expected": next(iter(values), "unknown"),
                "family": file.split("/", 1)[0],
            }
        )
    counts = dict(collections.Counter(c["logic"] for c in cases))
    assert counts == {"QF_UF": 1104, "QF_UFLIA": 300}
    args.out.mkdir(parents=True, exist_ok=False)
    selection = {
        "description": "All official SMT-COMP 2026 SingleQuery QF_UF/QF_UFLIA selected names, replayed on original inputs without competition scrambling; not competition-machine reproduction.",
        "source_url": "https://raw.githubusercontent.com/SMT-COMP/smt-comp.github.io/master/data/results-sq-2026.json.gz",
        "source_sha256": OFFICIAL_SHA,
        "expected": "Consensus of decisive archived official answers; unknown when none. No new reference solver execution.",
        "counts": counts,
        "cases": cases,
    }
    write(args.out / "selection.json", selection)
    sources = json.loads(args.sources.read_text())
    profiles = {name: sources[name] for name in ("BASE", "OFF")}
    assert all(p["args"] == ["--quiet", "--stats"] for p in profiles.values())
    public = {
        name: {k: v for k, v in p.items() if k != "solver"}
        for name, p in profiles.items()
    }
    protocol = {
        "timeout_seconds": 120,
        "address_space_bytes": 8589934592,
        "repetitions": 3,
        "chunk_size": 702,
        "cases": len(cases),
        "native_rows": len(cases) * 6,
        "sources": public,
        "selection_sha256": sha(args.out / "selection.json"),
        "runner_sha256": sha(Path("scripts/qualification/lia_hybrid/run.py")),
        "configure_sha256": sha(Path(__file__)),
        "model_stage": "All successful SAT rows exact-option replay/check-model/direct Dolmen after global timing audit; native180s/Dolmen60s. No model time in speed rows.",
    }
    write(args.out / "protocol.json", protocol)
    roots = {logic: inv["root"] for logic, inv in inventories.items()}
    for chunk in range(2):
        batch = cases[702 * chunk : 702 * (chunk + 1)]
        assert len(batch) == 702
        for rep in range(3):
            write(
                args.out / f"chunk-{chunk}-{rep}.json",
                {
                    "roots": roots,
                    "profiles": profiles,
                    "paired_groups": [["BASE", "OFF"]],
                    "cases": [dict(c, repetition=rep) for c in batch],
                    "timeout": 120,
                    "address_space_bytes": 8589934592,
                    "out": f"{args.results}/rep-{rep}/chunk-{chunk}",
                },
            )
        write(
            args.out / f"campaign-{chunk}.json",
            {
                "configs": args.remote_configs,
                "mapping": [
                    {"cohort": f"chunk-{chunk}", "slot": i} for i in range(702)
                ],
            },
        )
    write(args.out / "routing.json", {"profiles": profiles, "roots": roots})
    print(json.dumps({"counts": counts, "native_rows": protocol["native_rows"]}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("official", "uf-inventory", "uflia-inventory", "sources", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("results", "remote-configs"):
        parser.add_argument("--" + name, required=True)
    configure(parser.parse_args())


if __name__ == "__main__":
    main()
