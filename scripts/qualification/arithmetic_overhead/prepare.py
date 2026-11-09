#!/usr/bin/env python3
"""Prepare private paired-run manifests; performs metadata I/O only, no solver runs."""

import argparse
import hashlib
import json
import os
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--config", type=Path, required=True)
parser.add_argument("--stage", required=True)
args = parser.parse_args()
spec = json.loads(args.config.read_text())
base = Path(spec["workspace"])
stage = args.stage


def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


roots = {k: Path(v) for k, v in spec["roots"].items()}
roots = {key: path.resolve() for key, path in roots.items()}
root = Path(os.path.commonpath(list(roots.values())))
# The diagnostic and development selections carry frozen content identities.
# Reject drift before creating any timing manifests; do not replace old hashes.
frozen_cases = json.loads(
    Path("benchmarks/arithmetic-efficiency-20261108/experiment.json").read_text()
)["cases"]
assert [(c["logic"], c["file"]) for c in spec["diagnostics"]] == [
    (c["logic"], c["file"]) for c in frozen_cases
]
for case, frozen in zip(spec["diagnostics"], frozen_cases):
    assert digest(roots[case["logic"]] / case["file"]) == frozen["sha256"]
for case in spec["lia_selection"]:
    assert digest(roots["QF_LIA"] / case["file"]) == case["sha256"]
flags = {
    "default": [],
    "m124": [
        "--lia-round-integer-bounds",
        "--lia-primitive-int-rows",
        "--no-lia-integer-slacks",
        "--lia-patch-integral-moves",
    ],
    "uniform": ["--lia-cube=uniform"],
    "m124-uniform": [
        "--lia-round-integer-bounds",
        "--lia-primitive-int-rows",
        "--no-lia-integer-slacks",
        "--lia-patch-integral-moves",
        "--lia-cube=uniform",
    ],
}


def entry(logic, name):
    path = (roots[logic] / name).resolve()
    return {
        "file": str(path.relative_to(root)),
        "logical_file": logic + "/" + name,
        "logic": logic,
        "family": name.split("/")[0],
        "sha256": digest(path),
    }


def profiles(integer):
    return [
        {
            "name": arm + "-" + name,
            "binary": arm,
            "command": [spec[arm], "--quiet", "--stats", *args],
            "binary_sha256": digest(spec[arm]),
        }
        for name, args in flags.items()
        if integer or name == "default"
        for arm in (
            ["baseline", "control", "candidate"]
            if "control" in spec
            else ["baseline", "candidate"]
        )
    ]


suites = {
    "diagnostic-integer": (
        [
            entry(e["logic"], e["file"])
            for e in spec["diagnostics"]
            if e["logic"] in {"QF_LIA", "QF_UFLIA"}
        ],
        3,
        True,
        8,
    ),
    "diagnostic-real": (
        [
            entry(e["logic"], e["file"])
            for e in spec["diagnostics"]
            if e["logic"] in {"QF_LRA", "QF_UFLRA"}
        ],
        3,
        False,
        4,
    ),
    "lia-development": (
        [entry("QF_LIA", e["file"]) for e in spec["lia_selection"]],
        1,
        True,
        100,
    ),
    "lra-selection60": (
        [
            entry("QF_LRA", name)
            for name in spec["lra_manifest"]
            if name and not name.startswith("#")
        ],
        1,
        False,
        64,
    ),
}
summary = {}
for name, (inputs, repetitions, integer, shards) in suites.items():
    config = {
        "run_id": "2026-11-09-arithmetic-overhead-" + stage + "-" + name,
        "timeout": 60,
        "repetitions": repetitions,
        "inputs": inputs,
        "profiles": profiles(integer),
    }
    if repetitions == 1:
        config["profiles"].append(
            {
                "name": "z3",
                "binary": "z3",
                "command": [spec["z3"]],
                "binary_sha256": digest(spec["z3"]),
            }
        )
    path = base / "tmp" / ("overhead-" + stage + "-" + name + ".json")
    path.write_text(json.dumps(config, indent=2) + "\n")
    summary[name] = {
        "shards": shards,
        "root": str(root),
        "config": str(path),
        "files": len(inputs),
        "repetitions": repetitions,
    }
# Select family-diverse small real seeds, never generated fixtures. Their fresh
# original-answer gate runs before mutations, with hashes retained in each task.
mutations = {}
for logic, key, names in [
    (
        "lia",
        "QF_LIA",
        [e["file"] for e in spec["lia_selection"] if e["bucket"] == "easy-controls"],
    ),
    ("lra", "QF_LRA", spec["lra_manifest"]),
    ("uflia", "QF_UFLIA", spec["uflia_manifest"]),
    ("uflra", "QF_UFLRA", spec["uflra_manifest"]),
]:
    names = [s.split("#", 1)[0].strip() for s in names]
    names = [
        s
        for s in names
        if s
        and (roots[key] / s).is_file()
        and (roots[key] / s).stat().st_size <= 200000
    ]
    names.sort(key=lambda s: ((roots[key] / s).stat().st_size, s))
    chosen = []
    families = set()
    for name in names:
        family = (
            "/".join(name.split("/")[:2])
            if key == "QF_UFLIA" and name.startswith("mathsat/")
            else name.split("/")[0]
        )
        if family in families:
            continue
        chosen.append(
            {
                "path": str(roots[key] / name),
                "file": name,
                "logic": key,
                "sha256": digest(roots[key] / name),
            }
        )
        families.add(family)
        if len(chosen) == 5:
            break

    for name in names:
        if len(chosen) == 5:
            break
        if any(e["file"] == name for e in chosen):
            continue
        chosen.append(
            {
                "path": str(roots[key] / name),
                "file": name,
                "logic": key,
                "sha256": digest(roots[key] / name),
            }
        )
    assert len(chosen) == 5, (logic, len(chosen))
    mutations[logic] = chosen
(base / "tmp/overhead-mutations.json").write_text(
    json.dumps(mutations, indent=2) + "\n"
)
(base / "tmp" / ("overhead-" + stage + "-suites.json")).write_text(
    json.dumps(summary, indent=2) + "\n"
)
print(
    json.dumps(
        {
            k: {x: v[x] for x in ["files", "repetitions", "shards"]}
            for k, v in summary.items()
        }
    )
)
