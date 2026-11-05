#!/usr/bin/env python3
"""SLURM QF_LIA export gate: fresh answers, integer proofs and Boolean refutation."""

from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
from check_lia_certificate import check_file


def invoke(command, source):
    return subprocess.run(
        command, input=source, text=True, capture_output=True, timeout=15, check=False
    )


def numeral(value):
    return f"(- {-value})" if value < 0 else str(value)


def generate(index, rng):
    a, b = rng.randrange(-20, 21), rng.randrange(-20, 21)
    x, y = f"(- x {numeral(a)})", f"(- y {numeral(b)})"
    unsat = index % 5 != 0
    family = (index // 5) % 5
    scale = rng.randrange(1, 10)

    def upper(term, bound):
        if index % 2:
            return f"(<= (* {scale} {term}) {numeral(scale * bound)})"
        return f"(not (>= (* {scale} {term}) {numeral(scale * bound + 1)}))"

    if family == 0:
        facts = [upper(f"(+ (* 3 {x}) (* 2 {y}))", 4 if unsat else 5)]
        facts += [f"(>= {y} 1)", f"(>= (* 2 {x}) 1)"]
    elif family == 1:
        facts = [upper(f"(+ (* 2 {x}) {y})", 2 if unsat else 3)]
        facts += [upper(f"(+ (* (- 2) {x}) {y})", 0), f"(>= {y} 1)"]
    elif family == 2:
        divisor = rng.randrange(2, 20)
        facts = [upper(f"(* {divisor} {x})", divisor - 1 if unsat else divisor)]
        facts += [f"(>= (* {divisor} {x}) 1)"]
    elif family == 3:
        facts = [upper(f"(+ {x} {y})", 0 if unsat else 2)]
        facts += [f"(>= {x} 1)", f"(>= {y} 0)"]
    else:
        divisor = rng.randrange(2, 20)
        facts = [f"(= (* {divisor} {x}) {1 if unsat else divisor})"]
    body = "(and " + " ".join(facts) + ")"
    if index % 3 == 0:
        # The second branch is always inconsistent; the first determines status.
        body = f"(or {body} (and (>= y {numeral(b)}) (< y {numeral(b)})))"
    extra = ""
    if index % 11 == 0:
        extra += "(assert (or (= x y) (not (= x y))))"
    if index % 13 == 0:
        extra += "(assert (= z (ite flag (+ x y) (- y x))))"
    return (
        prelude() + f"(assert {body})" + extra + "(check-sat)",
        "unsat" if unsat else "sat",
    )


def prelude():
    return (
        "(set-logic QF_LIA)(declare-fun x () Int)(declare-fun y () Int)"
        "(declare-fun z () Int)(declare-fun flag () Bool)"
    )


def focused_cases():
    problems = [
        ("(assert (= x y))(assert (< x y))", "unsat"),
        ("(assert (distinct x y))(assert (<= x y))(assert (<= y x))", "unsat"),
        ("(assert (distinct x y))(assert (< x y))", "sat"),
        (
            (
                "(assert flag)(assert (= z (ite flag (+ x y) (- x y))))"
                "(assert (>= x 2))(assert (>= y 3))(assert (< z 5))"
            ),
            "unsat",
        ),
        (
            (
                "(define-fun sum ((a Int) (b Int)) Int (+ a b))"
                "(assert (<= (sum x y) 0))(assert (> x 1))(assert (>= y 0))"
            ),
            "unsat",
        ),
        ("(assert (<= (* 2 x) (- 1)))(assert (>= (* 2 x) (- 1)))", "unsat"),
        (
            "(assert (<= (* 100000000000000000000000000001 x) 1))(assert (>= x 1))",
            "unsat",
        ),
        (
            (
                "(declare-fun |quoted integer| () Int)"
                "(assert (not (<= |quoted integer| 1)))(assert (<= |quoted integer| 1))"
            ),
            "unsat",
        ),
        ("(assert (= x 3))(assert (= (div x 2) 2))", "unsat"),
        ("(assert (= x 3))(assert (= (mod x 2) 0))", "unsat"),
        ("(assert (<= (abs x) (- 1)))", "unsat"),
        ("(assert false)", "unsat"),
    ]
    return [(prelude() + body + "(check-sat)", expected) for body, expected in problems]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("solver", "reference", "sat-solver"):
        parser.add_argument(f"--{flag}", required=True)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=202611052)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("export qualification belongs in SLURM")
    args.out.mkdir(parents=True, exist_ok=False)
    rng = random.Random(args.seed)
    counts = dict.fromkeys(
        (
            "pass",
            "sat",
            "unsat",
            "wrong_answer",
            "error",
            "timeout",
            "unsupported",
            "reference_disagreement",
        ),
        0,
    )
    modes = ["none", "core", "mus"]
    focused = focused_cases()
    totals = {"cuts": 0, "entries": 0, "conflicts": 0}
    for index in range(args.count + len(focused)):
        source, expected = (
            focused[index]
            if index < len(focused)
            else generate(index - len(focused), rng)
        )
        path = args.out / f"{index:04d}.sexp"
        (args.out / f"{index:04d}.smt2").write_text(source)
        try:
            reference = invoke([args.reference, "-in"], source)
            if reference.returncode or reference.stdout.strip() != expected:
                counts["reference_disagreement"] += 1
                raise ValueError(f"reference disagrees: {reference}")
            command = [
                args.solver,
                "--quiet",
                "--stats",
                "--export-cnf",
                str(path),
                f"--export-cnf-reduction={modes[index % 3]}",
                "--preprocessing-mode=" + ("auto" if index % 17 == 0 else "off"),
            ]
            if index % 3 == 1:
                command.append("--lra-propagation=false")
            result = invoke(command, source)
            (args.out / f"{index:04d}.out").write_text(result.stdout)
            (args.out / f"{index:04d}.err").write_text(result.stderr)
            answers = [
                s
                for s in result.stdout.splitlines()
                if s in ("sat", "unsat", "unknown")
            ]
            if result.returncode or answers != [expected]:
                if any(s in ("sat", "unsat") and s != expected for s in answers):
                    counts["wrong_answer"] += 1
                if "unsupported" in result.stderr.lower():
                    counts["unsupported"] += 1
                raise ValueError(f"solver/export failed: {result}")
            if expected == "unsat":
                checked = check_file(path, args.sat_solver)
                for key in totals:
                    totals[key] += checked[key]
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
                    "witness_totals": totals,
                    "modes": modes,
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
