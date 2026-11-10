#!/usr/bin/env python3
"""SLURM-only exact/reference and independent-kernel qualification of the adapter."""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import subprocess
import sys
import tarfile
from collections import Counter
from fractions import Fraction as F
from pathlib import Path

from certificate_cnf_statement import convert_certificate
from check_lra_certificate import check_file, parse
from farkas_coefficients import weight_text
from micromath_stress import digest, invoke, write_json
from replay_micromath_lra import PIPE


def numeric(value):
    value = F(value)
    numerator = str(value.numerator) if value >= 0 else f"(- {-value.numerator})"
    return (
        numerator if value.denominator == 1 else f"(/ {numerator} {value.denominator})"
    )


def generated_case(index, rng):
    satisfiable = index % 4 == 0
    count = rng.randrange(1, 5)
    coefficients = [F(rng.randrange(1, 20), rng.randrange(1, 8)) for _ in range(count)]
    bounds = [F(rng.randrange(-10, 11), rng.randrange(1, 8)) for _ in range(count)]
    strict = not satisfiable and index % 3 == 0
    facts = [
        f"({'<' if strict and i == 0 else '<='} (* {numeric(a)} x{i}) {numeric(b)})"
        for i, (a, b) in enumerate(zip(coefficients, bounds))
    ]
    terms = [f"(* {numeric(-a)} x{i})" for i, a in enumerate(coefficients)]
    total = terms[0] if len(terms) == 1 else "(+ " + " ".join(terms) + ")"
    rhs = -sum(bounds) + (1 if satisfiable else 0 if strict else -1)
    facts.append(f"(<= {total} {numeric(rhs)})")
    scale = F(rng.randrange(1000, 100000), rng.randrange(1000, 100000))
    weights = [scale] * len(facts)
    if index % 5 == 0:
        facts.append(facts[0])
        weights[0] /= 2
        weights.append(weights[0])
    declarations = " ".join(f"(declare-fun x{i} () Real)" for i in range(count))
    statement = (
        "(set-logic QF_LRA)\n"
        + declarations
        + "\n"
        + "\n".join(f"(assert {fact})" for fact in facts)
        + "\n(check-sat)\n"
    )
    certificate = (
        "(primo-conflicts (version 2) (logic QF_LRA) (signature "
        + declarations
        + ") (cnf "
        + " ".join(f"(clause {fact})" for fact in facts)
        + ") (conflicts (conflict-lra "
        + " ".join(f"({fact} {weight_text(w)})" for fact, w in zip(facts, weights))
        + ")))\n"
    )
    return certificate, statement, "sat" if satisfiable else "unsat"


def same_structure(original, candidate):
    left, right = parse(original.read_text()), parse(candidate.read_text())
    if left[:5] != right[:5]:
        raise ValueError("declarations or initial CNF changed")
    a = [[pair[0] for pair in lemma[1:]] for lemma in left[5][1:]]
    b = [[pair[0] for pair in lemma[1:]] for lemma in right[5][1:]]
    if a != b:
        raise ValueError("theory literals/order changed")
    return True


