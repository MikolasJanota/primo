#!/usr/bin/env python3
"""Profile saved CORE certificates with isolated gprof binaries, inside SLURM.

Proof streams remain pipes. GNU time measures each process (including waited-for
children); gprof measures its instrumented executable, without source annotation.
See docs/evidence/checker-gprof-20261111.md for scope and interpretation.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import signal
import subprocess
import sys
from pathlib import Path

from micromath_stress import digest, invoke, write_json
from replay_checker_handoff import CADICAL_SHA256, pipeline_status, tail
from replay_checker_medium import validate_selection
from replay_remaining_checker_failures import checked_copy

# GNU time's process elapsed time includes waiting on the streaming partner.
TIME_FORMAT = (
    "user_seconds=%U\nsystem_seconds=%S\nelapsed_seconds=%e\n"
    "max_rss_kib=%M\nvoluntary_switches=%w\ninvoluntary_switches=%c\n"
    "filesystem_inputs=%I\nfilesystem_outputs=%O\nexit_status=%x"
)
TIMED_PIPE = r"""
set -uo pipefail
export TIME="$8"
prlimit --as=3221225472 --fsize=268435456 -- \
  "$9" -o "$6/producer.time" \
  env GMON_OUT_PREFIX="$6/producer.gmon" \
  "$1" "$2" "$3" - --raw --cadical "$4" 2> "$6/producer.err" |
prlimit --as=3221225472 --fsize=268435456 -- \
  "$9" -o "$6/kernel.time" \
  env GMON_OUT_PREFIX="$6/kernel.gmon" \
  "$5" "$2" - > "$6/kernel.out" 2> "$6/kernel.err"
