#!/usr/bin/env python3
"""SLURM-only qualification of support objectives and fixed-conclusion LIA edits."""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

from certificate_cnf_statement import convert_certificate
from check_lia_certificate import check_file as check_integer
from check_lra_certificate import check_file as check_real
from check_lra_certificate import parse
from micromath_stress import digest, invoke, write_json
from qualify_farkas_coefficients import generated_case, kernel_result, numeric
from replay_micromath_lra import PIPE


def generated_integer(index, rng):
    """Integer-only half-integral interval, two cuts, and optional duplicate rows."""
    k, scale = rng.randrange(-100, 101), rng.randrange(2, 100000)
    sat = index % 4 == 0
    high = 2 * k + (2 if sat else 1)
    facts = [f"(<= (* (- 2) x) {numeric(-2 * k - 1)})", f"(<= (* 2 x) {numeric(high)})"]
    first = f"(cut lower {2 * scale} (p0 {scale}))"
    premises = [f"(p{i} {fact})" for i, fact in enumerate(facts)]
    if index % 3 == 0:
        facts.append(facts[0])
        premises.append(f"(p2 {facts[0]})")
        first = f"(cut lower {4 * scale} (p0 {scale}) (p2 {scale}))"
    signature = "(declare-fun x () Int)"
    statement = "(set-logic QF_LIA)\n" + signature + "\n"
    statement += "\n".join(f"(assert {fact})" for fact in facts) + "\n(check-sat)\n"
    certificate = (
        f"(primo-conflicts (version 3) (logic QF_LIA) (signature {signature}) (cnf "
        + " ".join(f"(clause {fact})" for fact in facts)
        + ") (conflicts (conflict-lia (premises "
        + " ".join(premises)
        + f") (proof {first} (cut unused 1 (p1 1))"
        + f" (cut impossible {scale} (lower {2 * scale}) (p1 {scale}) (unused 0))"
        + " (contradiction impossible)))))\n"
    )
    return certificate, statement, "sat" if sat else "unsat"


