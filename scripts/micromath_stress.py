#!/usr/bin/env python3
"""Bounded real-QF_UF certificate pilot; run only inside a SLURM allocation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import resource
import signal
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

FAMILIES = [
    "NEQ",
    "PEQ",
    "QG-classification",
    "TypeSafe",
    "20190906-CLEARSY",
    "2018-Goel-hwbench",
]
MODES = ("none", "core", "mus")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command: list[str], cwd: Path, log: Path, timeout: float) -> dict:
    started = time.monotonic()
    record = {
        "command": command,
        "stdout": str(log.with_suffix(".out")),
        "stderr": str(log.with_suffix(".err")),
    }
    if timeout <= 0:
        return {**record, "state": "timeout", "seconds": 0, "exit_code": None}
    with (
        log.with_suffix(".out").open("wb") as stdout,
        log.with_suffix(".err").open("wb") as stderr,
    ):
        process = subprocess.Popen(
            command, cwd=cwd, stdout=stdout, stderr=stderr, start_new_session=True
        )
        state = "ok"
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            state = "timeout"
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            except ProcessLookupError:
                process.wait()
        if state == "ok" and process.returncode != 0:
            state = "error"
    return {
        **record,
        "state": state,
        "seconds": time.monotonic() - started,
        "exit_code": process.returncode,
    }


def contents(path: str) -> str:
    return Path(path).read_text(errors="replace") if Path(path).exists() else ""


def answer(record: dict) -> str:
    answers = [
        line.strip()
        for line in contents(record["stdout"]).splitlines()
        if line.strip() in {"sat", "unsat", "unknown", "unsupported"}
    ]
    return answers[-1] if answers else ""


def select(args: argparse.Namespace) -> dict:
    if args.manifest:
        selection = json.loads(args.manifest.read_text())
        for case in selection["cases"]:
            if case.get("expected") != "unsat":
                raise ValueError("this pilot accepts UNSAT benchmark manifests only")
            path = Path(case["file"])
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("unsafe benchmark manifest path")
            if digest(args.corpus / path) != case["sha256"]:
                raise ValueError(f"benchmark hash mismatch: {path}")
        return selection
    cases = []
    candidate_counts = {}
    for family in FAMILIES:
        eligible = []
        for path in (args.corpus / family).rglob("*.smt2"):
            size = path.stat().st_size
            if size > 1024 * 1024:
                continue
            with path.open("rb") as source:
                header = source.read(65536)
            if re.search(rb"(?m)^\s*\(set-info\s+:status\s+unsat\s*\)", header):
                eligible.append((size, path.relative_to(args.corpus).as_posix()))
        eligible.sort()
        candidate_counts[family] = len(eligible)
        indices = (
            {int((len(eligible) - 1) * q) for q in (0.25, 0.75)} if eligible else set()
        )
        for index in sorted(indices):
            size, name = eligible[index]
            cases.append(
                {
                    "file": name,
                    "family": family,
                    "bytes": size,
                    "sha256": digest(args.corpus / name),
                    "expected": "unsat",
                }
            )
    if not cases:
        raise ValueError("no eligible UNSAT benchmarks found")
    return {
        "selection": "two size-rank quantiles (25%,75%) per family, metadata UNSAT, <=1MiB; status in first64KiB",
        "candidate_counts": candidate_counts,
        "cases": cases,
    }


def run_case(args: argparse.Namespace, index: int, case: dict) -> dict:
    # Each worker and its children share explicit per-process resource ceilings.
    resource.setrlimit(resource.RLIMIT_AS, (3 * 1024**3, 3 * 1024**3))
    resource.setrlimit(resource.RLIMIT_FSIZE, (256 * 1024**2, 256 * 1024**2))
    directory = args.out / "cases" / f"{index:03d}"
    directory.mkdir(parents=True)
    source = args.corpus / case["file"]
    result = {"case": case, "profiles": []}
    if digest(source) != case["sha256"]:
        return {**result, "error": "benchmark hash changed"}
    reference = invoke(
        [args.reference, "-smt2", str(source)],
        args.micromath,
        directory / "reference",
        args.reference_timeout,
    )
    result["reference"] = reference
    result["reference_answer"] = answer(reference) if reference["state"] == "ok" else ""
    if result["reference_answer"] == "sat":
        return {**result, "error": "reference disagrees with UNSAT metadata"}
    for mode in MODES:
        trial = directory / mode
        trial.mkdir()
        profile = {"mode": mode, "stages": {}, "status": "not_started"}
        result["profiles"].append(profile)
        deadline = time.monotonic() + args.mode_budget
        exported = trial / "input.primo"
        commands = [
            (
                "export",
                [
                    args.primo,
                    "--quiet",
                    "--stats",
                    "--experimental-export-erase-sorts",
                    f"--export-cnf-reduction={mode}",
                    "--export-cnf",
                    str(exported),
                    str(source),
                ],
                30,
            ),
            (
                "conflicts",
                [str(args.micromath / "smt/check-conflicts"), str(exported)],
                10,
            ),
            (
                "challenge",
                [
                    str(args.micromath / "smt/build-challenge"),
                    str(args.out / "axioms.umd"),
                    str(exported),
                    str(trial / "challenge.umd"),
                ],
                10,
            ),
            (
                "cnf",
                [
                    str(args.micromath / "smt/prove-cnf"),
                    str(exported),
                    str(trial / "problem.cnf"),
                    str(trial / "problem.lrat"),
                    "--cadical",
                    args.cadical,
                ],
                15,
            ),
            (
                "proof",
                [
                    str(args.micromath / "smt/prove-lrat"),
                    str(trial / "challenge.umd"),
                    str(exported),
                    str(trial / "problem.lrat"),
                    str(trial / "solution.ump"),
                ],
                30,
            ),
            (
                "kernel",
                [
                    str(args.micromath / "check-solution"),
                    str(trial / "challenge.umd"),
                    str(trial / "solution.ump"),
                ],
                10,
            ),
        ]
        for stage, command, stage_limit in commands:
            record = invoke(
                command,
                args.micromath,
                trial / stage,
                min(stage_limit, deadline - time.monotonic()),
            )
            profile["stages"][stage] = record
            error = contents(record["stderr"])
            if record["state"] != "ok":
                if record["state"] == "timeout":
                    profile["status"] = f"{stage}_timeout"
                elif record["exit_code"] == -signal.SIGXFSZ:
                    profile["status"] = f"{stage}_file_limit"
                elif record["exit_code"] is not None and record["exit_code"] < 0:
                    profile["status"] = f"{stage}_crash"
                elif stage == "export" and any(
                    word in error.lower()
                    for word in ("unsupported", "not support", "supports", "requires")
                ):
                    profile["status"] = "unsupported_export"
                elif (
                    stage == "conflicts"
                    and "is consistent under congruence closure" in error
                ):
                    profile["status"] = "invalid_conflict"
                elif stage == "conflicts":
                    profile["status"] = "checker_rejected"
                elif stage == "cnf" and record["exit_code"] == 2:
                    profile["status"] = "incomplete_certificate"
                else:
                    profile["status"] = f"{stage}_error"
                break
            if stage == "export":
                actual = answer(record)
                profile["answer"] = actual
                profile["statistics"] = dict(
                    line.split(maxsplit=1)
                    for line in error.splitlines()
                    if line.startswith(":export.") and len(line.split()) == 2
                )
                if actual != "unsat":
                    profile["status"] = (
                        "answer_disagreement" if actual == "sat" else "export_unknown"
                    )
                    break
            if stage == "kernel":
                profile["status"] = (
                    "kernel_proven"
                    if "challenge proven" in contents(record["stdout"]).splitlines()
                    else "kernel_unconfirmed"
                )
        profile["files"] = {
            p.name: {"bytes": p.stat().st_size, "sha256": digest(p)}
            for p in trial.iterdir()
            if p.suffix in {".primo", ".lrat", ".ump"}
        }
        (trial / "result.json").write_text(json.dumps(profile, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("corpus", "micromath", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    for name in ("primo", "reference", "cadical", "expected-git"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--workers", type=int, default=4, choices=range(1, 5))
    parser.add_argument("--reference-timeout", type=float, default=20)
    parser.add_argument("--mode-budget", type=float, default=60)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("solver batches require a SLURM allocation")
    for name in ("corpus", "micromath", "out"):
        setattr(args, name, getattr(args, name).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    version = invoke(
        [args.primo, "--version"], args.micromath, args.out / "primo-version", 10
    )
    if version["state"] != "ok" or f"git: {args.expected_git}\n" not in contents(
        version["stdout"]
    ):
        raise ValueError("unexpected Primo binary revision")
    selection = select(args)
    (args.out / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    provenance = {
        "primo_git": args.expected_git,
        "micromath_git": os.environ.get("MICROMATH_GIT"),
        "cadical_git": os.environ.get("CADICAL_GIT"),
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "python": platform.python_version(),
        "workers": args.workers,
        "reference_timeout_seconds": args.reference_timeout,
        "mode_budget_seconds": args.mode_budget,
        "phase_limits_seconds": {
            "export": 30,
            "conflicts": 10,
            "challenge": 10,
            "cnf": 15,
            "proof": 30,
            "kernel": 10,
        },
        "as_limit_bytes_per_worker_and_child": 3 * 1024**3,
        "file_size_limit_bytes": 256 * 1024**2,
        "binary_sha256": {
            name: digest(Path(path))
            for name, path in {
                "primo": args.primo,
                "reference": args.reference,
                "cadical": args.cadical,
                "check-solution": str(args.micromath / "check-solution"),
            }.items()
        },
        "harness_sha256": digest(Path(__file__)),
        "command": sys.argv,
    }
    (args.out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    axioms = invoke(
        [
            str(args.micromath / "build-axioms"),
            "--fragment",
            "equality",
            str(args.out / "axioms.umd"),
        ],
        args.micromath,
        args.out / "axioms",
        10,
    )
    if axioms["state"] != "ok":
        raise RuntimeError("axiom preparation failed")
    rows = []
    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        futures = {
            pool.submit(run_case, args, index, case): case
            for index, case in enumerate(selection["cases"])
        }
        for future in as_completed(futures):
            try:
                rows.append(future.result())
            except (OSError, RuntimeError, ValueError, MemoryError) as error:
                rows.append(
                    {"case": futures[future], "error": repr(error), "profiles": []}
                )
            counts = Counter(p["status"] for row in rows for p in row["profiles"])
            summary = {
                "cases_completed": len(rows),
                "cases_selected": len(selection["cases"]),
                "case_errors": sum("error" in row for row in rows),
                "profile_counts": dict(counts),
                "profiles_expected": len(selection["cases"]) * len(MODES),
                "all_profiles_proven": counts["kernel_proven"]
                == len(selection["cases"]) * len(MODES)
                and not any("error" in row for row in rows),
                "families": {
                    family: dict(
                        Counter(
                            p["status"]
                            for row in rows
                            if row["case"]["family"] == family
                            for p in row["profiles"]
                        )
                    )
                    for family in sorted(
                        {case["family"] for case in selection["cases"]}
                    )
                },
                "rows": rows,
            }
            (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
            print(
                json.dumps(
                    {
                        key: value
                        for key, value in summary.items()
                        if key not in {"rows", "families"}
                    }
                ),
                flush=True,
            )


if __name__ == "__main__":
    main()
