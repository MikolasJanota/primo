#!/usr/bin/env python3
"""Bounded Micromath kernel replay of saved LRA starter certificates in SLURM."""

from __future__ import annotations

import argparse
import json
import os
import platform
from collections import Counter
from pathlib import Path

from check_lra_certificate import parse, spelling
from micromath_stress import digest, invoke, write_json

# --raw streams executed proof steps; both producer and kernel must succeed.
PIPE = r"""
set -uo pipefail
save=()
if [[ -n $7 ]]; then save=(--save "$7"); fi
prlimit --as=3221225472 --fsize=268435456 -- \
  "$1" "$2" "$3" - --raw --cadical "$4" "${save[@]}" \
  2> "$6/producer.err" |
prlimit --as=3221225472 --fsize=268435456 -- \
  "$5" "$2" - > "$6/kernel.out" 2> "$6/kernel.err"
statuses=("${PIPESTATUS[@]}")
printf '%s %s\n' "${statuses[0]}" "${statuses[1]}" > "$6/exit-status.txt"
if (( statuses[0] != 0 || statuses[1] != 0 )); then exit 1; fi
grep -Fxq 'challenge proven' "$6/kernel.out"
! grep -Fxq 'Inconsistency proven!' "$6/kernel.out"
"""


def checked_bundle(root):
    manifest = json.loads((root / "manifest.json").read_text())
    for relative, record in manifest["files"].items():
        path = root / relative
        if path.stat().st_size != record["bytes"] or digest(path) != record["sha256"]:
            raise ValueError(f"bundle hash/size mismatch: {relative}")
    return manifest


