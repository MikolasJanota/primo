#!/usr/bin/env python3
"""Check unchanged native v3 integer cuts and small authored controls in SLURM."""

from __future__ import annotations

import argparse
import copy
import json
import os
import platform
import re
from collections import Counter
from pathlib import Path

from check_lra_certificate import parse, spelling
from micromath_stress import digest, invoke, write_json
from replay_micromath_lra import replay_case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("micromath", "examples", "cadical", "out"):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument(
        "--micromath-v2-envelope",
        action="store_true",
        help="test an explicitly labelled header-only compatibility copy",
    )
    parser.add_argument("--stage-seconds", type=int, default=60)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("checker qualification belongs in SLURM")
    for key in ("micromath", "examples", "cadical", "out"):
        setattr(args, key, getattr(args, key).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    metadata = json.loads(
        (args.examples / "qf-lia-certificate-native.json").read_text()
    )
    for file, sha in metadata["files"].items():
        if digest(args.examples / file) != sha:
            raise ValueError(f"native input hash mismatch: {file}")
    source = args.examples / "qf-lia-certificate-proposal.smt2"
    native = args.examples / "qf-lia-certificate-native.sexp"
    document = parse(native.read_text())
    if document[1] != ["version", "3"]:
        raise ValueError("expected unchanged native version-3 certificate")
    rows = []
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "micromath_git": args.micromath_git,
        "producer_git": metadata["source_git"],
        "native_metadata_sha256": digest(
            args.examples / "qf-lia-certificate-native.json"
        ),
        "native_envelope": "version3 unchanged; no adaptation",
        "binary_sha256": {
            name: digest(args.micromath / name)
            for name in (
                "build-axioms",
                "smt/build-lia-challenge",
                "smt/prove-lia",
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
        "rows": rows,
    }

    def checkpoint():
        report["counts"] = dict(Counter(row["status"] for row in rows))
        write_json(args.out / "summary.json", report)

    axioms = args.out / "axioms.umd"
    report["axioms"] = invoke(
        [
            "prlimit",
            "--as=3221225472",
            "--fsize=268435456",
            "--",
            str(args.micromath / "build-axioms"),
            "--fragment",
            "finitary",
            str(axioms),
        ],
        args.micromath,
        args.out / "axioms",
        args.stage_seconds,
    )
    checkpoint()
    if report["axioms"]["state"] != "ok":
        raise SystemExit(1)

    def replay(name, certificate, expected="accept", statement=source, **options):
        replay_case(
            args,
            axioms,
            rows,
            checkpoint,
            name,
            statement,
            certificate,
            expected,
            arithmetic="lia",
            **options,
        )

    controls = args.out / "controls"
    controls.mkdir()
    report["header_copies"] = []

    def compatibility_copy(path):
        raw = path.read_bytes()
        # This diagnostic fixture copy changes one byte in the envelope only.
        match = re.search(rb"(?m)^[ \t]*\(version[ \t]+(3)[ \t]*\)[ \t]*\r?$", raw)
        if match is None:
            raise ValueError("expected a standalone version-3 header")
        offset = match.start(1)
        adapted = raw[:offset] + b"2" + raw[offset + 1 :]
        result = controls / (path.stem + "-micromath-v2.sexp")
        result.write_bytes(adapted)
        report["header_copies"].append(
            {
                "source": str(path),
                "source_sha256": digest(path),
                "copy": str(result),
                "copy_sha256": digest(result),
                "changed_byte_offset": offset,
                "old": "3",
                "new": "2",
            }
        )
        return result

    handwritten = args.examples / "qf-lia-certificate-proposal.sexp"
    if args.micromath_v2_envelope:
        replay("00-native-v3-envelope", native, "reject")
        rows[-1]["status"] = "expected_envelope_rejection"
        report["native_envelope"] = (
            "original v3 rejected; proof payload preserved in explicitly labelled v2 header copies"
        )
        native = compatibility_copy(native)
        handwritten = compatibility_copy(handwritten)
        document = parse(native.read_text())
        checkpoint()
    saved = args.out / "saved-theory.umd"
    replay("01-native-proof", native, save=saved)
    replay("02-handwritten-proof", handwritten)

    def altered(name, change, expected="accept"):
        root = copy.deepcopy(document)
        change(root)
        path = controls / f"{name}.sexp"
        path.write_text(spelling(root) + "\n")
        replay(name, path, expected)

    altered("02-empty-producer-cnf", lambda root: root.__setitem__(4, ["cnf"]))
    zero = copy.deepcopy(document)
    zero[5][1][2][-2].append(["p0", "0"])
    zero_path = controls / "zero-weight.sexp"
    zero_path.write_text(spelling(zero) + "\n")
    replay("03-zero-weight-reference", zero_path)
    bare = controls / "bare-conflict.sexp"
    bare.write_text(spelling(document[5][1]) + "\n")
    replay("04-bare-conflict", bare)
    altered(
        "reject-inexact-divisor",
        lambda root: root[5][1][2][1].__setitem__(2, "3"),
        "reject",
    )
    altered(
        "reject-forward-reference",
        lambda root: root[5][1][2][1][3].__setitem__(0, "s0"),
        "reject",
    )
    altered(
        "reject-negative-weight",
        lambda root: root[5][1][2][1][3].__setitem__(1, ["-", "1"]),
        "reject",
    )
    altered(
        "reject-false-contradiction",
        lambda root: root[5][1][2][-1].__setitem__(1, "p0"),
        "reject",
    )
    sat_source = controls / "satisfiable-source.smt2"
    sat_source.write_text(
        "(set-logic QF_LIA)(declare-fun x () Int)(declare-fun y () Int)(assert (>= y 1))(check-sat)\n"
    )
    replay("reject-satisfiable-source", native, "reject", statement=sat_source)
    replay("06-reuse-saved-theory", native, theory=saved)
    checkpoint()


if __name__ == "__main__":
    main()
