#!/usr/bin/env python3
"""Replay the complete saved checker-failure index, without native solving.

The exported-CNF boundary and strict raw-pipe acceptance are unchanged from
replay_checker_handoff.py. Optional LRA profiles use the separately qualified
coefficient adapter; they are bounded comparisons, not solver changes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import signal
import subprocess
import sys
from collections import Counter
from pathlib import Path

from micromath_stress import digest, invoke, write_json
from replay_checker_handoff import CADICAL_SHA256, pipeline_status, tail
from replay_micromath_lra import PIPE

Z3_SHA256 = "54bae839dd54e262edac6f755fc99659ce2a121301faff20a3e3b94478dcead0"


def assignment(rows, shards):
    """Deterministic greedy scheduling; keep each case's profiles on one worker."""
    buckets, loads = [[] for _ in range(shards)], [0] * shards
    for index, row in sorted(
        enumerate(rows),
        key=lambda item: (
            -estimated_seconds(item[1]),
            item[1]["logic"],
            item[1]["file"],
        ),
    ):
        worker = min(range(shards), key=lambda i: (loads[i], i))
        buckets[worker].append(index)
        loads[worker] += estimated_seconds(row)
    return buckets


def profiles(row):
    if row["logic"] == "QF_LRA" and row["file"] != (
        "clock_synchro/clocksynchro_7clocks.worst_case_skew.induct.smt2"
    ):
        return ("original", "cheap", "optimized")
    return ("original",)


def estimated_seconds(row):
    return 180 + (
        2 * (120 + row["native_bytes"] / 100000) if len(profiles(row)) > 1 else 0
    )


