#!/usr/bin/env python3
"""Separate bounded raw-proof export from gzip-fed kernel replay in SLURM."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import platform
import resource
import selectors
import signal
import subprocess
import time
from pathlib import Path

from micromath_stress import digest, invoke, write_json

ADDRESS_BYTES = 3 * 1024**3
FILE_BYTES = 256 * 1024**2
RAW_BYTES = 2 * 1024**3


def kill_group(process):
    # The session is private to this stage, including any CaDiCaL descendants.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def capture(
    command, cwd, directory, seconds, raw_limit=RAW_BYTES, gzip_limit=FILE_BYTES
):
    """Stream to gzip; handled producer failures/deadlines/caps finalize a prefix."""
    path = directory / "proof.partial.umd.gz"
    started = time.monotonic()
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    sha = hashlib.sha256()
    total = lines = 0
    tail = b""
    state = "ok"
    process = None
    record = {"command": command, "seconds_limit": seconds}
    progress = directory / "export-progress.jsonl"
    with (
        path.open("wb") as output,
        gzip.GzipFile(
            filename="", fileobj=output, mode="wb", compresslevel=1, mtime=0
        ) as packed,
        (directory / "producer.err").open("wb") as errors,
        progress.open("w") as samples,
        selectors.DefaultSelector() as ready,
    ):
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=errors,
            start_new_session=True,
        )
        assert process.stdout is not None
        ready.register(process.stdout, selectors.EVENT_READ)
        next_sample = 0.0
        try:
            while True:
                elapsed = time.monotonic() - started
                if elapsed >= next_sample:
                    samples.write(
                        json.dumps(
                            {
                                "seconds": elapsed,
                                "raw_bytes": total,
                                "lines": lines,
                                "gzip_bytes": output.tell(),
                            }
                        )
                        + "\n"
                    )
                    samples.flush()
                    next_sample = elapsed + 1
                if elapsed >= seconds:
                    state = "timeout"
                    break
                if not ready.select(min(1, seconds - elapsed)):
                    continue
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    try:
                        process.wait(
                            timeout=max(0.001, seconds - (time.monotonic() - started))
                        )
                    except subprocess.TimeoutExpired:
                        state = "timeout"
                    break
                if total + len(chunk) > raw_limit:
                    state = "raw_limit"
                    break
                packed.write(chunk)
                sha.update(chunk)
                total += len(chunk)
                lines += chunk.count(b"\n")
                tail = (tail + chunk)[-4096:]
                # Leave room for compressor buffers and the gzip trailer.
                if output.tell() >= max(0, gzip_limit - 1024**2):
                    state = "gzip_limit"
                    break
        finally:
            kill_group(process)
            process.stdout.close()
    if state == "ok" and process.returncode != 0:
        state = "error"
    complete = state == "ok" and process.returncode == 0
    if complete:
        path = path.rename(directory / "proof.umd.gz")
    (directory / "proof-tail.txt").write_bytes(tail)
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return {
        **record,
        "user_cpu_seconds": usage_after.ru_utime - usage_before.ru_utime,
        "system_cpu_seconds": usage_after.ru_stime - usage_before.ru_stime,
        "children_peak_rss_kib": usage_after.ru_maxrss,
        "state": state,
        "exit_code": process.returncode,
        "seconds": time.monotonic() - started,
        "complete": complete,
        "raw_bytes": total,
        "raw_sha256": sha.hexdigest(),
        "newline_count": lines,
        "gzip_file": path.name,
        "gzip_bytes": path.stat().st_size,
        "gzip_sha256": digest(path),
        "progress": progress.name,
    }


KERNEL = r"""
set -uo pipefail
prlimit --as=3221225472 --fsize=268435456 -- gzip -dc -- "$1" |
{ time prlimit --as=3221225472 --fsize=268435456 -- "$2" "$3" - \
  > "$4/kernel.out" 2> "$4/kernel.err"; } 2> "$4/kernel.resources.txt"
