#!/usr/bin/env python3
"""Validate saved LIA checker-failure inputs with Yices and independent cut/SAT replay.

Runs only in SLURM. This audits the exported-CNF certificate boundary; it does
not prove preprocessing or inspect the external checker implementation.
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

from arithmetic_certificate_stress import excerpt, lia_header_copy
from micromath_stress import digest, invoke, write_json


def exact_header_copy(native, adapted):
    """Compare bounded chunks against an independently generated header copy."""
    temporary = adapted.with_name("expected-v2.sexp")
    metadata = lia_header_copy(native, temporary)
    try:
        if digest(adapted) != metadata["copy_sha256"]:
            raise ValueError("saved LIA compatibility copy does not match native body")
        return metadata
    finally:
        temporary.unlink()


def package_feedback(args, report, original, scripts):
    """Include only hash-matched inputs that passed every independent check."""
    bundle = args.out / "feedback"
    bundle.mkdir()
    cases = []
    for row in report["rows"]:
        if row["status"] not in (
            "independent_checks_passed",
            "checker_error_reproduced",
        ):
            continue
        index = row["index"]
        name = f"{index:03}-" + (
            "closed-boolean"
            if row["previous_status"] == "proof_error"
            else row["previous_status"]
        )
        target = bundle / name
        target.mkdir()
        audited = args.out / f"{index:03}"
        saved = args.previous_run / f"{index:03}"
        for filename in row["files"]:
            shutil.copyfile(audited / filename, target / filename)
        for source, directory in (
            (saved, target / "original-logs"),
            (audited, target / "audit-logs"),
        ):
            directory.mkdir()
            for pattern in (
                "*.out",
                "*.err",
                "pipe/*.out",
                "pipe/*.err",
                "pipe/exit-status.txt",
                "replay/*.out",
                "replay/*.err",
                "replay/exit-status.txt",
            ):
                for path in source.glob(pattern):
                    relative = path.relative_to(source)
                    destination = directory / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, destination)
        write_json(target / "case.json", row)
        cases.append(
            {
                "directory": name,
                "original_file": row["case"]["file"],
                "observed": row["previous_status"],
                "audit": row["status"],
            }
        )
    if not cases:
        return {"status": "no independently validated feedback cases"}
    for source, name in (
        ("replay_micromath_lia_feedback.sh", "replay.sh"),
        ("check_lia_certificate.py", "check_lia_certificate.py"),
        ("check_lra_certificate.py", "check_lra_certificate.py"),
        ("certificate_cnf_statement.py", "certificate_cnf_statement.py"),
    ):
        shutil.copyfile(scripts / source, bundle / name)
    (bundle / "replay.sh").chmod(0o755)
    guide = f"""# Micromath LIA exported-CNF feedback

Pinned checker: {report["micromath_git"]}. Original pilot: SLURM {original["job_id"]}.
Independent audit: SLURM {report["job_id"]}. Yices version:

{report["yices_version"]}

Each included case passed Yices UNSAT checks on both the original SMT-LIB and
exported-CNF statement; exact Python integer-cut replay and independent CaDiCaL
propositional UNSAT; regenerated statement byte equality; native/copy hashes;
and the explicit one-byte LIA version-3 to version-2 envelope-copy check.
No producer-side defect was found by these checks. This does not prove the
original-to-CNF transformation or establish checker soundness.

Cases marked checker_error_reproduced were replayed during the audit.
Included resource-limit cases retain the original pilot's logs and budgets;
their checker runs were not repeated by this audit. Limits are feedback about
this workload and budget, not claims of a checker correctness bug. The checker
implementation was neither inspected nor modified; no upstream fetch occurred.
Read manifest.json and each case.json for the actual included subset/status.

From an allocated Linux compute node, after building the pinned checker targets:

    bash replay.sh /path/to/Micromath /path/to/cadical CASE_DIRECTORY /fresh/log/directory

The script builds the challenge from exported-cnf.smt2, reads micromath-v2.sexp,
and pipes --raw proof output directly to check-solution. It uses 3 GiB address
space and 256 MiB maximum file size per child, 30 seconds for challenge building,
and 60 seconds per proof/kernel process. Error exits are expected for these
feedback cases. Original pipeline runs used one overall 60-second proof deadline;
the standalone script uses per-process deadlines and prints both exit codes.
An error exit or timeout is not a verified proof. Valid acceptance requires both
processes to exit zero and the kernel to print exactly challenge proven.

Optional independent checks (Yices and CaDiCaL are external executables):

    /path/to/yices-smt2 CASE_DIRECTORY/input.smt2
    /path/to/yices-smt2 CASE_DIRECTORY/exported-cnf.smt2
    python3 check_lia_certificate.py CASE_DIRECTORY/native.sexp --sat-solver /path/to/cadical

