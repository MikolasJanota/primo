#!/usr/bin/env python3
"""Recheck saved positive arithmetic cases and collect optional coefficient stats.

No native solving or checker-internal inspection. See
docs/evidence/checker-medium-20261111.md for the selection and measurement boundary.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
from collections import Counter
from pathlib import Path

from micromath_stress import digest, invoke, write_json
from replay_checker_handoff import CADICAL_SHA256, pipeline_status, tail
from replay_micromath_lra import PIPE
from replay_remaining_checker_failures import checked_copy


def cost_band(seconds):
    return "short" if seconds < 10 else "medium" if seconds <= 90 else "near_limit"


def adapter_command(scripts, source, directory, profile, optimizer, min_bits):
    command = [
        sys.executable,
        str(scripts / "minimize_lra_coefficients.py"),
        str(source),
        str(directory / "native.sexp"),
        "--report",
        str(directory / "coefficients.json"),
    ]
    if profile in ("size", "support"):
        command += [
            "--objective",
            profile,
            "--optimizer",
            str(optimizer),
            "--min-bits",
            str(min_bits),
            "--problems-dir",
            str(directory / "problems"),
        ]
    elif profile != "cheap":
        raise ValueError("expected a transformed profile")
    return command


def integer_envelope(native, compatible):
    """Write the external v2 envelope in bounded memory; validate the complete body."""
    with native.open("rb") as source, compatible.open("xb") as target:
        header = source.read(64)
        if not header.startswith(b"(primo-conflicts\n  (version 3)\n  (logic QF_LIA)"):
            raise ValueError("unexpected native LIA header")
        target.write(header[:28] + b"2" + header[29:])
        shutil.copyfileobj(source, target, 1024**2)
    return checked_copy(native, compatible)


def compare_profiles(
    args,
    scripts,
    case,
    row,
    native,
    original,
    directory,
    challenge,
    bounded,
    checkpoint,
):
    """Each profile proves the same original challenge; no file-sized syntax tree."""
    row["profiles"] = {}
    for profile in args.profiles:
        target = directory / profile
        target.mkdir()
        result = {"status": "started", "profile": profile}
        row["profiles"][profile] = result
        checkpoint()
        proof_input = original
        if profile != "original":
            result["adapter"] = bounded(
                adapter_command(
                    scripts, native, target, profile, args.optimizer, args.min_bits
                ),
                target / "adapter",
                150,
            )
            if result["adapter"]["state"] != "ok":
                result["status"] = "adapter_" + result["adapter"]["state"]
                checkpoint()
                continue
            candidate = target / "native.sexp"
            stats = json.loads((target / "coefficients.json").read_text())
            if stats["input_sha256"] != case["native_sha256"] or stats[
                "output_sha256"
            ] != digest(candidate):
                raise ValueError("adapter input/output hash mismatch")
            result["coefficients"] = stats
            proof_input = candidate
            if case["logic"] == "QF_LIA":
                proof_input = target / "micromath-v2.sexp"
                result["compatibility_sha256"] = integer_envelope(
                    candidate, proof_input
                )
        result["certificate_sha256"] = digest(proof_input)
        result["certificate_bytes"] = proof_input.stat().st_size
        pipe = target / "pipe"
        pipe.mkdir()
        theory = "lia" if case["logic"] == "QF_LIA" else "lra"
        result["pipeline"] = invoke(
            [
                "bash",
                "-c",
                PIPE,
                "medium-coefficient-comparison",
                str(args.micromath / f"smt/prove-{theory}"),
                str(challenge),
                str(proof_input),
                str(args.cadical),
                str(args.micromath / "check-solution"),
                str(pipe),
                "",
            ],
            args.micromath,
            target / "pipeline",
            120,
        )
        result["pipeline_exit_codes"] = [
            int(x) for x in tail(pipe / "exit-status.txt").split()
        ]
        result["kernel_result"] = tail(pipe / "kernel.out")
        result["producer_diagnostic"] = tail(pipe / "producer.err")
        result["status"] = pipeline_status(
            result["pipeline"], result["pipeline_exit_codes"], result["kernel_result"]
        )
        result["kernel_steps"] = {
            m[1]: int(m[2])
            for m in re.finditer(
                r"^(.+): (\d+) kernel steps",
                result["producer_diagnostic"],
                re.MULTILINE,
            )
        }
        checkpoint()
        if profile == "original" and result["status"] != "verified":
            break
    results = row["profiles"]
    row["status"] = (
        "verified"
        if len(results) == len(args.profiles)
        and all(r["status"] == "verified" for r in results.values())
        else "comparison_incomplete"
    )
    checkpoint()


def validate_selection(selection):
    keys, locations = set(), set()
    for row in selection["rows"]:
        key = row["logic"], row["file"]
        relative = Path(row["primary_case"])
        if (
            key in keys
            or relative in locations
            or relative.is_absolute()
            or ".." in relative.parts
            or not relative.parts
            or relative.parts[0] != row["logic"]
            or row["logic"] not in ("QF_LRA", "QF_LIA")
            or row["previous"]["status"] != "verified"
            or row["previous"]["pipeline_exit_codes"] != [0, 0]
            or row["retained_lemmas"] <= 0
        ):
            raise ValueError("invalid or nonpositive saved-case selection")
        keys.add(key)
        locations.add(relative)
    if not keys:
        raise ValueError("empty case selection")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("selection", "saved", "micromath", "build-report", "cadical", "out"):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--workflow-git", required=True)
    parser.add_argument("--shard", type=int, required=True)
    parser.add_argument("--shards", type=int, default=4)
    parser.add_argument("--coefficient-statistics", action="store_true")
    parser.add_argument(
        "--profiles",
        nargs="+",
        choices=("original", "cheap", "size", "support"),
        default=["original"],
    )
    parser.add_argument("--optimizer", type=Path)
    parser.add_argument("--min-bits", type=int, default=8)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID") or not 0 <= args.shard < args.shards:
        parser.error("valid shard and SLURM allocation required")
    if args.profiles[0] != "original" or len(set(args.profiles)) != len(args.profiles):
        parser.error("profiles must start with original and contain no duplicates")
    if args.min_bits < 0 or (
        set(args.profiles) & {"size", "support"} and args.optimizer is None
    ):
        parser.error("nonnegative min-bits and optimizer for size/support required")
    if args.optimizer:
        args.optimizer = args.optimizer.resolve()
    for flag in ("selection", "saved", "micromath", "build_report", "cadical", "out"):
        setattr(args, flag, getattr(args, flag).resolve())
    selection = json.loads(args.selection.read_text())
    validate_selection(selection)
    if (
        subprocess.check_output(
            ["git", "-C", str(args.micromath), "rev-parse", "HEAD"], text=True
        ).strip()
        != args.micromath_git
    ):
        raise ValueError("checker revision changed")
    if subprocess.check_output(
        [
            "git",
            "-C",
            str(args.micromath),
            "status",
            "--porcelain",
            "--untracked-files=no",
        ],
        text=True,
    ).strip():
        raise ValueError("tracked checker checkout changed")
    hashes = {}
    for line in (args.build_report / "binaries.sha256").read_text().splitlines():
        value, name = line.split(maxsplit=1)
        if digest(args.micromath / name) != value:
            raise ValueError("checker executable changed")
        hashes[name] = value
    if len(hashes) != 6 or digest(args.cadical) != CADICAL_SHA256:
        raise ValueError("incomplete checker build or changed SAT executable")
    args.out.mkdir(parents=True, exist_ok=False)
    scripts = Path(__file__).resolve().parent
    report = {
        "workflow_git": args.workflow_git,
        "command": sys.argv,
        "job_id": os.environ["SLURM_JOB_ID"],
        "shard": args.shard,
        "finished": False,
        "selection_sha256": digest(args.selection),
        "checker_git": args.micromath_git,
        "checker_sha256": hashes,
        "cadical_sha256": CADICAL_SHA256,
        "workflow_sha256": {p.name: digest(p) for p in scripts.glob("*.py")},
        "rows": [],
    }
    if args.optimizer:
        report["optimizer_sha256"] = digest(args.optimizer)
        report["optimizer_version"] = subprocess.check_output(
            [str(args.optimizer), "--version"], text=True
        ).strip()

    def checkpoint():
        report["counts"] = dict(Counter(r["status"] for r in report["rows"]))
        write_json(args.out / "summary.json", report)

    def bounded(command, log, seconds):
        return invoke(
            [
                "prlimit",
                "--as=3221225472",
                "--fsize=268435456",
                "--",
                *map(str, command),
            ],
            args.out,
            log,
            seconds,
        )

    def interrupted(_signal, _frame):
        raise InterruptedError("discovery replay interrupted")

    for caught in (signal.SIGTERM, signal.SIGINT):
        signal.signal(caught, interrupted)
    checkpoint()
    axioms = args.out / "axioms.umd"
    report["axioms"] = bounded(
        [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
        args.out / "axioms",
        30,
    )
    if report["axioms"]["state"] != "ok":
        raise ValueError("axiom construction failed")
    for index, case in enumerate(selection["rows"]):
        if index % args.shards != args.shard:
            continue
        directory = args.out / f"case-{index:03d}"
        directory.mkdir()
        row = {"index": index, "case": case, "status": "checking_inputs"}
        report["rows"].append(row)
        checkpoint()
        try:
            source = args.saved / case["primary_case"]
            native, statement = source / "native.sexp", source / "exported-cnf.smt2"
            for path, prefix in ((native, "native"), (statement, "statement")):
                if (
                    path.stat().st_size != case[prefix + "_bytes"]
                    or digest(path) != case[prefix + "_sha256"]
                ):
                    raise ValueError("saved input bytes changed")
            theory = "lia" if case["logic"] == "QF_LIA" else "lra"
            certificate = native
            if theory == "lia":
                certificate = source / "micromath-v2.sexp"
                row["compatibility_sha256"] = checked_copy(native, certificate)
            challenge = directory / "challenge.umd"
            row["challenge"] = bounded(
                [
                    args.micromath / f"smt/build-{theory}-challenge",
                    axioms,
                    statement,
                    challenge,
                ],
                directory / "challenge",
                60,
            )
            if row["challenge"]["state"] != "ok":
                row["status"] = "challenge_" + row["challenge"]["state"]
                checkpoint()
                continue
            if args.profiles != ["original"]:
                compare_profiles(
                    args,
                    scripts,
                    case,
                    row,
                    native,
                    certificate,
                    directory,
                    challenge,
                    bounded,
                    checkpoint,
                )
                continue
            pipe = directory / "pipe"
            pipe.mkdir()
            row["pipeline"] = invoke(
                [
                    "bash",
                    "-c",
                    PIPE,
                    "medium-checker-replay",
                    str(args.micromath / f"smt/prove-{theory}"),
                    str(challenge),
                    str(certificate),
                    str(args.cadical),
                    str(args.micromath / "check-solution"),
                    str(pipe),
                    "",
                ],
                args.micromath,
                directory / "pipeline",
                120,
            )
            row["pipeline_exit_codes"] = [
                int(x) for x in tail(pipe / "exit-status.txt").split()
            ]
            row["kernel_result"] = tail(pipe / "kernel.out")
            row["producer_diagnostic"] = tail(pipe / "producer.err")
            row["status"] = pipeline_status(
                row["pipeline"], row["pipeline_exit_codes"], row["kernel_result"]
            )
            if row["status"] == "verified":
                row["cost_band"] = cost_band(row["pipeline"]["seconds"])
                row["kernel_steps"] = {
                    m[1]: int(m[2])
                    for m in re.finditer(
                        r"^(.+): (\d+) kernel steps",
                        row["producer_diagnostic"],
                        re.MULTILINE,
                    )
                }
                if args.coefficient_statistics:
                    stats = directory / "coefficients.json"
                    row["coefficient_scan"] = bounded(
                        [
                            sys.executable,
                            scripts / "minimize_lra_coefficients.py",
                            native,
                            directory / "cheap.sexp",
                            "--report",
                            stats,
                        ],
                        directory / "coefficient-scan",
                        180,
                    )
                    if row["coefficient_scan"]["state"] == "ok":
                        row["coefficients"] = json.loads(stats.read_text())
                    # This output is for metrics/future replay; only the original was kernel-checked.
            checkpoint()
        except InterruptedError:
            # Cancellation must stop the worker instead of becoming a recoverable I/O error.
            checkpoint()
            raise
        except (OSError, ValueError, KeyError) as error:
            row.update(status="input_or_workflow_error", error=str(error))
            checkpoint()
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
