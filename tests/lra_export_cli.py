#!/usr/bin/env python3
"""SLURM export qualification: fresh QF_LRA answers, exact witnesses and Boolean refutation."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import random
import subprocess
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "lra_checker", Path(__file__).parents[1] / "scripts/check_lra_certificate.py"
)
CHECKER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(CHECKER)


def invoke(command, source):
    return subprocess.run(
        command, input=source, text=True, capture_output=True, timeout=15, check=False
    )


def generate(index, rng):
    a, b = rng.randrange(-20, 21), rng.randrange(-20, 21)
    scale = rng.randrange(1, 10)
    unsat = index % 5 != 0
    strict = index % 2 == 0
    bound = a + b if strict or not unsat else a + b - 1
    if not unsat:
        bound += 3
    p = "(> y " + str(b) + ")" if strict else "(>= y " + str(b) + ")"
    premises = [f"(>= x {a})", p, f"(<= (* {scale} (+ x y)) {scale * bound})"]
    if index % 7 == 0:
        # Negated comparisons exercise recorded literal polarity.
        premises[0] = f"(not (< x {a}))"
    if index % 11 == 0:
        premises.append("(or (= x y) (not (= x y)))")
    if index % 13 == 0:
        premises.append("(= z (ite flag (+ x y) (- y x)))")
    if index % 3 == 0:
        second = [f"(>= x {a + 1})", f"(>= y {b - 1})", f"(< (+ x y) {a + b})"]
        body = (
            "(assert (or (and "
            + " ".join(premises[:3])
            + ") (and "
            + " ".join(second)
            + ")))"
        )
        body += "".join(f"(assert {fact})" for fact in premises[3:])
    else:
        body = "".join(f"(assert {fact})" for fact in premises)
    source = (
        "(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)(declare-fun z () Real)(declare-fun flag () Bool)"
        + body
        + "(check-sat)"
    )
    return source, "unsat" if unsat else "sat"


def focused_cases():
    prelude = "(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)(declare-fun z () Real)(declare-fun flag () Bool)"
    problems = [
        ("(assert (= x y))(assert (< x y))", "unsat"),
        ("(assert (distinct x y))(assert (<= x y))(assert (<= y x))", "unsat"),
        ("(assert (distinct x y))(assert (< x y))", "sat"),
        (
            "(assert flag)(assert (= z (ite flag (+ x y) (- x y))))(assert (>= x 2))(assert (>= y 3))(assert (< z 5))",
            "unsat",
        ),
        (
            "(define-fun sum ((a Real) (b Real)) Real (+ a b))(assert (<= (sum x y) 0))(assert (> x 1))(assert (>= y 0))",
            "unsat",
        ),
        ("(assert (<= (* (/ 2 7) x) (- (/ 3 11))))(assert (>= x 0))", "unsat"),
        (
            "(assert (<= (* 100000000000000000000000000001 x) 1))(assert (>= x 1))",
            "unsat",
        ),
        (
            "(declare-fun |quoted real| () Real)(assert (not (<= |quoted real| (/ 1 3))))(assert (<= |quoted real| (/ 1 3)))",
            "unsat",
        ),
    ]
    return [(prelude + body + "(check-sat)", expected) for body, expected in problems]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--sat-solver", required=True)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=202611050)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("export qualification belongs in SLURM")
    args.out.mkdir(parents=True, exist_ok=False)
    rng = random.Random(args.seed)
    counts = {
        "pass": 0,
        "sat": 0,
        "unsat": 0,
        "wrong_answer": 0,
        "error": 0,
        "timeout": 0,
    }
    entries = 0
    profiles = ["none", "core", "mus"]
    focused = focused_cases()
    for index in range(args.count + len(focused)):
        if index < len(focused):
            source, expected = focused[index]
        else:
            source, expected = generate(index - len(focused), rng)
        unsat = expected == "unsat"
        path = args.out / f"{index:04d}.sexp"
        (args.out / f"{index:04d}.smt2").write_text(source)
        try:
            ref = invoke([args.reference, "-in"], source)
            if ref.returncode or ref.stdout.strip() != expected:
                raise ValueError(f"reference disagrees: {ref}")
            mode = profiles[index % 3]
            command = [
                args.solver,
                "--quiet",
                "--stats",
                "--export-cnf",
                str(path),
                f"--export-cnf-reduction={mode}",
                "--preprocessing-mode=" + ("auto" if index % 17 == 0 else "off"),
            ]
            if index % 3 == 1:
                command.append("--lra-propagation=false")
            if index % 19 == 0:
                command += [
                    "--lra-row-propagation",
                    "--lra-bidirectional-row-propagation",
                ]
            result = invoke(command, source)
            (args.out / f"{index:04d}.out").write_text(result.stdout)
            (args.out / f"{index:04d}.err").write_text(result.stderr)
            answers = [
                x.strip()
                for x in result.stdout.splitlines()
                if x.strip() in ("sat", "unsat", "unknown")
            ]
            if result.returncode or answers != [expected]:
                if answers and answers[0] in ("sat", "unsat") and answers != [expected]:
                    counts["wrong_answer"] += 1
                raise ValueError(f"solver/export failed: {result}")
            if unsat:
                checked = CHECKER.check_file(path, args.sat_solver)
                entries += checked["entries"]
            elif path.exists():
                raise ValueError("SAT export created an artifact")
            counts["pass"] += 1
            counts[expected] += 1
        except subprocess.TimeoutExpired as error:
            counts["timeout"] += 1
            (args.out / f"{index:04d}.failure").write_text(repr(error))
        except (OSError, ValueError) as error:
            counts["error"] += 1
            (args.out / f"{index:04d}.failure").write_text(repr(error))
        (args.out / "summary.json").write_text(
            json.dumps(
                {
                    "seed": args.seed,
                    "generated_count": args.count,
                    "focused_count": len(focused),
                    "count": args.count + len(focused),
                    "completed": index + 1,
                    "counts": counts,
                    "witness_entries": entries,
                    "modes": profiles,
                },
                indent=2,
            )
            + "\n"
        )
        if counts["error"] or counts["wrong_answer"]:
            raise SystemExit(1)
    if counts["timeout"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