Native certificates are unchanged producer bytes; micromath-v2.sexp changes only
byte 28 from 3 to 2. Original sources are for provenance/reference checks. The
statement being kernel-checked is the exported CNF. No challenge dumps, checker
binaries or raw proofs are bundled. These are original instances, not minimized.
"""
    (bundle / "README.md").write_text(guide)
    files = {
        str(path.relative_to(bundle)): {
            "sha256": digest(path),
            "bytes": path.stat().st_size,
        }
        for path in sorted(bundle.rglob("*"))
        if path.is_file()
    }
    write_json(
        bundle / "manifest.json",
        {
            "cases": cases,
            "files": files,
            "micromath_git": report["micromath_git"],
            "audit_job": report["job_id"],
        },
    )
    archive = args.out / "micromath-lia-feedback.tar.gz"
    with (
        archive.open("wb") as output,
        gzip.GzipFile(fileobj=output, mode="wb", mtime=0) as compressed,
        tarfile.open(fileobj=compressed, mode="w") as tar,
    ):
        for path in sorted(bundle.rglob("*")):
            if not path.is_file():
                continue
            info = tar.gettarinfo(str(path), str(path.relative_to(bundle)))
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = 0
            info.mode = 0o755 if path.name == "replay.sh" else 0o644
            with path.open("rb") as source:
                tar.addfile(info, source)
    return {
        "archive": archive.name,
        "sha256": digest(archive),
        "bytes": archive.stat().st_size,
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("previous-run", "micromath", "yices", "cadical", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--indices", default="11,14,15,18,19")
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("certificate validation and checker replay require SLURM")
    for key in ("previous_run", "micromath", "yices", "cadical", "out"):
        setattr(args, key, getattr(args, key).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    original = json.loads((args.previous_run / "summary.json").read_text())
    if (
        original["logic"] != "QF_LIA"
        or original.get("statement_source") != "exported-cnf"
    ):
        raise ValueError("expected saved exported-CNF QF_LIA pilot")
    revision = subprocess.check_output(
        ["git", "-C", str(args.micromath), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != args.micromath_git or revision != original["micromath_git"]:
        raise ValueError("checker revision differs from saved failure")
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
        raise ValueError("tracked checker files changed")
    scripts = Path(__file__).resolve().parent
    binary_hashes = {
        name: digest(args.micromath / name)
        for name in (
            "build-axioms",
            "smt/build-lia-challenge",
            "smt/prove-lia",
            "check-solution",
        )
    }
    binary_hashes["cadical"] = digest(args.cadical)
    if any(
        value != original["binary_sha256"][name]
        for name, value in binary_hashes.items()
    ):
        raise ValueError("checker/SAT binary differs from original failure")
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "micromath_git": revision,
        "binary_sha256": {**binary_hashes, "yices": digest(args.yices)},
        "previous_job": original["job_id"],
        "previous_summary_sha256": digest(args.previous_run / "summary.json"),
        "statement_source": "exported-cnf",
        "source_to_cnf_verified": False,
        "rows": [],
        "workflow_sha256": {
            name: digest(scripts / name)
            for name in (
                "audit_arithmetic_checker_failures.py",
                "arithmetic_certificate_stress.py",
                "certificate_cnf_statement.py",
                "micromath_stress.py",
                "replay_micromath_lra.py",
                "replay_micromath_lia_feedback.sh",
                "slurm_arithmetic_checker_audit.sbatch",
                "select_micromath_fast_cases.py",
                "check_lia_certificate.py",
                "check_lra_certificate.py",
            )
        },
        "limits": {
            "validation_seconds": 60,
            "challenge_seconds": 30,
            "proof_seconds": 60,
            "replay_script_seconds": 125,
            "address_bytes": 3221225472,
            "file_bytes": 268435456,
        },
    }

    def checkpoint():
        report["completed"] = len(report["rows"])
        write_json(args.out / "summary.json", report)

    def bounded(command, log, seconds=60, cwd=None):
        return invoke(
            [
                "prlimit",
                "--as=3221225472",
                "--fsize=268435456",
                "--",
                *map(str, command),
            ],
            cwd or args.out,
            log,
            seconds,
        )

    version = bounded([args.yices, "--version"], args.out / "yices-version", 10)
    if version["state"] != "ok":
        raise ValueError("Yices version command failed")
    report["yices_version"] = excerpt(version["stdout"])
    checkpoint()
    for index in map(int, args.indices.split(",")):
        previous = original["rows"][index]
        if previous["status"] not in (
            "proof_error",
            "proof_timeout",
            "challenge_timeout",
        ):
            raise ValueError("selected row is not a saved checker failure")
        saved = args.previous_run / f"{index:03}"
        directory = args.out / f"{index:03}"
        directory.mkdir()
        row = {
            "index": index,
            "case": previous["case"],
            "previous_status": previous["status"],
            "status": "validation_incomplete",
            "files": {},
            "stages": {},
        }
        for name in (
            "input.smt2",
            "native.sexp",
            "micromath-v2.sexp",
            "exported-cnf.smt2",
        ):
            shutil.copyfile(saved / name, directory / name)
            row["files"][name] = {
                "sha256": digest(directory / name),
                "bytes": (directory / name).stat().st_size,
            }
        expected = {
            "input.smt2": previous["case"]["sha256"],
            "native.sexp": previous["native_certificate_sha256"],
            "micromath-v2.sexp": previous["header_copy"]["copy_sha256"],
            "exported-cnf.smt2": previous["statement"]["sha256"],
        }
        if any(
            row["files"][name]["sha256"] != value for name, value in expected.items()
        ):
            raise ValueError("saved input hash mismatch")
        row["header_copy"] = exact_header_copy(
            directory / "native.sexp", directory / "micromath-v2.sexp"
        )
        for label, name in (
            ("original_reference", "input.smt2"),
            ("cnf_reference", "exported-cnf.smt2"),
        ):
            stage = bounded([args.yices, directory / name], directory / label)
            row["stages"][label] = stage
            row[label + "_unsat"] = stage["state"] == "ok" and excerpt(
                stage["stdout"]
            ).splitlines() == ["unsat"]
        conversion = bounded(
            [
                sys.executable,
                scripts / "certificate_cnf_statement.py",
                directory / "native.sexp",
                directory / "regenerated.smt2",
                "--logic",
                "QF_LIA",
            ],
            directory / "regeneration",
        )
        row["stages"]["regeneration"] = conversion
        row["statement_byte_match"] = (
            conversion["state"] == "ok"
            and digest(directory / "regenerated.smt2")
            == row["files"]["exported-cnf.smt2"]["sha256"]
        )
        if row["statement_byte_match"]:
            (directory / "regenerated.smt2").unlink()
        independent = bounded(
            [
                sys.executable,
                scripts / "check_lia_certificate.py",
                directory / "native.sexp",
                "--sat-solver",
                args.cadical,
            ],
            directory / "independent",
        )
        row["stages"]["independent"] = independent
        if independent["state"] == "ok":
            row["independent_report"] = json.loads(excerpt(independent["stdout"]))
        disagreement = any(
            stage["state"] == "ok" and excerpt(stage["stdout"]).splitlines() == ["sat"]
            for label, stage in row["stages"].items()
            if label in ("original_reference", "cnf_reference")
        )
        if disagreement:
            row["status"] = "reference_disagreement"
            report["rows"].append(row)
            checkpoint()
            raise SystemExit(2)
        valid = (
            row["original_reference_unsat"]
            and row["cnf_reference_unsat"]
            and row["statement_byte_match"]
            and independent["state"] == "ok"
            and row["independent_report"].get("propositional_unsat") is True
        )
        if valid:
            row["status"] = "independent_checks_passed"
            # Reproduce errors, while preserving historical resource-limit observations.
            if previous["status"] == "proof_error":
                replay = directory / "replay"
                stage = invoke(
                    [
                        "bash",
                        str(scripts / "replay_micromath_lia_feedback.sh"),
                        str(args.micromath),
                        str(args.cadical),
                        str(directory),
                        str(replay),
                    ],
                    args.micromath,
                    directory / "replay-script",
                    125,
                )
                row["stages"]["replay"] = stage
                codes_file = replay / "exit-status.txt"
                codes = (
                    [int(x) for x in codes_file.read_text().split()]
                    if codes_file.exists()
                    else []
                )
                row["pipeline_exit_codes"] = codes
                diagnostic_file = replay / "producer.err"
                diagnostic = (
                    excerpt(diagnostic_file) if diagnostic_file.exists() else ""
                )
                row["producer_diagnostic"] = diagnostic[-2000:]
                kernel_file = replay / "kernel.out"
                kernel = (
                    excerpt(kernel_file).splitlines() if kernel_file.exists() else []
                )
                row["error_reproduced"] = (
                    stage["state"] == "error"
                    and codes == [1, 1]
                    and diagnostic.strip()
                    == "Logic failure: evaluation needs a closed Boolean term"
                    and "challenge proven" not in kernel
                    and "Inconsistency proven!" not in kernel
                )
                row["status"] = (
                    "checker_error_reproduced"
                    if row.get("error_reproduced")
                    else "replay_changed"
                )
        report["rows"].append(row)
        checkpoint()
    report["package"] = package_feedback(args, report, original, scripts)
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
