#!/usr/bin/env python3
"""Compare pinned Micromath builds on the four preserved failure representatives."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import tarfile
from pathlib import Path

from micromath_stress import digest, invoke, write_json
from replay_micromath_lra import PIPE

CASES = (
    ("wisa5", "lia", "packets/definition-index/wisa-definition-allocation"),
    ("clock7", "lra", "packets/proof-limits/lra-clock-7"),
    ("lia-oom", "lia", "packets/proof-limits/lia-native-read-oom"),
    ("challenge-p15", "lia", "packets/challenge-deadline"),
)
CADICAL_SHA256 = "678b51caea85af331086fc5838a0f76e1da72d04a58f52fac79ce9481980c107"


def tail(path: Path) -> str:
    if not path.exists():
        return ""
    with path.open("rb") as stream:
        stream.seek(max(0, path.stat().st_size - 32768))
        return stream.read().decode(errors="replace")


def pipeline_status(stage, codes, output):
    if stage["state"] == "timeout":
        return "proof_timeout"
    if (
        stage["state"] == "ok"
        and codes == [0, 0]
        and "challenge proven" in output.splitlines()
        and "Inconsistency proven!" not in output.splitlines()
    ):
        return "verified"
    return "proof_error"


def unpack_case(bundle, manifest, relative, output, certificate=None):
    """Copy only needed inputs, one bounded block at a time; verify every copied file."""
    if digest(bundle) != manifest["sha256"]:
        raise ValueError("handoff archive hash differs from its manifest")
    wanted = {"case.json", "exported-cnf.smt2"}
    if certificate:
        wanted.add(certificate)
    keys = {f"{relative}/{filename}" for filename in wanted}
    prefix = "micromath-handoff-20261109/"
    found = set()
    with tarfile.open(bundle, "r|gz") as archive:
        for member in archive:
            name = Path(member.name)
            if not member.isfile() or name.is_absolute() or ".." in name.parts:
                raise ValueError("unsafe handoff member")
            key = member.name.removeprefix(prefix)
            if key not in keys:
                continue
            target = output / name.name
            with target.open("xb") as destination:
                shutil.copyfileobj(archive.extractfile(member), destination, 1024**2)
            if digest(target) != manifest["payload_sha256"][key]:
                raise ValueError("preserved checker input changed")
            found.add(name.name)
    if found != wanted:
        raise ValueError("handoff is missing a selected input")
    return {
        name: {"sha256": digest(output / name), "bytes": (output / name).stat().st_size}
        for name in sorted(found)
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("bundle", "manifest", "micromath", "build-report", "cadical", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--case", choices=[row[0] for row in CASES], required=True)
    parser.add_argument("--profile", choices=("old", "new"), required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("checker comparisons require SLURM")
    for name in ("bundle", "manifest", "micromath", "build_report", "cadical", "out"):
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
        raise ValueError("checker revision differs or tracked sources are dirty")
    if not (args.build_report / "FINISHED").is_file():
        raise ValueError("checker build did not finish")
    binary_hashes = {}
    for line in (args.build_report / "binaries.sha256").read_text().splitlines():
        checksum, filename = line.split(maxsplit=1)
        filename = filename.strip()
        if digest(args.micromath / filename) != checksum:
            raise ValueError("checker binary differs from its qualified build")
        binary_hashes[filename] = checksum
    if len(binary_hashes) != 6 or digest(args.cadical) != CADICAL_SHA256:
        raise ValueError("expected six checker binaries and the original CaDiCaL")
    args.out.mkdir(parents=True, exist_ok=False)
    inputs = args.out / "inputs"
    inputs.mkdir()
    name, theory, relative = next(row for row in CASES if row[0] == args.case)
    proof = name != "challenge-p15"
    manifest = json.loads(args.manifest.read_text())
    certificate_name = (
        ("native.sexp" if theory == "lra" else "micromath-v2.sexp") if proof else None
    )
    payloads = unpack_case(args.bundle, manifest, relative, inputs, certificate_name)
    previous = json.loads((inputs / "case.json").read_text())
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "profile": args.profile,
        "case": name,
        "file": previous["case"]["file"],
        "primary_status": previous["status"],
        "micromath_git": actual,
        "binary_sha256": binary_hashes,
        "cadical_sha256": CADICAL_SHA256,
        "archive_sha256": manifest["sha256"],
        "workflow_sha256": digest(Path(__file__)),
        "payloads": payloads,
        "limits": {
            "axioms": 30,
            "challenge": 60,
            "overall_proof_kernel": 120,
            "process_address_bytes": 3221225472,
            "process_file_bytes": 268435456,
        },
        "statement_source": "exported-cnf",
        "source_to_cnf_verified": False,
        "proof_transport": "--raw live pipe; no raw proof file",
        "stages": {},
        "status": "not_started",
        "finished": False,
    }

    def checkpoint():
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
            args.out / log,
            seconds,
        )

    checkpoint()
    axioms = args.out / "axioms.umd"
    stage = bounded(
        [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
        "axioms",
        30,
    )
    report["stages"]["axioms"] = stage
    if stage["state"] != "ok":
        report["status"] = "axioms_" + stage["state"]
    else:
        checkpoint()
        challenge = args.out / "challenge.umd"
        stage = bounded(
            [
                args.micromath / f"smt/build-{theory}-challenge",
                axioms,
                inputs / "exported-cnf.smt2",
                challenge,
            ],
            "challenge",
            60,
        )
        report["stages"]["challenge"] = stage
        if stage["state"] != "ok":
            report["status"] = "challenge_" + stage["state"]
        else:
            report["challenge_bytes"] = challenge.stat().st_size
            report["challenge_sha256"] = digest(challenge)
            if not proof:
                report["status"] = "challenge_built"
            else:
                checkpoint()
                pipe = args.out / "pipe"
                pipe.mkdir()
                certificate = inputs / (
                    "native.sexp" if theory == "lra" else "micromath-v2.sexp"
                )
                stage = invoke(
                    [
                        "bash",
                        "-c",
                        PIPE,
                        "checker-handoff-update",
                        str(args.micromath / f"smt/prove-{theory}"),
                        str(challenge),
                        str(certificate),
                        str(args.cadical),
                        str(args.micromath / "check-solution"),
                        str(pipe),
                        "",
                    ],
                    args.micromath,
                    args.out / "pipeline",
                    120,
                )
                report["stages"]["proof"] = stage
                codes = [int(value) for value in tail(pipe / "exit-status.txt").split()]
                report["pipeline_exit_codes"] = codes
                report["producer_diagnostic"] = tail(pipe / "producer.err")
                report["kernel_result"] = tail(pipe / "kernel.out")
                report["status"] = pipeline_status(
                    stage, codes, report["kernel_result"]
                )
    report["seconds"] = sum(stage["seconds"] for stage in report["stages"].values())
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
