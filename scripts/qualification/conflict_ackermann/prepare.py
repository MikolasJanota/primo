#!/usr/bin/env python3
"""Freeze an enriched UF/mixed pilot and prepare immutable-runner metadata."""

from __future__ import annotations

import argparse
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path

LOGICS = ("QF_UF", "QF_UFLIA")
FLAGS = {
    "OFF": ["--quiet", "--stats"],
    "DIAG": ["--quiet", "--stats", "--euf-conflict-ackermann-diagnostics"],
    "ON": ["--quiet", "--stats", "--euf-conflict-ackermann"],
}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def round_robin(groups, n):
    result = []
    while len(result) < n:
        previous = len(result)
        for family in sorted(groups):
            if groups[family] and len(result) < n:
                result.append(groups[family].pop(0))
        assert len(result) > previous, "insufficient eligible inputs"
    return result


def choose(cases, priority, mandatory=()):
    chosen = list(mandatory)
    used = {c["file"] for c in chosen}
    groups = collections.defaultdict(list)
    for c in cases:
        if c["file"] not in used and c["file"] in priority:
            groups[c["file"].split("/")[0]].append(c)
    for group in groups.values():
        group.sort(key=lambda c: (-priority[c["file"]], c["file"]))
    chosen.extend(round_robin(groups, 20 - len(chosen)))
    used = {c["file"] for c in chosen}
    groups = collections.defaultdict(list)
    for c in cases:
        if c["file"] not in used:
            groups[c["file"].split("/")[0]].append(c)
    for group in groups.values():
        group.sort(
            key=lambda c: hashlib.sha256(
                ("20261117:" + c["sha256"]).encode()
            ).hexdigest()
        )
    chosen.extend(round_robin(groups, 40 - len(chosen)))
    return chosen


def plan(a):
    source = Path("benchmarks/uniform120-20261109/report/timing.csv.gz")
    by = collections.defaultdict(dict)
    with gzip.open(source, "rt") as f:
        for r in csv.DictReader(f):
            if r["logic"] in LOGICS:
                by[r["logic"], r["file"]][r["profile"]] = r
    candidates = {logic: [] for logic in LOGICS}
    gaps = {}
    mutations = {logic: [] for logic in LOGICS}
    for (logic, name), profiles in by.items():
        b = profiles["primo"]
        if b["wrong"] == "true":
            continue
        case = {
            "logic": logic,
            "file": name,
            "sha256": b["sha256"],
            "expected": b["expected"],
        }
        if logic == "QF_UF" and b["status"] in ("ok", "timeout"):
            candidates[logic].append(case)
            refs = [
                float(r["seconds"])
                for k, r in profiles.items()
                if k != "primo"
                and r["status"] == "ok"
                and r["answer"] in ("sat", "unsat")
                and r["exit_code"] == "0"
            ]
            if (
                refs
                and min(refs) <= 5
                and (b["status"] == "timeout" or float(b["seconds"]) >= 1)
            ):
                gaps[name] = (
                    240 if b["status"] == "timeout" else float(b["seconds"])
                ) / max(min(refs), 0.001)
        if (
            b["status"] == "ok"
            and b["answer"] in ("sat", "unsat")
            and float(b["seconds"]) <= 2
        ):
            mutations[logic].append({**case, "baseline_seconds": float(b["seconds"])})
    parent = json.loads(
        Path("benchmarks/bound-propagation-20261113/selection.json").read_text()
    )
    candidates["QF_UFLIA"] = [c for c in parent["cases"] if c["logic"] == "QF_UFLIA"]
    activity = {}
    activity_receipts = {}
    for p in a.parent_rows.glob("*/rows.jsonl"):
        for line in p.read_text().splitlines():
            r = json.loads(line)
            if r["logic"] == "QF_UFLIA" and r["profile"] == "B":
                n = int(r["stats"].get(":euf.incremental.explain-congruence-edges", 0))
                if n:
                    activity[r["file"]] = n
                    activity_receipts[r["file"]] = sha(p)
    chosen = choose(candidates["QF_UF"], gaps) + choose(
        candidates["QF_UFLIA"], activity
    )
    selection = {
        "scope": "Target-enriched40inputs/logic, not representative or global tuning.",
        "method": "20family-round-robin priority inputs then20hash-ordered family-round-robin fill; family=first path component.",
        "UF_priority": "Historical native/reference gap with any reference<=5s and native>=1s/timeout; sourceuniform120 archive.",
        "UFLIA_priority": "Printed congruence-explanation edge counts from qualified combinations B rep0; includes explanation work, not necessarily conflict-only.",
        "historical_uniform_sha256": sha(source),
        "cases": chosen,
        "prior_activity": {
            c["file"]: {
                "edges": activity[c["file"]],
                "rows_sha256": activity_receipts[c["file"]],
            }
            for c in chosen
            if c["logic"] == "QF_UFLIA" and c["file"] in activity
        },
    }
    a.out.mkdir(parents=True, exist_ok=False)
    (a.out / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    tasks = [
        {"logic": logic, "label": p, "args": flags}
        for logic in LOGICS
        for p, flags in FLAGS.items()
    ]
    protocol = {
        "scope": "Bounded opt-in existing-atom prototype; no default or global tuning decision.",
        "baseline_source": "4f02a8b7 (production-equivalent to pre-change fca39be2)",
        "baseline_sha256": "08948789d787e2a81e76ad671738f29ac192dde34b4c9276789dff55b03a44ff",
        "candidate_source": "13230b6d",
        "profiles": FLAGS,
        "tasks": tasks,
        "cases": 80,
        "repetitions": 3,
        "timeout": 30,
        "address_space_bytes": 8589934592,
        "native_rows": 960,
        "generated_count_per_task": 1000,
        "generated_seed_base": 2026121700,
        "mutation_seed_base": 2026121800,
        "gate_model_limits_seconds": [30, 30],
        "timing_model_limits_seconds": [180, 60],
        "selection_sha256": sha(a.out / "selection.json"),
    }
    (a.out / "protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")
    # Public input identities only. Remote roots/argv/configs are private run metadata.
    (a.out / "mutation-candidates.json").write_text(
        json.dumps(mutations, indent=2) + "\n"
    )
    regression = []
    paths = set(Path("benchmarks/seeds/qf_uf").glob("*.smt2")) | set(
        Path("benchmarks/smoke/qf_uf").glob("*.smt2")
    )
    paths.add(Path("benchmarks/smoke/uf-conflict-ackermann-scopes.smt2"))
    for p in sorted(paths):
        regression.append({"logic": "QF_UF", "file": str(p), "sha256": sha(p)})
    regression.extend(
        c
        for c in json.loads(
            Path(
                "benchmarks/bound-propagation-20261113/regression-seeds.json"
            ).read_text()
        )["cases"]
        if c["logic"] == "QF_UFLIA"
    )
    p = Path("benchmarks/smoke/uflia-conflict-ackermann.smt2")
    regression.append({"logic": "QF_UFLIA", "file": str(p), "sha256": sha(p)})
    (a.out / "regression-seeds.json").write_text(
        json.dumps({"cases": regression}, indent=2) + "\n"
    )
    print(
        json.dumps(
            {
                "cases": len(chosen),
                "native_rows": 960,
                "gate_tasks": 6,
                "regression_inputs": len(regression),
            }
        )
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--parent-rows", type=Path, required=True)
    plan(p.parse_args())


if __name__ == "__main__":
    main()
