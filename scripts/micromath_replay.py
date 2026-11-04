#!/usr/bin/env python3
"""Replay saved Primo certificates with an external Micromath build in SLURM."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import resource
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from micromath_stress import FILE_SIZE_LIMIT_BYTES, invoke


def digest(path: Path) -> str:
    state = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            state.update(block)
    return state.hexdigest()


def log_text(path: Path) -> str:
    with path.open("r", errors="replace") as stream:
        return stream.read(65536)


def trace_info(cnf: Path, lrat: Path) -> dict:
    with cnf.open() as stream:
        variables = int(
            next(line for line in stream if line.startswith("p ")).split()[2]
        )
    largest = 0
    negative_hints = 0
    with lrat.open() as stream:
        for line in stream:
            tokens = line.split()
            if not tokens or tokens[0] == "c":
                continue
            if len(tokens) < 2:
                raise ValueError("invalid LRAT row")
            if tokens[1] == "d":
                continue
            end = tokens.index("0", 1)
            largest = max(largest, max((abs(int(x)) for x in tokens[1:end]), default=0))
            negative_hints += any(int(x) < 0 for x in tokens[end + 1 :])
    return {
        "dimacs_variables": variables,
        "largest_lrat_variable": largest,
        "introduces_variables": largest > variables,
        "additions_with_negative_hints": negative_hints,
    }


def replay(args: argparse.Namespace, source: Path) -> dict:
    resource.setrlimit(resource.RLIMIT_AS, (3 * 1024**3, 3 * 1024**3))
    resource.setrlimit(
        resource.RLIMIT_FSIZE, (FILE_SIZE_LIMIT_BYTES, FILE_SIZE_LIMIT_BYTES)
    )
    relative = source.parent.relative_to(args.inputs)
    dest = args.out / "cases" / relative
    dest.mkdir(parents=True)
    row = {
        "case": str(relative),
        "input_sha256": digest(source),
        "status": "not_started",
        "stages": {},
    }
    challenge = (
        source.with_name("challenge.umd") if args.reuse_lrat else dest / "challenge.umd"
    )
    cnf = source.with_name("problem.cnf") if args.reuse_lrat else dest / "problem.cnf"
    lrat = (
        source.with_name("problem.lrat") if args.reuse_lrat else dest / "problem.lrat"
    )
    proof = dest / "solution.ump"
    commands = [
        ("conflicts", [str(args.micromath / "smt/check-conflicts"), str(source)], 15)
    ]
    if not args.reuse_lrat:
        commands += [
            (
                "challenge",
                [
                    str(args.micromath / "smt/build-challenge"),
                    str(args.out / "axioms.umd"),
                    str(source),
                    str(challenge),
                ],
                15,
            ),
            (
                "cnf",
                [
                    str(args.micromath / "smt/prove-cnf"),
                    str(source),
                    str(cnf),
                    str(lrat),
                    "--cadical",
                    str(args.cadical),
                ],
                30,
            ),
        ]
    commands += [
        (
            "proof",
            [
                str(args.micromath / "smt/prove-lrat"),
                str(challenge),
                str(source),
                str(lrat),
                str(proof),
                "--raw",
            ],
            90,
        ),
        (
            "kernel",
            [str(args.micromath / "check-solution"), str(challenge), str(proof)],
            45,
        ),
    ]
    deadline = time.monotonic() + 180
    try:
        for name, command, limit in commands:
            stage = invoke(
                command,
                args.micromath,
                dest / name,
                min(limit, deadline - time.monotonic()),
            )
            row["stages"][name] = stage
            if stage["state"] != "ok":
                message = log_text(Path(stage["stderr"]))
                row["status"] = name + "_" + stage["state"]
                if name == "proof" and any(
                    term in message.lower()
                    for term in (
                        "snapshot variables",
                        "extension variables",
                        "rat hints",
                    )
                ):
                    row["status"] = "unsupported_trace"
                row["error"] = message[-4000:]
                break
            if name == "kernel":
                lines = log_text(Path(stage["stdout"])).splitlines()
                row["status"] = (
                    "kernel_proven"
                    if "challenge proven" in lines
                    and "Inconsistency proven!" not in lines
                    else "kernel_unconfirmed"
                )
        if cnf.exists() and lrat.exists():
            row["trace"] = trace_info(cnf, lrat)
        row["source_files"] = {
            name: {"sha256": digest(path), "bytes": path.stat().st_size}
            for name, path in (
                ("input", source),
                ("challenge", challenge),
                ("lrat", lrat),
            )
            if path.exists()
        }
        if row["input_sha256"] != digest(source):
            row["status"] = "input_changed"
        row["outputs"] = {
            path.name: {"sha256": digest(path), "bytes": path.stat().st_size}
            for path in dest.iterdir()
            if path.suffix in (".ump", ".umd", ".cnf", ".lrat")
        }
        row["removed_success_artifacts"] = []
        if (
            row["status"] == "kernel_proven"
            and args.keep_success_artifacts == "certificates"
        ):
            for name in row["outputs"]:
                (dest / name).unlink()
                row["removed_success_artifacts"].append(name)
    except (OSError, ValueError, StopIteration) as error:
        row["status"] = "workflow_error"
        row["error"] = repr(error)
    (dest / "result.json").write_text(json.dumps(row, indent=2) + "\n")
    return row


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("micromath", "inputs", "out", "cadical"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--workers", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--reuse-lrat", action="store_true")
    parser.add_argument(
        "--keep-success-artifacts", choices=("all", "certificates"), default="all"
    )
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("checker batches require a SLURM allocation")
    for name in ("micromath", "inputs", "out", "cadical"):
        setattr(args, name, getattr(args, name).resolve())
    sources = sorted(args.inputs.rglob("input.primo"))
    if not sources:
        parser.error("no saved input.primo certificates found")
    if args.reuse_lrat:
        for source in sources:
            for name in ("challenge.umd", "problem.cnf", "problem.lrat"):
                if not source.with_name(name).is_file():
                    parser.error(f"missing saved file: {source.with_name(name)}")
    args.out.mkdir(parents=True, exist_ok=False)
    provenance = {
        "micromath_git": args.micromath_git,
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "python": platform.python_version(),
        "command": sys.argv,
        "workflow_sha256": digest(Path(__file__)),
        "invoke_helper_sha256": digest(Path(__file__).with_name("micromath_stress.py")),
        "reuse_lrat": args.reuse_lrat,
        "workers": args.workers,
        "input_count": len(sources),
        "worker_address_space_gib": 3,
        "worker_file_limit_mib": FILE_SIZE_LIMIT_BYTES // 1024**2,
        "proof_export_mode": "raw",
        "keep_success_artifacts": args.keep_success_artifacts,
        "pipeline_seconds": 180,
        "stage_limits_seconds": {
            "conflicts": 15,
            "challenge": 15,
            "cnf": 30,
            "proof": 90,
            "kernel": 45,
        },
        "termination_grace_seconds": 2,
        "binary_sha256": {
            str(path): digest(path)
            for path in (
                args.cadical,
                args.micromath / "smt/prove-cnf",
                args.micromath / "smt/prove-lrat",
                args.micromath / "check-solution",
            )
        },
    }
    (args.out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if not args.reuse_lrat:
        stage = invoke(
            [
                str(args.micromath / "build-axioms"),
                "--fragment",
                "equality",
                str(args.out / "axioms.umd"),
            ],
            args.micromath,
            args.out / "axioms",
            15,
        )
        if stage["state"] != "ok":
            raise RuntimeError("axiom generation failed; see axioms logs")
    rows = []
    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(replay, args, source): source for source in sources}
        for future in as_completed(futures):
            try:
                rows.append(future.result())
            except (RuntimeError, OSError, MemoryError) as error:
                rows.append(
                    {
                        "case": str(futures[future].parent.relative_to(args.inputs)),
                        "status": "worker_error",
                        "error": repr(error),
                        "stages": {},
                    }
                )
            counts = dict(Counter(row["status"] for row in rows))
            summary = {
                "completed": len(rows),
                "expected": len(sources),
                "counts": counts,
                "all_kernel_proven": counts.get("kernel_proven", 0) == len(sources),
                "rows": sorted(rows, key=lambda row: row["case"]),
            }
            temporary = args.out / "summary.tmp"
            temporary.write_text(json.dumps(summary, indent=2) + "\n")
            temporary.replace(args.out / "summary.json")
            print(
                json.dumps({k: v for k, v in summary.items() if k != "rows"}),
                flush=True,
            )
            del futures[future]
    if not summary["all_kernel_proven"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
