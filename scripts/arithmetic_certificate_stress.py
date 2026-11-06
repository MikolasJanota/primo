#!/usr/bin/env python3
"""Bounded CORE/source/kernel stress pilot on recorded-fast arithmetic UNSAT inputs."""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import time
from collections import Counter, defaultdict
from pathlib import Path

from micromath_stress import digest, invoke, write_json
from replay_micromath_lra import PIPE
from select_micromath_fast_cases import DECLARATION, read_history

TIERS = ((4096, 32768), (32768, 131072), (131072, 524289))


def excerpt(path):
    with Path(path).open("rb") as stream:
        first = stream.read(65536)
        if stream.read(1):
            stream.seek(max(0, Path(path).stat().st_size - 4096))
            first += b"\n[... log truncated ...]\n" + stream.read(4096)
    return first.decode(errors="replace")


def select_cases(records, corpus, count, per_family):
    pools = [defaultdict(list) for _ in TIERS]
    excluded = Counter()
    seen = set()
    for record in records:
        relative = Path(record["file"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("unsafe historical input path")
        path = corpus / relative
        if not path.is_file():
            excluded["missing"] += 1
            continue
        size = path.stat().st_size
        tier = next((i for i, (lo, hi) in enumerate(TIERS) if lo <= size < hi), None)
        if tier is None:
            excluded["source_size"] += 1
            continue
        if record["sha256"] in seen:
            excluded["duplicate_hash"] += 1
            continue
        # Conservative lexical admission screen, not a complete SMT-LIB parser.
        source = path.read_text()
        if source.count("(check-sat)") != 1 or any(
            x in source for x in ("(push", "(pop", "(get-", "(reset", "(set-option")
        ):
            excluded["session_screen"] += 1
            continue
        if any(match[2].strip() for match in DECLARATION.finditer(source)):
            excluded["positive_arity_declaration"] += 1
            continue
        if digest(path) != record["sha256"]:
            raise ValueError(f"historical input changed: {relative}")
        seen.add(record["sha256"])
        pools[tier][record["family"]].append(
            {**record, "source_bytes": size, "size_tier": tier}
        )
    for pool in pools:
        for cases in pool.values():
            cases.sort(key=lambda row: (row["source_bytes"], row["file"]))
    selected = []
    families = Counter()
    targets = [count // 3 + (i < count % 3) for i in range(3)]

    def take(tier, limit):
        taken = 0
        while taken < limit:
            changed = False
            for family in sorted(pools[tier]):
                cases = pools[tier][family]
                if cases and families[family] < per_family:
                    selected.append(cases.pop(0))
                    families[family] += 1
                    taken += 1
                    changed = True
                    if taken == limit:
                        break
            if not changed:
                break

    for tier, target in enumerate(targets):
        take(tier, target)
    for tier in range(3):
        take(tier, count - len(selected))
    selected.sort(key=lambda row: (row["size_tier"], row["source_bytes"], row["file"]))
    if len(selected) != count:
        raise ValueError(f"only {len(selected)} eligible inputs for requested {count}")
    return {
        "cases": selected,
        "excluded": dict(excluded),
        "tier_targets": targets,
        "tier_selected": dict(Counter(row["size_tier"] for row in selected)),
        "families": dict(families),
        "selection": "recorded native UNSAT<=1s; stratified4..32/32..128/128..512KiB; smallest first within tiers; max cases/family; conservative single-query/no-positive-arity screen",
    }


def lia_header_copy(source, target):
    """Stream native LIA into an explicit one-byte envelope copy; never rewrite its body."""
    with source.open("rb") as original:
        header = [original.readline(128) for _ in range(3)]
        if header != [b"(primo-conflicts\n", b"  (version 3)\n", b"  (logic QF_LIA)\n"]:
            raise ValueError(
                "unexpected native LIA envelope; no compatibility copy written"
            )
        with target.open("xb") as output:
            output.write(header[0] + b"  (version 2)\n" + header[2])
            shutil.copyfileobj(original, output, 1024 * 1024)
    return {
        "source_sha256": digest(source),
        "copy_sha256": digest(target),
        "changed_byte_offset": 28,
        "old": "3",
        "new": "2",
        "body": "copied verbatim in bounded chunks",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logic", choices=("QF_LRA", "QF_LIA"), required=True)
    for flag in (
        "history",
        "corpus",
        "solver",
        "reference",
        "cadical",
        "micromath",
        "out",
    ):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--historical-git", required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--count", type=int, default=24)
    parser.add_argument("--max-per-family", type=int, default=3)
    parser.add_argument("--reference-seconds", type=int, default=15)
    parser.add_argument("--export-seconds", type=int, default=30)
    parser.add_argument("--challenge-seconds", type=int, default=30)
    parser.add_argument("--proof-seconds", type=int, default=60)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("stress selection and solver/checker batches require SLURM")
    for key in (
        "history",
        "corpus",
        "solver",
        "reference",
        "cadical",
        "micromath",
        "out",
    ):
        setattr(args, key, getattr(args, key).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    selection = select_cases(
        read_history(args.history, args.historical_git, 1),
        args.corpus,
        args.count,
        args.max_per_family,
    )
    write_json(args.out / "selection.json", selection)
    arithmetic = "lia" if args.logic == "QF_LIA" else "lra"
    rows = []
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": platform.node(),
        "logic": args.logic,
        "micromath_git": args.micromath_git,
        "requested": args.count,
        "limits": {
            "reference": args.reference_seconds,
            "export": args.export_seconds,
            "challenge": args.challenge_seconds,
            "proof_and_kernel": args.proof_seconds,
            "process_address_bytes": 3221225472,
            "process_file_bytes": 268435456,
        },
        "native_profile": "recorded default preprocessing plus CORE export",
        "proof_transport": "--raw stdout pipe; original source statement; no raw proof files",
        "rows": rows,
        "workflow_sha256": digest(Path(__file__)),
        "binary_sha256": {
            "primo": digest(args.solver),
            "reference": digest(args.reference),
            "cadical": digest(args.cadical),
            **{
                name: digest(args.micromath / name)
                for name in (
                    "build-axioms",
                    f"smt/build-{arithmetic}-challenge",
                    f"smt/prove-{arithmetic}",
                    "check-solution",
                )
            },
        },
    }

    def checkpoint():
        report["completed"] = len(rows)
        report["counts"] = dict(Counter(row["status"] for row in rows))
        report["families"] = {
            family: dict(
                Counter(
                    row["status"] for row in rows if row["case"]["family"] == family
                )
            )
            for family in sorted({row["case"]["family"] for row in rows})
        }
        write_json(args.out / "summary.json", report)

    def bounded(command, log, seconds, cwd=None):
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

    axioms = args.out / "axioms.umd"
    report["axioms"] = bounded(
        [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
        args.out / "axioms",
        30,
        args.micromath,
    )
    checkpoint()
    if report["axioms"]["state"] != "ok":
        raise SystemExit(1)
    for index, case in enumerate(selection["cases"]):
        directory = args.out / f"{index:03d}"
        directory.mkdir()
        source = directory / "input.smt2"
        shutil.copyfile(args.corpus / case["file"], source)
        row = {"case": case, "status": "not_started", "stages": {}}
        started = time.monotonic()
        try:
            if digest(source) != case["sha256"]:
                raise ValueError("snapshot hash differs from historical input")
            ref = bounded(
                [args.reference, source],
                directory / "reference",
                args.reference_seconds,
            )
            row["stages"]["reference"] = ref
            answer = [
                line
                for line in excerpt(ref["stdout"]).splitlines()
                if line in ("sat", "unsat", "unknown")
            ]
            if ref["state"] != "ok":
                row["status"] = "reference_" + ref["state"]
            elif answer != ["unsat"]:
                row["status"] = "reference_disagreement"
            else:
                challenge = directory / "challenge.umd"
                build = bounded(
                    [
                        args.micromath / f"smt/build-{arithmetic}-challenge",
                        axioms,
                        source,
                        challenge,
                    ],
                    directory / "challenge",
                    args.challenge_seconds,
                    args.micromath,
                )
                row["stages"]["challenge"] = build
                if build["state"] != "ok":
                    row["status"] = "challenge_" + build["state"]
                else:
                    certificate = directory / "native.sexp"
                    export = bounded(
                        [
                            args.solver,
                            "--quiet",
                            "--stats",
                            "--export-cnf",
                            certificate,
                            "--export-cnf-reduction=core",
                            source,
                        ],
                        directory / "export",
                        args.export_seconds,
                    )
                    row["stages"]["export"] = export
                    output = excerpt(export["stdout"])
                    native_answers = [
                        line
                        for line in output.splitlines()
                        if line in ("sat", "unsat", "unknown")
                    ]
                    stats = excerpt(export["stderr"])
                    row["export_statistics"] = {
                        key: int(value)
                        for key, value in re.findall(
                            r"(?m)^(:export\.[\w.-]+) (\d+)$", stats
                        )
                    }
                    if export["state"] != "ok":
                        row["status"] = "export_" + export["state"]
                    elif native_answers == ["sat"]:
                        row["status"] = "wrong_answer"
                    elif native_answers != ["unsat"]:
                        row["status"] = (
                            "reconstruction_incomplete"
                            if row["export_statistics"].get(":export.main-unsat") == 1
                            else "main_inconclusive"
                        )
                    elif not certificate.is_file():
                        row["status"] = "missing_certificate"
                    else:
                        row["native_certificate_bytes"] = certificate.stat().st_size
                        row["native_certificate_sha256"] = digest(certificate)
                        proof_input = certificate
                        if arithmetic == "lia":
                            proof_input = directory / "micromath-v2.sexp"
                            row["header_copy"] = lia_header_copy(
                                certificate, proof_input
                            )
                        pipe = directory / "pipe"
                        pipe.mkdir()
                        proof = invoke(
                            [
                                "bash",
                                "-c",
                                PIPE,
                                "arithmetic-stress-pipe",
                                str(args.micromath / f"smt/prove-{arithmetic}"),
                                str(challenge),
                                str(proof_input),
                                str(args.cadical),
                                str(args.micromath / "check-solution"),
                                str(pipe),
                                "",
                            ],
                            args.micromath,
                            directory / "pipeline",
                            args.proof_seconds,
                        )
                        row["stages"]["proof"] = proof
                        codes_path = pipe / "exit-status.txt"
                        codes = (
                            [int(value) for value in codes_path.read_text().split()]
                            if codes_path.exists()
                            else []
                        )
                        row["pipeline_exit_codes"] = codes
                        kernel = excerpt(pipe / "kernel.out")
                        row["kernel_result"] = kernel
                        producer = excerpt(pipe / "producer.err")
                        row["producer_diagnostic"] = producer[-2000:]
                        if proof["state"] == "timeout":
                            row["status"] = "proof_timeout"
                        elif (
                            proof["state"] == "ok"
                            and codes == [0, 0]
                            and "challenge proven" in kernel.splitlines()
                            and "Inconsistency proven!" not in kernel.splitlines()
                        ):
                            row["status"] = "verified"
                            row["challenge_sha256"] = digest(challenge)
                            challenge.unlink()
                        elif (
                            len(codes) == 2
                            and all(code in (0, 1, 2) for code in codes)
                            and "s SATISFIABLE" in producer.splitlines()
                        ):
                            row["status"] = "source_refutation_missing"
                        elif any(code == 153 for code in codes):
                            row["status"] = "proof_file_limit"
                        elif any(code == 137 for code in codes):
                            row["status"] = "proof_killed"
                        else:
                            row["status"] = "proof_error"
        except (OSError, ValueError) as error:
            row["status"] = "workflow_error"
            row["error"] = str(error)
        row["seconds"] = time.monotonic() - started
        rows.append(row)
        checkpoint()
        if row["status"] in (
            "wrong_answer",
            "reference_disagreement",
            "workflow_error",
        ):
            raise SystemExit(2)
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