def text(path):
    return path.read_text(errors="replace") if path.exists() else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("micromath", "bundle", "cadical", "out"):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--stage-seconds", type=int, default=60)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("checker qualification belongs in SLURM")
    for key in ("micromath", "bundle", "cadical", "out"):
        setattr(args, key, getattr(args, key).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = checked_bundle(args.bundle)
    rows = []
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "micromath_git": args.micromath_git,
        "producer_git": manifest["producer_git"],
        "bundle_manifest_sha256": digest(args.bundle / "manifest.json"),
        "binary_sha256": {
            name: digest(args.micromath / name)
            for name in (
                "build-axioms",
                "smt/build-lra-challenge",
                "smt/prove-lra",
                "check-solution",
            )
        },
        "cadical_sha256": digest(args.cadical),
        "workflow_sha256": digest(Path(__file__)),
        "limits": {
            "stage_seconds": args.stage_seconds,
            "process_address_bytes": 3 * 1024**3,
            "process_file_bytes": 256 * 1024**2,
        },
        "proof_transport": "prove-lra stdout --raw into independent check-solution stdin; no raw proof files",
        "rows": rows,
    }

    def checkpoint():
        report["counts"] = dict(Counter(row["status"] for row in rows))
        write_json(args.out / "summary.json", report)

    def bounded(command, log):
        return invoke(
            ["prlimit", "--as=3221225472", "--fsize=268435456", "--", *command],
            args.micromath,
            log,
            args.stage_seconds,
        )

    axioms = args.out / "axioms.umd"
    report["axioms"] = bounded(
        [str(args.micromath / "build-axioms"), "--fragment", "finitary", str(axioms)],
        args.out / "axioms",
    )
    checkpoint()
    if report["axioms"]["state"] != "ok":
        raise SystemExit(1)

    def replay(name, source, certificate, expected="accept", theory=None, save=None):
        directory = args.out / name
        directory.mkdir()
        challenge = directory / "challenge.umd"
        row = {
            "name": name,
            "expected": expected,
            "status": "not_started",
            "source": str(source),
            "source_sha256": digest(source),
            "certificate": str(certificate),
            "certificate_sha256": digest(certificate),
        }
        row["challenge"] = bounded(
            [
                str(args.micromath / "smt/build-lra-challenge"),
                str(theory or axioms),
                str(source),
                str(challenge),
            ],
            directory / "challenge-build",
        )
        if row["challenge"]["state"] != "ok":
            row["status"] = "challenge_" + row["challenge"]["state"]
        else:
            pipe_dir = directory / "pipe"
            pipe_dir.mkdir()
            row["proof"] = invoke(
                [
                    "bash",
                    "-c",
                    PIPE,
                    "micromath-lra-pipe",
                    str(args.micromath / "smt/prove-lra"),
                    str(challenge),
                    str(certificate),
                    str(args.cadical),
                    str(args.micromath / "check-solution"),
                    str(pipe_dir),
                    str(save) if save else "",
                ],
                args.micromath,
                directory / "pipeline",
                args.stage_seconds,
            )
            codes = [int(x) for x in text(pipe_dir / "exit-status.txt").split()]
            row["pipeline_exit_codes"] = codes
            proven = "challenge proven" in text(pipe_dir / "kernel.out").splitlines()
            inconsistent = (
                "Inconsistency proven!" in text(pipe_dir / "kernel.out").splitlines()
            )
            if row["proof"]["state"] == "timeout":
                row["status"] = "timeout"
            elif expected == "accept":
                row["status"] = (
                    "verified"
                    if row["proof"]["state"] == "ok"
                    and codes == [0, 0]
                    and proven
                    and not inconsistent
                    else "error"
                )
            elif proven or inconsistent:
                row["status"] = "invalid_accepted"
            else:
                # A crash, resource kill, or missing executable is not a valid rejection.
                rejected = len(codes) == 2 and (
                    codes[0] in (1, 2) or codes[0] == 0 and codes[1] in (1, 2)
                )
                row["status"] = "expected_rejection" if rejected else "error"
        rows.append(row)
        checkpoint()
        if row["status"] not in ("verified", "expected_rejection"):
            raise SystemExit(1)

    saved = args.out / "saved-theory.umd"
    for case in manifest["cases"]:
        replay(
            case["name"],
            args.bundle / case["input"],
            args.bundle / case["certificate"],
            save=saved if case["name"] == "05-rational-weight" else None,
        )

    weak = args.bundle / "inputs/01-weak-gap.smt2"
    weak_cert = args.bundle / "certificates/01-weak-gap.sexp"
    controls = args.out / "controls"
    controls.mkdir()
    zero_source = controls / "zero-weight.smt2"
    zero_source.write_text(
        weak.read_text().replace(
            "(assert (<= x 0))",
            "(declare-fun y () Real)\n(assert (<= y 10))\n(assert (<= x 0))",
        )
    )
    replay(
        "accept-zero-weight",
        zero_source,
        args.bundle / "controls/accept-zero-weight.sexp",
    )
    replay(
        "reject-zeroed-weight",
        weak,
        args.bundle / "controls/reject-zeroed-weight.sexp",
        "reject",
    )
    replay(
        "reject-lost-strictness",
        args.bundle / "inputs/02-strict-boundary.smt2",
        args.bundle / "controls/reject-lost-strictness.sexp",
        "reject",
    )

    document = parse(weak_cert.read_text())
    document[4] = ["cnf"]
    empty_cnf = controls / "empty-producer-cnf.sexp"
    empty_cnf.write_text(spelling(document) + "\n")
    replay("accept-empty-producer-cnf", weak, empty_cnf)

    sat_source = controls / "satisfiable-source.smt2"
    sat_source.write_text(
        "(set-logic QF_LRA)(declare-fun x () Real)(assert (<= x 0))(check-sat)\n"
    )
    replay("reject-satisfiable-source", sat_source, weak_cert, "reject")
    replay(
        "reuse-saved-theory",
        args.bundle / "inputs/05-rational-weight.smt2",
        args.bundle / "certificates/05-rational-weight.sexp",
        theory=saved,
    )
    checkpoint()


if __name__ == "__main__":
    main()