statuses=("${PIPESTATUS[@]}")
printf '%s %s\n' "${statuses[0]}" "${statuses[1]}" > "$4/kernel-exits.txt"
(( statuses[0] == 0 && statuses[1] == 0 ))
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("results", "feedback", "micromath", "cadical", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--case-index", type=int, choices=range(3), required=True)
    parser.add_argument("--stage-seconds", type=int, default=300)
    parser.add_argument("--raw-proof-bytes", type=int, default=RAW_BYTES)
    parser.add_argument("--gzip-proof-bytes", type=int, default=FILE_BYTES)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("proof diagnostics belong in SLURM")
    if min(args.stage_seconds, args.raw_proof_bytes, args.gzip_proof_bytes) <= 0:
        parser.error("time and proof byte limits must be positive")
    for name in ("results", "feedback", "micromath", "cadical", "out"):
        setattr(args, name, getattr(args, name).resolve())
    audit = json.loads((args.feedback / "summary.json").read_text())
    case = audit["rows"][args.case_index]
    if case["status"] != "independent_checks_passed":
        raise ValueError("only independently validated saved cases are eligible")
    shard, index = case["name"].split("-")[1::2]
    source = args.results / f"shard-{shard}/run/{index}"
    old = json.loads((source.parent / "summary.json").read_text())
    previous = old["rows"][int(index)]
    if (
        previous["case"]["file"] != case["file"]
        or previous["status"] != "proof_timeout"
    ):
        raise ValueError("case identity or prior outcome changed")
    for name in ("smt/prove-lra", "check-solution"):
        if digest(args.micromath / name) != old["binary_sha256"][name]:
            raise ValueError("checker binary changed")
    if digest(args.cadical) != old["binary_sha256"]["cadical"]:
        raise ValueError("CaDiCaL changed")
    for name in ("input.smt2", "native.sexp", "exported-cnf.smt2"):
        if digest(source / name) != case["files"][name]["sha256"]:
            raise ValueError("saved payload changed")
    challenge = source / "challenge.umd"
    if (
        previous.get("challenge_sha256")
        and digest(challenge) != previous["challenge_sha256"]
    ):
        raise ValueError("saved challenge changed")
    args.out.mkdir(parents=True, exist_ok=False)
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "case": case["file"],
        "case_name": case["name"],
        "micromath_git": old["micromath_git"],
        "binary_sha256": old["binary_sha256"],
        "workflow_sha256": digest(Path(__file__)),
        "previous_array": "362761",
        "audit_job": audit["job_id"],
        "source": str(source),
        "payloads": case["files"],
        "challenge_sha256": digest(challenge),
        "challenge_bytes": challenge.stat().st_size,
        "challenge_hash_previously_recorded": "challenge_sha256" in previous,
        "limits": {
            "stage_seconds": args.stage_seconds,
            "process_address_bytes": ADDRESS_BYTES,
            "process_file_bytes": FILE_BYTES,
            "raw_proof_bytes": args.raw_proof_bytes,
            "compressed_proof_bytes": args.gzip_proof_bytes,
        },
        "transport": "producer --raw directly into gzip level1, then separate gzip-fed kernel; no raw file",
        "source_to_cnf_verified": False,
        "finished": False,
    }
    write_json(args.out / "summary.json", report)
    command = [
        "prlimit",
        f"--as={ADDRESS_BYTES}",
        f"--fsize={FILE_BYTES}",
        "--",
        str(args.micromath / "smt/prove-lra"),
        str(challenge),
        str(source / "native.sexp"),
        "-",
        "--raw",
        "--cadical",
        str(args.cadical),
    ]
    report["export"] = capture(
        command,
        args.micromath,
        args.out,
        args.stage_seconds,
        raw_limit=args.raw_proof_bytes,
        gzip_limit=args.gzip_proof_bytes,
    )
    report["status"] = "export_" + report["export"]["state"]
    write_json(args.out / "summary.json", report)
    if report["export"]["complete"]:
        stage = invoke(
            [
                "bash",
                "-c",
                KERNEL,
                "gzip-kernel-replay",
                str(args.out / report["export"]["gzip_file"]),
                str(args.micromath / "check-solution"),
                str(challenge),
                str(args.out),
            ],
            args.micromath,
            args.out / "kernel-pipeline",
            args.stage_seconds,
        )
        report["kernel"] = stage
        exits = args.out / "kernel-exits.txt"
        report["kernel_exit_codes"] = (
            [int(x) for x in exits.read_text().split()] if exits.exists() else []
        )
        messages = (args.out / "kernel.out").read_text(errors="replace").splitlines()
        accepted = (
            stage["state"] == "ok"
            and report["kernel_exit_codes"] == [0, 0]
            and "challenge proven" in messages
            and "Inconsistency proven!" not in messages
        )
        report["status"] = (
            "verified"
            if accepted
            else "kernel_" + (stage["state"] if stage["state"] != "ok" else "rejected")
        )
    report["finished"] = True
    write_json(args.out / "summary.json", report)
    print(
        json.dumps(
            {
                "case": case["file"],
                "status": report["status"],
                "export": report["export"],
            }
        )
    )


if __name__ == "__main__":
    main()
