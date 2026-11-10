#!/usr/bin/env python3
"""Bounded real-QF_UF certificate checks; run only inside a SLURM allocation."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import resource
import signal
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

MODES = ("none", "core", "mus")
FILE_SIZE_LIMIT_BYTES = 1024**3


def digest(path: Path) -> str:
    state = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            state.update(block)
    return state.hexdigest()


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
        except BaseException:
            # Interrupting a caller must also stop its independent child session.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
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


def write_json(path: Path, value: dict) -> None:
    """Publish a complete checkpoint without exposing a partially written JSON file."""
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def select(args: argparse.Namespace) -> dict:
    selection = json.loads(args.manifest.read_text())
    for index, case in enumerate(selection["cases"]):
        if case.get("expected") != "unsat":
            raise ValueError("certificate checks accept UNSAT benchmark manifests only")
        seconds = case.get("historical_seconds", float("inf"))
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError("select recorded native UNSAT before exporting")
        path = Path(case["file"])
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("unsafe benchmark manifest path")
        if index % args.shard_count != args.shard_index:
            continue
        if digest(args.corpus / path) != case["sha256"]:
            raise ValueError(f"benchmark hash mismatch: {path}")
    return selection


def run_case(args: argparse.Namespace, index: int, case: dict) -> dict:
    # Each worker and its children share explicit per-process resource ceilings.
    resource.setrlimit(resource.RLIMIT_AS, (3 * 1024**3, 3 * 1024**3))
    resource.setrlimit(
        resource.RLIMIT_FSIZE, (FILE_SIZE_LIMIT_BYTES, FILE_SIZE_LIMIT_BYTES)
    )
    directory = args.out / "cases" / f"{index:03d}"
    directory.mkdir(parents=True)
    source = args.corpus / case["file"]
    result = {"index": index, "case": case, "profiles": []}
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
    baseline_export_failed = False
    for mode in args.modes:
        if baseline_export_failed:
            result["profiles"].append(
                {"mode": mode, "stages": {}, "status": "skipped_baseline_export_failed"}
            )
            continue
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
                args.admission_timeout if mode == "none" else args.export_timeout,
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
                    "--raw",
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
            if stage == "export":
                profile["statistics"] = dict(
                    line.split(maxsplit=1)
                    for line in error.splitlines()
                    if line.startswith(":export.") and len(line.split()) == 2
                )
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
        if mode == "none" and ("answer" not in profile or profile["answer"] != "unsat"):
            baseline_export_failed = True
        profile["files"] = {
            p.name: {"bytes": p.stat().st_size, "sha256": digest(p)}
            for p in trial.iterdir()
            if p.suffix in {".primo", ".lrat", ".ump", ".umd", ".cnf"}
        }
        profile["removed_success_artifacts"] = []
        if (
            profile["status"] == "kernel_proven"
            and args.keep_success_artifacts == "certificates"
        ):
            for name in profile["files"]:
                if name != "input.primo":
                    (trial / name).unlink()
                    profile["removed_success_artifacts"].append(name)
        write_json(trial / "result.json", profile)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("corpus", "micromath", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    for name in ("primo", "reference", "cadical", "expected-git"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4, choices=range(1, 5))
    parser.add_argument("--reference-timeout", type=float, default=20)
    parser.add_argument("--mode-budget", type=float, default=60)
    parser.add_argument("--admission-timeout", type=float, default=5)
    parser.add_argument("--export-timeout", type=float, default=30)
    parser.add_argument("--modes", nargs="+", choices=MODES, default=list(MODES))
    parser.add_argument(
        "--keep-success-artifacts",
        choices=("all", "certificates"),
        default="all",
        help="discard large intermediates only after kernel success",
    )
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument(
        "--compact-summary",
        action="store_true",
        help="keep full rows in case files/rows.jsonl, not summary.json",
    )
    args = parser.parse_args()
    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        parser.error("require shard-count>=1 and 0<=shard-index<shard-count")
    if len(set(args.modes)) != len(args.modes):
        parser.error("repeated modes are not allowed")
    if "none" in args.modes and args.modes[0] != "none":
        parser.error("none admission mode must be first when requested")
    if any(
        not math.isfinite(x) or x <= 0
        for x in (
            args.reference_timeout,
            args.mode_budget,
            args.admission_timeout,
            args.export_timeout,
        )
    ):
        parser.error("timeouts must be finite and positive")
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
        "modes": args.modes,
        "shard_count": args.shard_count,
        "shard_index": args.shard_index,
        "compact_summary": args.compact_summary,
        "keep_success_artifacts": args.keep_success_artifacts,
        "reference_timeout_seconds": args.reference_timeout,
        "mode_budget_seconds": args.mode_budget,
        "phase_limits_seconds": {
            "baseline_export": args.admission_timeout,
            "reduced_export": args.export_timeout,
            "conflicts": 10,
            "challenge": 10,
            "cnf": 15,
            "proof": 30,
            "kernel": 10,
        },
        "as_limit_bytes_per_worker_and_child": 3 * 1024**3,
        "file_size_limit_bytes": FILE_SIZE_LIMIT_BYTES,
        "proof_export_mode": "raw",
        "binary_sha256": {
            name: digest(Path(path))
            for name, path in {
                "primo": args.primo,
                "reference": args.reference,
                "cadical": args.cadical,
                "check-solution": str(args.micromath / "check-solution"),
                "build-axioms": str(args.micromath / "build-axioms"),
                "check-conflicts": str(args.micromath / "smt/check-conflicts"),
                "build-challenge": str(args.micromath / "smt/build-challenge"),
                "prove-cnf": str(args.micromath / "smt/prove-cnf"),
                "prove-lrat": str(args.micromath / "smt/prove-lrat"),
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
    assigned = [
        (index, case)
        for index, case in enumerate(selection["cases"])
        if index % args.shard_count == args.shard_index
    ]
    rows = []
    counts: Counter[str] = Counter()
    references: Counter[str] = Counter()
    families: dict[str, Counter[str]] = {
        case["family"]: Counter() for _, case in assigned
    }
    completed = case_errors = 0

    def checkpoint() -> None:
        expected = len(assigned) * len(args.modes)
        summary = {
            "cases_completed": completed,
            "cases_selected": len(assigned),
            "manifest_cases": len(selection["cases"]),
            "shard_index": args.shard_index,
            "shard_count": args.shard_count,
            "modes": args.modes,
            "case_errors": case_errors,
            "profile_counts": dict(counts),
            "reference_counts": dict(references),
            "profiles_expected": expected,
            "all_profiles_proven": completed == len(assigned)
            and counts["kernel_proven"] == expected
            and case_errors == 0,
            "families": {name: dict(value) for name, value in sorted(families.items())},
        }
        if not args.compact_summary:
            summary["rows"] = rows
        write_json(args.out / "summary.json", summary)
        print(
            json.dumps(
                {k: v for k, v in summary.items() if k not in {"rows", "families"}}
            ),
            flush=True,
        )

    checkpoint()
    with (
        (args.out / "rows.jsonl").open("a") as journal,
        ProcessPoolExecutor(max_workers=args.workers) as pool,
    ):
        futures = {
            pool.submit(run_case, args, index, case): (index, case)
            for index, case in assigned
        }
        for future in as_completed(futures):
            index, case = futures.pop(future)
            try:
                row = future.result()
            except (OSError, RuntimeError, ValueError, MemoryError) as error:
                row = {
                    "index": index,
                    "case": case,
                    "error": repr(error),
                    "profiles": [],
                }
            directory = args.out / "cases" / f"{index:03d}"
            directory.mkdir(parents=True, exist_ok=True)
            write_json(directory / "result.json", row)
            journal.write(json.dumps(row) + "\n")
            journal.flush()
            os.fsync(journal.fileno())
            completed += 1
            case_errors += "error" in row
            statuses = [p["status"] for p in row["profiles"]]
            counts.update(statuses)
            families[case["family"]].update(statuses)
            if "reference" in row:
                references.update(
                    [
                        row["reference"]["state"]
                        + ":"
                        + (row["reference_answer"] or "no-answer")
                    ]
                )
            if not args.compact_summary:
                rows.append(row)
            checkpoint()


if __name__ == "__main__":
    main()