def kernel_result(stage, directory):
    codes_path = directory / "exit-status.txt"
    codes = (
        [int(x) for x in codes_path.read_text().split()] if codes_path.exists() else []
    )
    text = (
        (directory / "kernel.out").read_text()
        if (directory / "kernel.out").exists()
        else ""
    )
    return (
        stage["state"] == "ok"
        and codes == [0, 0]
        and "challenge proven" in text.splitlines()
        and "Inconsistency proven!" not in text.splitlines(),
        codes,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("generated", "real"), required=True)
    for flag in (
        "out",
        "z3",
        "cadical",
        "micromath",
        "handoff",
        "handoff-manifest",
        "starters",
        "authored",
    ):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--build-report", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("qualification requires SLURM")
    for flag in (
        "out",
        "z3",
        "cadical",
        "micromath",
        "handoff",
        "handoff_manifest",
        "starters",
        "authored",
        "build_report",
    ):
        setattr(args, flag, getattr(args, flag).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    scripts = Path(__file__).resolve().parent
    report = {
        "job_id": os.environ["SLURM_JOB_ID"],
        "mode": args.mode,
        "workflow_sha256": {
            p: digest(scripts / p)
            for p in (
                "minimize_lra_coefficients.py",
                "farkas_coefficients.py",
                "qualify_farkas_coefficients.py",
            )
        },
        "z3_version": subprocess.check_output(
            [str(args.z3), "--version"], text=True
        ).strip(),
        "z3_sha256": digest(args.z3),
        "cadical_sha256": digest(args.cadical),
        "rows": [],
        "finished": False,
        "seed": 20261110,
    }

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

    def adapt(source, directory, optimize=False, force=False):
        target = directory / "simplified.sexp"
        command = [
            sys.executable,
            scripts / "minimize_lra_coefficients.py",
            source,
            target,
            "--report",
            directory / "statistics.json",
        ]
        if optimize:
            command += [
                "--optimizer",
                args.z3,
                "--problems-dir",
                directory / "problems",
                "--total-seconds",
                "30",
                "--call-seconds",
                "1",
            ]
            if force:
                command += ["--min-bits", "0"]
        stage = bounded(command, directory / "adapter", 150)
        return target, stage

    checkpoint()
    if args.mode == "generated":
        rng = random.Random(report["seed"])
        for index in range(1000):
            directory = args.out / f"{index:04d}"
            directory.mkdir()
            text, statement, expected = generated_case(index, rng)
            source = directory / "input.sexp"
            source.write_text(text)
            problem = directory / "source.smt2"
            problem.write_text(statement)
            reference = bounded([args.z3, problem], directory / "reference", 5)
            answer = (directory / "reference.out").read_text().strip()
            if reference["state"] != "ok" or answer != expected:
                raise ValueError(f"reference disagreement at generated case {index}")
            target, stage = adapt(
                source,
                directory,
                optimize=expected == "unsat" and index < 68,
                force=True,
            )
            if expected == "unsat":
                if stage["state"] != "ok":
                    raise ValueError(f"valid generated certificate rejected at {index}")
                same_structure(source, target)
                check_file(target, args.cadical)
                stats = json.loads((directory / "statistics.json").read_text())
                if (
                    stats["after_max_bits"] > stats["before_max_bits"]
                    or stats["after_weight_bytes"] > stats["before_weight_bytes"]
                ):
                    raise ValueError("coefficient proxy regressed")
                status = "valid_witness_pass"
            else:
                if stage["state"] == "ok" or target.exists():
                    raise ValueError("SAT source obtained a certificate")
                stats = None
                status = "invalid_witness_rejected"
            report["rows"].append(
                {
                    "index": index,
                    "status": status,
                    "source_sha256": digest(source),
                    "reference": reference,
                    "adapter": stage,
                    "statistics": stats,
                }
            )
            checkpoint()
    else:
        actual = subprocess.check_output(
            ["git", "-C", str(args.micromath), "rev-parse", "HEAD"], text=True
        ).strip()
        if actual != args.micromath_git:
            raise ValueError("checker revision changed")
        report["micromath_git"] = actual
        report["checker_sha256"] = {}
        for line in (args.build_report / "binaries.sha256").read_text().splitlines():
            value, name = line.split(maxsplit=1)
            if digest(args.micromath / name) != value:
                raise ValueError("checker binary changed")
            report["checker_sha256"][name] = value
        inputs = args.out / "inputs"
        inputs.mkdir()
        with tarfile.open(args.starters) as archive:
            for member in archive:
                if (
                    member.isfile()
                    and member.name.startswith("lra-starter/certificates/")
                    and member.name.endswith(".sexp")
                ):
                    (inputs / Path(member.name).name).write_bytes(
                        archive.extractfile(member).read()
                    )
        if len(list(inputs.glob("*.sexp"))) != 12:
            raise ValueError("starter archive must provide all 12 certificates")
        manifest = json.loads(args.handoff_manifest.read_text())
        if digest(args.handoff) != manifest["sha256"]:
            raise ValueError("handoff changed")
        with tarfile.open(args.handoff) as archive:
            path = "micromath-handoff-20261109/packets/proof-limits/lra-clock-7/native.sexp"
            data = archive.extractfile(path).read()
            (inputs / "clock7.sexp").write_bytes(data)
        (inputs / "authored-choice.sexp").write_bytes(args.authored.read_bytes())
        axioms = args.out / "axioms.umd"
        stage = bounded(
            [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
            args.out / "axioms",
            30,
        )
        if stage["state"] != "ok":
            raise ValueError("axiom construction failed")
        for source in sorted(inputs.glob("*.sexp")):
            case = args.out / source.stem
            case.mkdir()
            statement = case / "exported-cnf.smt2"
            convert_certificate(source, statement, "QF_LRA")
            challenge = case / "challenge.umd"
            stage = bounded(
                [
                    args.micromath / "smt/build-lra-challenge",
                    axioms,
                    statement,
                    challenge,
                ],
                case / "challenge",
                60,
            )
            if stage["state"] != "ok":
                raise ValueError("challenge construction failed")
            for profile in ("original", "cheap", "optimized"):
                directory = case / profile
                directory.mkdir()
                certificate = source
                stats = None
                adapter = None
                if profile != "original":
                    certificate, adapter = adapt(
                        source,
                        directory,
                        profile == "optimized",
                        source.stem == "authored-choice",
                    )
                    if adapter["state"] != "ok":
                        raise ValueError("adapter failed on saved input")
                    same_structure(source, certificate)
                    stats = json.loads((directory / "statistics.json").read_text())
                check_file(certificate, args.cadical)
                pipe = directory / "pipe"
                pipe.mkdir()
                stage = invoke(
                    [
                        "bash",
                        "-c",
                        PIPE,
                        "farkas-coefficient-replay",
                        str(args.micromath / "smt/prove-lra"),
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
                accepted, codes = kernel_result(stage, pipe)
                diagnostic = (pipe / "producer.err").read_text(errors="replace")
                steps = {
                    m[1]: int(m[2])
                    for m in re.finditer(
                        r"^(.+): (\d+) kernel steps", diagnostic, re.MULTILINE
                    )
                }
                report["rows"].append(
                    {
                        "case": source.name,
                        "profile": profile,
                        "status": "kernel_verified"
                        if accepted
                        else "proof_" + stage["state"],
                        "source_sha256": digest(source),
                        "certificate_sha256": digest(certificate),
                        "certificate_bytes": certificate.stat().st_size,
                        "statement_sha256": digest(statement),
                        "adapter": adapter,
                        "statistics": stats,
                        "pipeline": stage,
                        "pipeline_exit_codes": codes,
                        "kernel_steps": steps,
                    }
                )
                checkpoint()
                if not accepted:
                    raise ValueError("independent checker replay failed")
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
