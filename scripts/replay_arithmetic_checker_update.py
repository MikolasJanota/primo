#!/usr/bin/env python3
"""Replay independently audited arithmetic feedback against a pinned checker update."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import time
from collections import Counter
from pathlib import Path

from capture_micromath_lra import KERNEL, capture
from micromath_stress import digest, invoke, write_json
from replay_micromath_lra import PIPE


def tail(path, limit=32768):
    if not path.exists():
        return ""
    with path.open("rb") as stream:
        stream.seek(max(0, path.stat().st_size - limit))
        return stream.read(limit).decode(errors="replace")


def pipeline_result(stage, directory, exits_name):
    codes = [int(x) for x in tail(directory / exits_name).split()]
    messages = tail(directory / "kernel.out").splitlines()
    if stage["state"] == "timeout":
        return "proof_timeout", codes
    accepted = (
        stage["state"] == "ok"
        and codes == [0, 0]
        and "challenge proven" in messages
        and "Inconsistency proven!" not in messages
    )
    return ("verified" if accepted else "proof_error"), codes


def bounded(command, cwd, log, seconds):
    return invoke(
        ["prlimit", "--as=3221225472", "--fsize=268435456", "--", *map(str, command)],
        cwd,
        log,
        seconds,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logic", choices=("LRA", "LIA"), required=True)
    for name in ("audit", "micromath", "cadical", "out", "build-report"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("checker update qualification belongs in SLURM")
    for name in ("audit", "micromath", "cadical", "out", "build_report"):
        setattr(args, name, getattr(args, name).resolve())
    actual = subprocess.check_output(
        ["git", "-C", str(args.micromath), "rev-parse", "HEAD"], text=True
    ).strip()
    dirty = subprocess.check_output(
        [
            "git",
            "-C",
            str(args.micromath),
            "status",
            "--porcelain",
            "--untracked-files=no",
        ],
        text=True,
    )
    if actual != args.micromath_git or dirty:
        raise ValueError("checker revision changed or tracked source is dirty")
    expected = {}
    for line in (args.build_report / "binaries.sha256").read_text().splitlines():
        sha, name = line.split(maxsplit=1)
        expected[name.strip()] = sha
    for name, sha in expected.items():
        if digest(args.micromath / name) != sha:
            raise ValueError("checker binary differs from qualified build report")
    audit_path = args.audit / "summary.json"
    audit = json.loads(audit_path.read_text())
    if not audit.get("finished"):
        raise ValueError("audit incomplete")
    trusted = {"independent_checks_passed", "checker_error_reproduced"}
    cases = audit["rows"]
    if len(cases) != (3 if args.logic == "LRA" else 5) or any(
        c["status"] not in trusted for c in cases
    ):
        raise ValueError("expected complete independently validated feedback set")
    expected_cadical = (
        audit["cadical_sha256"]
        if args.logic == "LRA"
        else audit["binary_sha256"]["cadical"]
    )
    if digest(args.cadical) != expected_cadical:
        raise ValueError("reference CaDiCaL changed")
    args.out.mkdir(parents=True, exist_ok=False)
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "logic": args.logic,
        "micromath_git": actual,
        "binary_sha256": expected,
        "cadical_sha256": digest(args.cadical),
        "audit_job": audit["job_id"],
        "audit_summary_sha256": digest(audit_path),
        "build_report": str(args.build_report),
        "source_to_cnf_verified": False,
        "limits": {
            "challenge_seconds": 30,
            "live_pipeline_seconds": 60,
            "capture_seconds": 60,
            "captured_kernel_seconds": 60,
            "process_address_bytes": 3221225472,
            "process_file_bytes": 268435456,
        },
        "workflow_sha256": {
            name: digest(Path(__file__).parent / name)
            for name in (
                "replay_arithmetic_checker_update.py",
                "capture_micromath_lra.py",
                "micromath_stress.py",
                "replay_micromath_lra.py",
                "check_lra_certificate.py",
            )
        },
        "rows": [],
        "finished": False,
    }

    def checkpoint():
        report["counts"] = dict(Counter(r["status"] for r in report["rows"]))
        write_json(args.out / "summary.json", report)

    axioms = args.out / "axioms.umd"
    report["axioms"] = bounded(
        [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
        args.micromath,
        args.out / "axioms",
        30,
    )
    checkpoint()
    if report["axioms"]["state"] != "ok":
        raise SystemExit(1)
    for previous in cases:
        name = previous["name"] if args.logic == "LRA" else f"{previous['index']:03}"
        source = args.audit / name
        directory = args.out / name
        directory.mkdir()
        for filename, meta in previous["files"].items():
            if digest(source / filename) != meta["sha256"]:
                raise ValueError("saved input changed")
        certificate = source / (
            "native.sexp" if args.logic == "LRA" else "micromath-v2.sexp"
        )
        row = {
            "name": name,
            "case": previous["file"]
            if args.logic == "LRA"
            else previous["case"]["file"],
            "previous_status": previous["previous_status"],
            "source": str(source),
            "payloads": previous["files"],
            "stages": {},
            "status": "not_started",
        }
        started = time.monotonic()
        challenge = directory / "challenge.umd"
        stage = bounded(
            [
                args.micromath / f"smt/build-{args.logic.lower()}-challenge",
                axioms,
                source / "exported-cnf.smt2",
                challenge,
            ],
            args.micromath,
            directory / "challenge",
            30,
        )
        row["stages"]["challenge"] = stage
        if stage["state"] != "ok":
            row["status"] = "challenge_" + stage["state"]
        else:
            row["challenge_bytes"] = challenge.stat().st_size
            row["challenge_sha256"] = digest(challenge)
            pipe = directory / "pipe"
            pipe.mkdir()
            stage = invoke(
                [
                    "bash",
                    "-c",
                    PIPE,
                    "checker-update-pipe",
                    str(args.micromath / f"smt/prove-{args.logic.lower()}"),
                    str(challenge),
                    str(certificate),
                    str(args.cadical),
                    str(args.micromath / "check-solution"),
                    str(pipe),
                    "",
                ],
                args.micromath,
                directory / "live-pipeline",
                60,
            )
            row["stages"]["live_pipeline"] = stage
            row["status"], row["pipeline_exit_codes"] = pipeline_result(
                stage, pipe, "exit-status.txt"
            )
            row["producer_diagnostic"] = tail(pipe / "producer.err")
            row["kernel_result"] = tail(pipe / "kernel.out")
            row["kernel_steps"] = {
                m[1]: int(m[2])
                for m in re.finditer(
                    r"^(.+): (\d+) kernel steps",
                    row["producer_diagnostic"],
                    re.MULTILINE,
                )
            }
            row["live_replay_seconds"] = time.monotonic() - started
            if args.logic == "LRA" and row["status"] == "verified":
                saved = directory / "capture"
                saved.mkdir()
                command = [
                    "prlimit",
                    "--as=3221225472",
                    "--fsize=268435456",
                    "--",
                    str(args.micromath / "smt/prove-lra"),
                    str(challenge),
                    str(certificate),
                    "-",
                    "--raw",
                    "--cadical",
                    str(args.cadical),
                ]
                row["capture"] = capture(command, args.micromath, saved, 60)
                if row["capture"]["complete"]:
                    replay = invoke(
                        [
                            "bash",
                            "-c",
                            KERNEL,
                            "updated-capture-replay",
                            str(saved / row["capture"]["gzip_file"]),
                            str(args.micromath / "check-solution"),
                            str(challenge),
                            str(saved),
                        ],
                        args.micromath,
                        saved / "kernel-pipeline",
                        60,
                    )
                    row["captured_kernel"] = replay
                    row["captured_kernel_status"], row["captured_kernel_exits"] = (
                        pipeline_result(replay, saved, "kernel-exits.txt")
                    )
        report["rows"].append(row)
        checkpoint()
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