statuses=("${PIPESTATUS[@]}")
printf '%s %s\n' "${statuses[0]}" "${statuses[1]}" > "$6/exit-status.txt"
if (( statuses[0] != 0 || statuses[1] != 0 )); then exit 1; fi
grep -Fxq 'challenge proven' "$6/kernel.out"
! grep -Fxq 'Inconsistency proven!' "$6/kernel.out"
"""


def time_metrics(path):
    """Reject incomplete time reports rather than silently reporting zero cost."""
    values = {}
    for line in path.read_text().splitlines():
        key, separator, value = line.partition("=")
        if separator:
            number = float(value)
            if not math.isfinite(number) or number < 0:
                raise ValueError("invalid time metric")
            values[key] = number
    expected = {line.split("=")[0] for line in TIME_FORMAT.splitlines()}
    if set(values) != expected:
        raise ValueError("incomplete time report")
    return values


def verified_build(checker, report, revision):
    if (
        subprocess.check_output(
            ["git", "-C", str(checker), "rev-parse", "HEAD"], text=True
        ).strip()
        != revision
        or subprocess.check_output(
            [
                "git",
                "-C",
                str(checker),
                "status",
                "--porcelain",
                "--untracked-files=no",
            ],
            text=True,
        ).strip()
    ):
        raise ValueError("checker revision or tracked content changed")
    hashes = {}
    for line in (report / "binaries.sha256").read_text().splitlines():
        value, name = line.split(maxsplit=1)
        if Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError("unsafe binary manifest")
        if digest(checker / name) != value:
            raise ValueError("checker binary changed")
        hashes[name] = value
    expected = {
        "build-axioms",
        "smt/build-lra-challenge",
        "smt/build-lia-challenge",
        "smt/prove-lra",
        "smt/prove-lia",
        "check-solution",
    }
    if set(hashes) != expected:
        raise ValueError("incomplete binary manifest")
    return hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in (
        "selection",
        "saved",
        "checker",
        "build-report",
        "profile-checker",
        "profile-build-report",
        "cadical",
        "time-tool",
        "out",
    ):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--checker-git", required=True)
    parser.add_argument("--workflow-git", required=True)
    parser.add_argument("--index", type=int, required=True)
    args = parser.parse_args()
    selection = json.loads(args.selection.read_text())
    validate_selection(selection)
    if not os.environ.get("SLURM_JOB_ID") or not 0 <= args.index < len(
        selection["rows"]
    ):
        parser.error("valid case index and SLURM allocation required")
    for flag in (
        "selection",
        "saved",
        "checker",
        "build_report",
        "profile_checker",
        "profile_build_report",
        "cadical",
        "time_tool",
        "out",
    ):
        setattr(args, flag, getattr(args, flag).resolve())
    builds = {
        "baseline": verified_build(args.checker, args.build_report, args.checker_git),
        "gprof": verified_build(
            args.profile_checker, args.profile_build_report, args.checker_git
        ),
    }
    if args.checker == args.profile_checker or digest(args.cadical) != CADICAL_SHA256:
        raise ValueError(
            "profiling requires an isolated build and qualified SAT binary"
        )
    args.out.mkdir(parents=True, exist_ok=False)
    case = selection["rows"][args.index]
    report = {
        "workflow_git": args.workflow_git,
        "command": sys.argv,
        "workflow_sha256": {
            p.name: digest(p) for p in Path(__file__).parent.glob("*.py")
        },
        "job_id": os.environ["SLURM_JOB_ID"],
        "checker_git": args.checker_git,
        "checker_sha256": builds,
        "cadical_sha256": CADICAL_SHA256,
        "time_tool_sha256": digest(args.time_tool),
        "selection_sha256": digest(args.selection),
        "index": args.index,
        "case": case,
        "finished": False,
        "status": "started",
        "profiles": {},
    }

    def checkpoint():
        write_json(args.out / "summary.json", report)

    def interrupted(_signal, _frame):
        raise InterruptedError("profiling interrupted")

    for caught in (signal.SIGINT, signal.SIGTERM):
        signal.signal(caught, interrupted)
    checkpoint()
    try:
        source = args.saved / case["primary_case"]
        native, statement = source / "native.sexp", source / "exported-cnf.smt2"
        for path, key in (
            (native, "native"),
            (statement, "statement"),
            (source / "input.smt2", "input"),
        ):
            if digest(path) != case[key + "_sha256"]:
                raise ValueError("saved input changed")
        certificate = native
        if case["logic"] == "QF_LIA":
            certificate = source / "micromath-v2.sexp"
            report["compatibility_sha256"] = checked_copy(native, certificate)
        report["certificate_sha256"] = digest(certificate)
        theory = "lia" if case["logic"] == "QF_LIA" else "lra"
        axioms, challenge = args.out / "axioms.umd", args.out / "challenge.umd"
        for label, command, seconds in (
            (
                "axioms",
                [args.checker / "build-axioms", "--fragment", "finitary", axioms],
                30,
            ),
            (
                "challenge",
                [
                    args.checker / f"smt/build-{theory}-challenge",
                    axioms,
                    statement,
                    challenge,
                ],
                60,
            ),
        ):
            report[label] = invoke(
                [
                    "prlimit",
                    "--as=3221225472",
                    "--fsize=268435456",
                    "--",
                    *map(str, command),
                ],
                args.out,
                args.out / label,
                seconds,
            )
            checkpoint()
            if report[label]["state"] != "ok":
                raise ValueError(label + " construction failed")
        report["challenge_sha256"] = digest(challenge)
        for profile, checker, seconds in (
            ("baseline", args.checker, 120),
            ("gprof", args.profile_checker, 600),
        ):
            directory = args.out / profile
            directory.mkdir()
            result = {"status": "started"}
            report["profiles"][profile] = result
            checkpoint()
            result["pipeline"] = invoke(
                [
                    "bash",
                    "-c",
                    TIMED_PIPE,
                    "profile-checker",
                    str(checker / f"smt/prove-{theory}"),
                    str(challenge),
                    str(certificate),
                    str(args.cadical),
                    str(checker / "check-solution"),
                    str(directory),
                    "",
                    TIME_FORMAT,
                    str(args.time_tool),
                ],
                directory,
                directory / "pipeline",
                seconds,
            )
            codes = [int(x) for x in tail(directory / "exit-status.txt").split()]
            result["pipeline_exit_codes"] = codes
            result["kernel_result"] = tail(directory / "kernel.out")
            result["status"] = pipeline_status(
                result["pipeline"], codes, result["kernel_result"]
            )
            result["kernel_steps"] = {
                m[1]: int(m[2])
                for m in re.finditer(
                    r"^(.+): (\d+) kernel steps",
                    tail(directory / "producer.err"),
                    re.MULTILINE,
                )
            }
            checkpoint()
            if result["status"] != "verified":
                break
            result["process_time"] = {
                name: time_metrics(directory / f"{name}.time")
                for name in ("producer", "kernel")
            }
            if profile == "gprof":
                result["gprof"] = {}
                for name, executable in (
                    ("producer", checker / f"smt/prove-{theory}"),
                    ("kernel", checker / "check-solution"),
                ):
                    files = list(directory.glob(f"{name}.gmon.*"))
                    if len(files) != 1:
                        raise ValueError(
                            "expected one gmon file per instrumented executable"
                        )
                    result["gprof"][name] = invoke(
                        ["gprof", "-b", str(executable), str(files[0])],
                        directory,
                        directory / f"{name}-gprof",
                        30,
                    )
                    if result["gprof"][name]["state"] != "ok":
                        raise ValueError("gprof report failed")
            checkpoint()
        results = report["profiles"]
        report["status"] = (
            "profiled"
            if len(results) == 2
            and all(p["status"] == "verified" for p in results.values())
            and results["baseline"]["kernel_steps"] == results["gprof"]["kernel_steps"]
            else "profiling_incomplete"
        )
        report["finished"] = True
        checkpoint()
    except BaseException as error:
        report.update(status="workflow_error", error=str(error))
        checkpoint()
        raise
    if report["status"] != "profiled":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
