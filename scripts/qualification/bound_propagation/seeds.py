#!/usr/bin/env python3
"""Check every tracked arithmetic regression seed, preserving query sequences."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("rows", "both"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("benchmarks/bound-propagation-20261113/regression-seeds.json"),
    )
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    manifest = json.loads(args.manifest.read_text())
    protocol = json.loads(
        Path("benchmarks/bound-propagation-20261113/protocol.json").read_text()
    )
    flags = [*protocol["profiles"][args.profile], "--check-model"]
    args.out.mkdir(parents=True, exist_ok=False)
    scoped = []
    single_sat = []
    with (args.out / "rows.jsonl").open("w") as stream:
        for index, case in enumerate(manifest["cases"]):
            source = Path(case["file"])
            assert hashlib.sha256(source.read_bytes()).hexdigest() == case["sha256"]
            results = {}
            reference_command = (
                ["cvc5", "--lang=smt2", "--incremental", "--produce-models"]
                if case.get("reference") == "cvc5"
                else ["contrib/local/bin/z3"]
            )
            for label, command in [
                ("native", ["build-slurm-rel/primo", *flags]),
                ("reference", reference_command),
            ]:
                result = subprocess.run(
                    [*command, str(source)],
                    capture_output=True,
                    text=True,
                    timeout=120,
                    check=False,
                )
                for kind in ("stdout", "stderr"):
                    (args.out / f"{index:03d}-{label}.{kind}").write_text(
                        getattr(result, kind)
                    )
                answers = [
                    line.strip()
                    for line in result.stdout.splitlines()
                    if line.strip() in ("sat", "unsat", "unknown")
                ]
                results[label] = {"exit_code": result.returncode, "answers": answers}
            receipt = dict(case, results=results)
            stream.write(json.dumps(receipt) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
            assert all(r["exit_code"] == 0 for r in results.values()), receipt
            assert (
                results["native"]["answers"]
                and "unknown" not in results["native"]["answers"]
            ), receipt
            assert results["native"]["answers"] == results["reference"]["answers"], (
                receipt
            )
            if len(results["native"]["answers"]) > 1:
                scoped.append(str(source))
            elif results["native"]["answers"] == ["sat"]:
                single_sat.append(str(source))
    if single_sat:
        selected = args.out / "sat.txt"
        selected.write_text("".join(f"{p}\n" for p in single_sat))
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={f}" for f in flags),
                "--manifest",
                str(selected),
                "--expect-sat",
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "120",
                "--dolmen-timeout",
                "30",
                "--out",
                str(args.out / "models"),
            ],
            check=True,
        )
    if scoped:
        subprocess.run(
            [
                sys.executable,
                "scripts/validate_incremental.py",
                "--solver",
                "build-slurm-rel/primo",
                *(f"--solver-arg={f}" for f in flags),
                "--ref",
                "contrib/local/bin/z3",
                "--dolmen",
                "contrib/local/bin/dolmen",
                "--timeout",
                "120",
                "--dolmen-timeout",
                "30",
                "--out",
                str(args.out / "scoped-models"),
                *scoped,
            ],
            check=True,
        )
    (args.out / "complete.json").write_text(
        json.dumps(
            {
                "profile": args.profile,
                "cases": len(manifest["cases"]),
                "scoped_sessions": len(scoped),
                "single_query_sat_models": len(single_sat),
                "runner_sha256": hashlib.sha256(
                    Path(__file__).read_bytes()
                ).hexdigest(),
                "manifest_sha256": hashlib.sha256(
                    args.manifest.read_bytes()
                ).hexdigest(),
                "job": os.environ["SLURM_JOB_ID"],
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