def checked_copy(native, adapted):
    """Verify the saved LIA copy differs only at header byte 28, in bounded RAM."""
    checksum = hashlib.sha256()
    with native.open("rb") as left, adapted.open("rb") as right:
        offset = 0
        while True:
            original, copied = left.read(1024**2), right.read(1024**2)
            if not original and not copied:
                break
            expected = original
            if offset == 0:
                if not original.startswith(
                    b"(primo-conflicts\n  (version 3)\n  (logic QF_LIA)\n"
                ):
                    raise ValueError("unexpected native LIA header")
                expected = original[:28] + b"2" + original[29:]
            if copied != expected:
                raise ValueError("saved LIA compatibility copy changed its body")
            checksum.update(copied)
            offset += len(original)
    return checksum.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in (
        "selection",
        "saved",
        "micromath",
        "build-report",
        "cadical",
        "z3",
        "out",
    ):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--shard", type=int, required=True)
    parser.add_argument("--shards", type=int, default=8)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID") or not 0 <= args.shard < args.shards:
        parser.error("requires SLURM and a valid shard")
    for flag in (
        "selection",
        "saved",
        "micromath",
        "build_report",
        "cadical",
        "z3",
        "out",
    ):
        setattr(args, flag, getattr(args, flag).resolve())
    scripts = Path(__file__).resolve().parent
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
    if (
        actual != args.micromath_git
        or dirty
        or not (args.build_report / "FINISHED").exists()
    ):
        raise ValueError("checker revision/build differs from qualified build")
    binaries = {}
    for line in (args.build_report / "binaries.sha256").read_text().splitlines():
        checksum, name = line.split(maxsplit=1)
        if digest(args.micromath / name) != checksum:
            raise ValueError("qualified checker binary changed")
        binaries[name] = checksum
    if len(binaries) != 6 or digest(args.cadical) != CADICAL_SHA256:
        raise ValueError("expected six checker binaries and pinned CaDiCaL")
    if digest(args.z3) != Z3_SHA256:
        raise ValueError("coefficient optimization executable changed")
    selection = json.loads(args.selection.read_text())
    rows = selection["rows"]
    if len(rows) != 109 or len({(r["logic"], r["file"]) for r in rows}) != 109:
        raise ValueError("expected the complete 109-case index")
    if any(r["logic"] not in ("QF_LRA", "QF_LIA") for r in rows) or sum(
        len(profiles(r)) for r in rows
    ) != 139:
        raise ValueError("expected 109 original profiles plus 30 LRA comparisons")
    selected = assignment(rows, args.shards)[args.shard]
    args.out.mkdir(parents=True, exist_ok=False)
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "shard": args.shard,
        "shards": args.shards,
        "selected_indices": selected,
        "selection_sha256": digest(args.selection),
        "micromath_git": actual,
        "binary_sha256": binaries,
        "cadical_sha256": CADICAL_SHA256,
        "z3_sha256": digest(args.z3),
        "z3_version": subprocess.check_output(
            [str(args.z3), "--version"], text=True
        ).strip(),
        "workflow_sha256": {
            name: digest(scripts / name)
            for name in (
                Path(__file__).name,
                "minimize_lra_coefficients.py",
                "farkas_coefficients.py",
                "micromath_stress.py",
                "replay_checker_handoff.py",
                "replay_micromath_lra.py",
            )
        },
        "limits": {
            "axioms_seconds": 30,
            "challenge_seconds": 60,
            "proof_kernel_seconds": 120,
            "adapter_seconds": 600,
            "process_address_bytes": 3221225472,
            "process_file_bytes": 268435456,
            "optimizer_call_seconds": 1,
            "optimizer_total_seconds": 30,
            "optimizer_max_queries": 32,
        },
        "source_to_cnf_verified": False,
        "native_solver_rerun": False,
        "rows": [],
        "finished": False,
    }

    def checkpoint():
        report["counts"] = dict(Counter(row["status"] for row in report["rows"]))
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
            args.micromath,
            log,
            seconds,
        )

    def interrupted(_signal, _frame):
        raise InterruptedError("replay interrupted")

    for caught in (signal.SIGINT, signal.SIGTERM):
        signal.signal(caught, interrupted)
    checkpoint()
    axioms = args.out / "axioms.umd"
    report["axioms"] = bounded(
        [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
        args.out / "axioms",
        30,
    )
    checkpoint()
    if report["axioms"]["state"] != "ok":
        raise ValueError("axiom construction failed")
    for index in selected:
        previous = rows[index]
        directory = args.out / f"case-{index:03}"
        directory.mkdir()
        row = {
            "index": index,
            "case": previous,
            "status": "checking_inputs",
            "profiles": {},
        }
        report["rows"].append(row)
        checkpoint()
        try:
            relative = Path(previous["primary_case"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("unsafe primary input path")
            source = args.saved / relative
            native, statement = source / "native.sexp", source / "exported-cnf.smt2"
            for path, kind in ((native, "native"), (statement, "statement")):
                if (
                    path.stat().st_size != previous[kind + "_bytes"]
                    or digest(path) != previous[kind + "_sha256"]
                ):
                    raise ValueError(f"saved {kind} hash/size changed")
            certificate = native
            theory = "lra" if previous["logic"] == "QF_LRA" else "lia"
            if theory == "lia":
                certificate = source / "micromath-v2.sexp"
                row["compatibility_sha256"] = checked_copy(native, certificate)
            row["source"] = str(source)
            row["challenge"] = bounded(
                [
                    args.micromath / f"smt/build-{theory}-challenge",
                    axioms,
                    statement,
                    directory / "challenge.umd",
                ],
                directory / "challenge",
                60,
            )
            if row["challenge"]["state"] != "ok":
                row["status"] = "challenge_" + row["challenge"]["state"]
                checkpoint()
                continue
            row["status"] = "replaying"
            checkpoint()
            for profile in profiles(previous):
                target = directory / profile
                target.mkdir()
                result = {"status": "started"}
                row["profiles"][profile] = result
                checkpoint()
                proof_input = certificate
                if profile != "original":
                    proof_input = target / "simplified.sexp"
                    command = [
                        sys.executable,
                        scripts / "minimize_lra_coefficients.py",
                        native,
                        proof_input,
                        "--report",
                        target / "statistics.json",
                    ]
                    if profile == "optimized":
                        command += [
                            "--optimizer",
                            args.z3,
                            "--problems-dir",
                            target / "problems",
                        ]
                    result["adapter"] = bounded(command, target / "adapter", 600)
                    if result["adapter"]["state"] != "ok":
                        result["status"] = "adapter_" + result["adapter"]["state"]
                        checkpoint()
                        continue
                    result["statistics"] = json.loads(
                        (target / "statistics.json").read_text()
                    )
                result["certificate_sha256"] = digest(proof_input)
                result["certificate_bytes"] = proof_input.stat().st_size
                pipe = target / "pipe"
                pipe.mkdir()
                stage = invoke(
                    [
                        "bash",
                        "-c",
                        PIPE,
                        "remaining-checker-replay",
                        str(args.micromath / f"smt/prove-{theory}"),
                        str(directory / "challenge.umd"),
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
                result["pipeline"] = stage
                result["pipeline_exit_codes"] = [
                    int(x) for x in tail(pipe / "exit-status.txt").split()
                ]
                result["status"] = pipeline_status(
                    stage, result["pipeline_exit_codes"], tail(pipe / "kernel.out")
                )
                result["producer_diagnostic"] = tail(pipe / "producer.err")
                result["kernel_result"] = tail(pipe / "kernel.out")
                result["kernel_steps"] = {
                    m[1]: int(m[2])
                    for m in re.finditer(
                        r"^(.+): (\d+) kernel steps",
                        result["producer_diagnostic"],
                        re.MULTILINE,
                    )
                }
                checkpoint()
            row["status"] = row["profiles"]["original"]["status"]
        except InterruptedError:
            checkpoint()
            raise
        except (OSError, ValueError) as error:
            row["status"] = "input_or_workflow_error"
            row["error"] = str(error)
        checkpoint()
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
