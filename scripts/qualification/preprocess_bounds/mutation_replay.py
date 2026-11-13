#!/usr/bin/env python3
"""Replay 100 deterministically selected real-seed mutants per integer logic."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path

ROOT = next(
    p
    for p in Path(__file__).resolve().parents
    if (p / "scripts/bench_qf_uf_full.py").is_file()
)
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--mutants", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    logic = "QF_LIA" if args.slot < 4 else "QF_UFLIA"
    groups = defaultdict(list)
    for path in (args.mutants / logic / "scratch").glob("*.smt2"):
        groups[path.name.split("-", 1)[0]].append(path)
    queues = {key: deque(sorted(paths)) for key, paths in groups.items()}
    selected = []
    while len(selected) < 100:
        progress = False
        for key in sorted(queues):
            if queues[key] and len(selected) < 100:
                selected.append(queues[key].popleft())
                progress = True
        assert progress, "fewer than100saved real-seed mutants"
    selected = selected[args.slot % 4 :: 4]
    assert len(selected) == 25
    out = args.out / f"mutation-replay-{args.slot}"
    out.mkdir(parents=True, exist_ok=False)
    sat = []
    flags = ["--quiet", "--preprocess-bound-tightening", "--check-model"]
    with (out / "rows.jsonl").open("w") as stream:
        for path in selected:
            ours = run_solver(["build-slurm-rel/primo", *flags], path, 10)
            reference = run_solver(
                ["contrib/local/bin/z3", "model_validate=true"], path, 10
            )
            assert ours["status"] in ("ok", "timeout"), ours
            assert reference["status"] in ("ok", "timeout"), reference
            if (
                ours["status"] == reference["status"] == "ok"
                and ours["answer"] in ("sat", "unsat")
                and reference["answer"] in ("sat", "unsat")
            ):
                assert ours["answer"] == reference["answer"], path
            row = {
                "file": path.name,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "ours": {k: ours[k] for k in ("status", "answer", "exit_code")},
                "reference": {
                    k: reference[k] for k in ("status", "answer", "exit_code")
                },
            }
            stream.write(json.dumps(row) + "\n")
            stream.flush()
            if ours["status"] == "ok" and ours["answer"] == "sat":
                sat.append(path)
    manifest = out / "sat.txt"
    manifest.write_text("".join(str(p) + "\n" for p in sat))
    if sat:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={arg}" for arg in flags),
                "--manifest",
                str(manifest),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out / "models"),
            ],
            check=True,
        )
    (out / "coverage.json").write_text(
        json.dumps(
            {
                "logic": logic,
                "cases": 25,
                "selected_sat": len(sat),
                "direct_dolmen_complete": True,
                "job": os.environ["SLURM_JOB_ID"],
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
