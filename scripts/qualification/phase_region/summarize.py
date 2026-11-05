#!/usr/bin/env python3
"""Verify complete timing/model coverage and emit a compact phase-region report."""
from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

from analyze import describe, solved
from run import profile as expected_profile

PROFILES = ("native", "m124", "beta-plain", "observe", "force-positive", "random-16",
            "random-4", "gated-16", "gated-4", "least", "most")
SEEDS = (2026110531, 2026110532, 2026110533)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", type=Path, required=True)
    parser.add_argument("--models", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--expected-solver-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    selection = json.loads(args.selection.read_text())
    losses = set(selection["losses"])
    files = losses | set(selection["controls"])
    expected = {(f, p, s) for f in files for p in PROFILES for s in SEEDS}
    rows = {}
    hashes = {}
    identities = set()
    input_hashes = {}
    validated = set()
    for path in sorted(args.timing.glob("task-*/rows.jsonl")):
        task = path.parent.name
        hashes[f"timing/{task}/rows.jsonl"] = digest(path)
        provenance_path = path.parent / "provenance.json"
        hashes[f"timing/{task}/provenance.json"] = digest(provenance_path)
        provenance = json.loads(provenance_path.read_text())
        if provenance["seeds"] != list(SEEDS) or provenance["timeout"] != 15:
            raise ValueError(f"unexpected protocol: {task}")
        if (f"git: {args.expected_git}" not in provenance["version"].splitlines()
                or provenance["solver_sha256"] != args.expected_solver_sha256
                or provenance["runner_sha256"] != digest(Path(__file__).with_name("run.py"))):
            raise ValueError(f"unexpected qualified source/binary/runner: {task}")
        identities.add((provenance["solver_sha256"], provenance["version"],
                        provenance["runner_sha256"], provenance["manifest_sha256"]))
        for name, value in provenance["inputs"].items():
            if name in input_hashes and input_hashes[name] != value:
                raise ValueError(f"input hash mismatch: {name}")
            input_hashes[name] = value
        for line in path.read_text().splitlines():
            row = json.loads(line)
            key = (row["file"], row["profile"], row["seed"])
            if key in rows or row["seed"] != SEEDS[row["round"]]:
                raise ValueError(f"duplicate or inconsistent row: {key}")
            if row["wrong"] or row["status"] not in {"ok", "timeout"}:
                raise ValueError(f"blocking outcome: {key}")
            if row["status"] == "ok" and row["answer"] not in {"sat", "unsat"}:
                raise ValueError(f"unexpected answer: {key}")
            flags, env = expected_profile(row["profile"], row["seed"])
            command = row["command"]
            if (row["env"] != env or command[0] != provenance["solver"]
                    or command[1:-1] != flags or not command[-1].endswith("/" + row["file"])
                    or row["file"] not in provenance["inputs"]):
                raise ValueError(f"policy command/environment mismatch: {key}")
            rows[key] = row
        coverage_path = args.models / task / "coverage.json"
        hashes[f"models/{task}/coverage.json"] = digest(coverage_path)
        coverage = json.loads(coverage_path.read_text())
        if (coverage["status"] != "complete" or coverage["passed"] != coverage["expected"]
                or coverage["solver_sha256"] != provenance["solver_sha256"]
                or coverage["runner_sha256"] != digest(Path(__file__).with_name("replay.py"))):
            raise ValueError(f"incomplete model coverage: {task}")
        task_validated = set()
        for group in coverage["groups"]:
            if (group["exit_code"] != 0 or group["expected"] != group["direct_dolmen"]
                    or group["expected"] != len(group["files"])):
                raise ValueError(f"failed model group: {task}")
            for name in group["files"]:
                key = (name, group["profile"], SEEDS[group["round"]])
                if key in validated:
                    raise ValueError(f"duplicate model coverage: {key}")
                validated.add(key)
                task_validated.add(key)
        if len(task_validated) != coverage["expected"]:
            raise ValueError(f"model count mismatch: {task}")
    if set(rows) != expected or set(input_hashes) != files or len(identities) != 1:
        raise ValueError("incomplete timing matrix or inconsistent provenance")
    # Replay follows process success, even if wall time rounded just beyond the
    # reporting cutoff. The solved/PAR2 predicate below still enforces 15 seconds.
    sat = {key for key, row in rows.items() if row["status"] == "ok"
           and row["answer"] == "sat" and row["exit_code"] == 0}
    if validated != sat:
        raise ValueError("SAT timing rows and validated rows differ")
    solver_hash, version, runner_hash, manifest_hash = identities.pop()
    result = {"schema_version": 1, "rows": len(rows), "sat_models_direct_dolmen": len(sat),
              "timeout_seconds": 15, "seeds": SEEDS, "solver_sha256": solver_hash,
              "version": version, "runner_sha256": runner_hash,
              "manifest_sha256": manifest_hash, "input_sha256": input_hashes,
              "artifact_sha256": hashes, "profiles": {}}
    for profile in PROFILES:
        selected = [r for (f, p, s), r in rows.items() if p == profile]
        family_rows = defaultdict(list)
        for row in selected:
            family_rows[row["file"].split("/")[0]].append(row)
        summary = describe(selected, 15)
        summary["losses"] = describe([r for r in selected if r["file"] in losses], 15)
        summary["controls"] = describe([r for r in selected if r["file"] not in losses], 15)
        summary["families"] = {f: {"rows": len(rs),
                                   "solved": sum(solved(r, 15) for r in rs),
                                   "statuses": dict(Counter(r["status"] for r in rs))}
                               for f, rs in sorted(family_rows.items())}
        summary["solved_losses_by_seed"] = {
            str(seed): sorted(r["file"] for r in selected
                              if r["seed"] == seed and r["file"] in losses and solved(r, 15))
            for seed in SEEDS}
        summary["loss_rows_with_arithmetic_summary"] = sum(
            bool(r.get("arithmetic_probe")) for r in selected if r["file"] in losses)
        summary["loss_rows_with_flips"] = sum(
            any(c.get("flips", 0) for c in r.get("arithmetic_probe", []))
            for r in selected if r["file"] in losses)
        result["profiles"][profile] = summary
        print(profile, "losses", summary["losses"]["solved"], "/90; controls",
              summary["controls"]["solved"], "/42; PAR2", round(summary["par2_sum_seconds"], 2))
    args.out.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