def same_structure(left, right):
    def stripped(path):
        root = parse(path.read_text())
        for conflict in root[5][1:]:
            if root[2][1] == "QF_LRA":
                for pair in conflict[1:]:
                    pair[1] = "WEIGHT"
            else:
                for cut in conflict[2][1:-1]:
                    cut[2] = "DIVISOR"
                    for pair in cut[3:]:
                        pair[1] = "WEIGHT"
        return root

    if stripped(left) != stripped(right):
        raise ValueError("certificate structure changed outside weights/divisors")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("generated", "kernel"), required=True)
    for flag in (
        "out",
        "z3",
        "cadical",
        "micromath",
        "build-report",
        "lra-inputs",
        "examples",
    ):
        parser.add_argument(f"--{flag}", type=Path, required=True)
    parser.add_argument("--micromath-git", required=True)
    parser.add_argument("--workflow-git", required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("qualification requires SLURM")
    for flag in (
        "out",
        "z3",
        "cadical",
        "micromath",
        "build_report",
        "lra_inputs",
        "examples",
    ):
        setattr(args, flag, getattr(args, flag).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    scripts = Path(__file__).resolve().parent
    report = {
        "mode": args.mode,
        "workflow_git": args.workflow_git,
        "job_id": os.environ["SLURM_JOB_ID"],
        "seed": 20261111,
        "finished": False,
        "rows": [],
        "workflow_sha256": {p.name: digest(p) for p in scripts.glob("*.py")},
        "z3_sha256": digest(args.z3),
        "cadical_sha256": digest(args.cadical),
        "z3_version": subprocess.check_output(
            [str(args.z3), "--version"], text=True
        ).strip(),
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

    def adapt(source, directory, objective=None):
        output = directory / "simplified.sexp"
        command = [
            sys.executable,
            scripts / "minimize_lra_coefficients.py",
            source,
            output,
            "--report",
            directory / "statistics.json",
        ]
        if objective:
            command += [
                "--objective",
                objective,
                "--optimizer",
                args.z3,
                "--min-bits",
                "0",
                "--problems-dir",
                directory / "problems",
            ]
        stage = bounded(command, directory / "adapter", 150)
        return output, stage

    def checked(source, target, logic):
        same_structure(source, target)
        (check_real if logic == "QF_LRA" else check_integer)(target, args.cadical)

    checkpoint()
    if args.mode == "generated":
        rng = random.Random(report["seed"])
        for logic, generate in (
            ("QF_LRA", generated_case),
            ("QF_LIA", generated_integer),
        ):
            for index in range(1000):
                directory = args.out / f"{logic}-{index:04d}"
                directory.mkdir()
                text, statement, expected = generate(index, rng)
                source, problem = directory / "input.sexp", directory / "input.smt2"
                source.write_text(text)
                problem.write_text(statement)
                reference = bounded([args.z3, problem], directory / "reference", 5)
                if (
                    reference["state"] != "ok"
                    or (directory / "reference.out").read_text().strip() != expected
                ):
                    raise ValueError(f"reference disagreement at {logic}/{index}")
                objective = ("support" if index % 2 else "size") if index < 80 else None
                target, stage = adapt(source, directory, objective)
                if expected == "unsat":
                    if stage["state"] != "ok":
                        raise ValueError(f"valid witness rejected at {logic}/{index}")
                    checked(source, target, logic)
                    stats = json.loads((directory / "statistics.json").read_text())
                    if (
                        stats["after_max_bits"] > stats["before_max_bits"]
                        or stats["after_weight_bytes"] + stats["after_divisor_bytes"]
                        > stats["before_weight_bytes"] + stats["before_divisor_bytes"]
                    ):
                        raise ValueError("numerical size guard violated")
                    status = "valid_witness_pass"
                else:
                    if stage["state"] != "error" or target.exists():
                        raise ValueError("SAT control not rejected normally")
                    stats, status = None, "sat_control_rejected"
                report["rows"].append(
                    {
                        "logic": logic,
                        "index": index,
                        "status": status,
                        "objective": objective,
                        "reference": reference,
                        "adapter": stage,
                        "statistics": stats,
                        "input_sha256": digest(source),
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
        for source in sorted(args.lra_inputs.glob("*.sexp")):
            (inputs / ("lra-" + source.name)).write_bytes(source.read_bytes())
        if len(list(inputs.glob("lra-*.sexp"))) != 14:
            raise ValueError(
                "expected twelve LRA starters, clock7 and authored example"
            )
        for name in (
            "qf-lia-certificate-native.sexp",
            "qf-lia-certificate-proposal.sexp",
            "lia-coefficient-choice.sexp",
        ):
            (inputs / name).write_bytes((args.examples / name).read_bytes())
        (inputs / "lia-scaled-negative-floor.sexp").write_text(
            generated_integer(3, random.Random(20261111))[0]
        )
        axioms = args.out / "axioms.umd"
        if (
            bounded(
                [args.micromath / "build-axioms", "--fragment", "finitary", axioms],
                args.out / "axioms",
                30,
            )["state"]
            != "ok"
        ):
            raise ValueError("axiom construction failed")
        for source in sorted(inputs.glob("*.sexp")):
            logic = parse(source.read_text())[2][1]
            theory = "lra" if logic == "QF_LRA" else "lia"
            case = args.out / source.stem
            case.mkdir()
            statement, challenge = case / "exported-cnf.smt2", case / "challenge.umd"
            convert_certificate(source, statement, logic)
            command = [
                args.micromath / f"smt/build-{theory}-challenge",
                axioms,
                statement,
                challenge,
            ]
            if bounded(command, case / "challenge", 60)["state"] != "ok":
                raise ValueError("challenge construction failed")
            for profile in ("original", "cheap", "size", "support"):
                directory = case / profile
                directory.mkdir()
                certificate, stage, stats = source, None, None
                if profile != "original":
                    certificate, stage = adapt(
                        source, directory, None if profile == "cheap" else profile
                    )
                    if stage["state"] != "ok":
                        raise ValueError("saved certificate adaptation failed")
                    stats = json.loads((directory / "statistics.json").read_text())
                checked(source, certificate, logic)
                if theory == "lia":
                    # Only the external checker's envelope differs; the native body is unchanged.
                    compatible = directory / "micromath-v2.sexp"
                    compatible.write_bytes(
                        certificate.read_bytes().replace(
                            b"(version 3)", b"(version 2)", 1
                        )
                    )
                    certificate = compatible
                pipe = directory / "pipe"
                pipe.mkdir()
                pipeline = invoke(
                    [
                        "bash",
                        "-c",
                        PIPE,
                        "arithmetic-coefficient-replay",
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
                accepted, codes = kernel_result(pipeline, pipe)
                diagnostic = (pipe / "producer.err").read_text(errors="replace")
                report["rows"].append(
                    {
                        "case": source.name,
                        "logic": logic,
                        "profile": profile,
                        "status": "kernel_verified"
                        if accepted
                        else "proof_" + pipeline["state"],
                        "input_sha256": digest(source),
                        "certificate_sha256": digest(certificate),
                        "certificate_bytes": certificate.stat().st_size,
                        "adapter": stage,
                        "statistics": stats,
                        "pipeline": pipeline,
                        "pipeline_exit_codes": codes,
                        "kernel_steps": {
                            m[1]: int(m[2])
                            for m in re.finditer(
                                r"^(.+): (\d+) kernel steps", diagnostic, re.MULTILINE
                            )
                        },
                    }
                )
                checkpoint()
                if not accepted:
                    raise ValueError("independent kernel replay failed")
    report["finished"] = True
    checkpoint()


if __name__ == "__main__":
    main()
