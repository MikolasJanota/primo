#!/usr/bin/env python3
"""Generate mixed QF_UFLIA cases and compare Primo with reference solvers."""

from __future__ import annotations

import argparse
import csv
import os
import random
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}

LIA_ATOMS = [
    "(<= 0 x)",
    "(<= x 2)",
    "(< x y)",
    "(<= (+ x y) 3)",
    "(<= (- x z) 1)",
    "(= y 0)",
    "(= z 1)",
    "(= w 2)",
    "(<= (* 2 x) (+ y 4))",
]

EUF_ATOMS = [
    "(= (f x) (f y))",
    "(= (f y) (f z))",
    "(= (g x y) (g y x))",
    "(p x)",
    "(p y)",
    "(q x y)",
    "(= (h b) x)",
    "(r b x)",
]

PURIFIED_ATOMS = [
    "(p (+ x 1))",
    "(q (+ x y) z)",
    "(= (f (+ x 1)) (f y))",
    "(<= (f x) (+ y 2))",
    "(< (+ (f x) z) 5)",
    "(= (g x y) (+ z 1))",
    "(= (h (p x)) (+ y 1))",
]


def with_polarity(rng: random.Random, atom: str) -> str:
    return atom if rng.randrange(2) == 0 else f"(not {atom})"


def declarations() -> str:
    return """(set-logic QF_UFLIA)
(declare-fun x () Int)
(declare-fun y () Int)
(declare-fun z () Int)
(declare-fun w () Int)
(declare-fun b () Bool)
(declare-fun f (Int) Int)
(declare-fun g (Int Int) Int)
(declare-fun h (Bool) Int)
(declare-fun p (Int) Bool)
(declare-fun q (Int Int) Bool)
(declare-fun r (Bool Int) Bool)
"""


def render(assertions: list[str]) -> str:
    body = "\n".join(f"(assert {assertion})" for assertion in assertions)
    return declarations() + body + "\n(check-sat)\n"


def generate_nonconvex_case(rng: random.Random, index: int) -> str:
    upper = 1 + rng.randrange(2)
    anchors = [("y", 0), ("z", 1), ("w", 2)][: upper + 1]
    assertions = ["(<= 0 x)", f"(<= x {upper})"]
    assertions.extend(f"(= {name} {value})" for name, value in anchors)

    # LIA entails a disjunction x=0 v ... v x=upper, but no single equality.
    # Congruence plus these result disequalities rejects every arrangement in
    # even cases. Odd cases omit one disequality and remain satisfiable.
    forbidden = anchors if index % 2 == 0 else anchors[:-1]
    assertions.extend(f"(not (= (f x) (f {name})))" for name, _ in forbidden)
    assertions.extend(["(or (p x) (not (p x)))", with_polarity(rng, "(r b x)")])
    return render(assertions)


def generate_random_case(rng: random.Random, index: int) -> str:
    assertions = [with_polarity(rng, atom) for atom in rng.sample(LIA_ATOMS, 3)]
    assertions.extend(with_polarity(rng, atom) for atom in rng.sample(EUF_ATOMS, 3))
    assertions.extend(
        with_polarity(rng, atom) for atom in rng.sample(PURIFIED_ATOMS, 2)
    )
    if index % 5 == 0:
        assertions.extend(["(= x y)", "(not (= (f x) (f y)))"])
    if index % 7 == 0:
        assertions.extend(["(= (+ x 1) y)", "(p (+ x 1))", "(not (p y))"])
    if index % 11 == 0:
        assertions.extend(["(= b (p x))", "(not (= (h b) (h (p x))))"])
    return render(assertions)


# The curated atom lists above are a fixed vocabulary: every case they build
# applies an uninterpreted function to a variable or to an arithmetic term, so
# no Int application is ever nested inside another. That shape is common in
# real inputs -- 18% of a 300-file sample of the QF_UFLRA benchmarks nest one UF
# application directly inside another -- and it is the shape that exercises
# model completion, which resolves an unvalued application by evaluating the
# arguments of other applications of the same symbol. Terms are composed
# bottom-up, one depth level at a time, so the builder needs no recursion and
# the depth is exact rather than expected.
INT_VARIABLES = ["x", "y", "z", "w"]


def numeral(value: int) -> str:
    """SMT-LIB has no negative numeral literal; -2 must be written (- 2)."""
    return str(value) if value >= 0 else f"(- {-value})"


# Divisors are drawn from both signs and include the units, so the Euclidean
# sign identities and the div/mod-by-one folds are all exercised. Zero is
# excluded: a literal zero divisor is a documented rejection, not an answer,
# and it is checked separately rather than mixed into differential runs.
DIVISORS = [-4, -3, -2, -1, 1, 2, 3, 4]

# Boolean conditions for generated ites, kept cheap so the condition is not
# where the difficulty lives.
def boolean_atom(rng: random.Random, terms: list[str]) -> str:
    relation = rng.choice(["<=", "<", "=", ">="])
    return f"({relation} {rng.choice(terms)} {rng.choice(terms)})"


def compose_int_terms(rng: random.Random, depth: int, width: int,
                      extended: bool = False) -> list[str]:
    """Return Int terms whose UF application nesting reaches `depth`.

    `ite` is always available: it belongs to the Core theory and is legal in
    every logic, so cases containing it stay conforming QF_UFLIA and Dolmen
    will model-check them. `*` is the operator that needs care -- see the
    comment on the scaling case below, which is where this generator was
    emitting non-conforming input.

    `extended` additionally emits div, mod and abs. Those are *not* in
    QF_UFLIA -- the logic definition excludes "the function symbols *, **, /,
    div, mod, and abs" and its :extensions clause readmits only `*` with
    concrete coefficients -- so Primo accepts them as a documented extension
    and Dolmen rightly refuses to check such a file. Cases in that shape are
    therefore validated through Z3 rather than Dolmen, which is why the two
    shapes are kept apart. FuzzSMT emits none of these operators for any Int
    logic, so this generator is their only source of coverage.
    """
    level = INT_VARIABLES + [numeral(value) for value in range(-2, 3)]
    deepest = list(level)
    for _ in range(depth):
        shallower = level
        # Always apply a unary function to something from the previous level,
        # so at least one term in the new level is genuinely one deeper.
        level = [f"(f {rng.choice(deepest)})"]
        for _ in range(width - 1):
            choice = rng.randrange(8 if extended else 5)
            left = rng.choice(shallower)
            right = rng.choice(shallower)
            if choice == 0:
                level.append(f"(f {left})")
            elif choice == 1:
                level.append(f"(g {left} {right})")
            elif choice == 2:
                level.append(f"(+ {left} {right})")
            elif choice == 3:
                # QF_UFLIA's :extensions readmits * only as (* c x) / (* x c)
                # with c an integer numeral and x a *free constant*. Scaling a
                # composite term is semantically linear but not conforming, and
                # Dolmen enforces the letter of it: (* 2 (* 2 (+ x (- 1))))
                # made it reject the file, which the model gate then reported
                # as a model error against a solver that had answered
                # correctly. Keep the operand a declared symbol.
                scaled = rng.choice(INT_VARIABLES)
                coefficient = numeral(rng.choice([-3, -2, 2, 3, 5]))
                level.append(f"(* {coefficient} {scaled})" if rng.random() < 0.5
                             else f"(* {scaled} {coefficient})")
            elif choice == 4:
                level.append(f"(ite {boolean_atom(rng, shallower)} {left} {right})")
            elif choice == 5:
                level.append(f"(div {left} {numeral(rng.choice(DIVISORS))})")
            elif choice == 6:
                level.append(f"(mod {left} {numeral(rng.choice(DIVISORS))})")
            else:
                level.append(f"(abs {left})")
        deepest = level
    return deepest


def generate_composed_case(rng: random.Random, index: int, depth: int,
                           extended: bool = False) -> str:
    """A case built from composed terms rather than the fixed atom vocabulary."""
    terms = compose_int_terms(rng, depth, width=4, extended=extended)
    assertions: list[str] = []
    for _ in range(2 + rng.randrange(3)):
        relation = rng.choice(["<=", "<", "="])
        assertions.append(
            with_polarity(
                rng, f"({relation} {rng.choice(terms)} {rng.choice(terms)})"
            )
        )
    # Model completion only has to resolve a nested application when something
    # forces its result. Equating a variable to a deep term and then bounding
    # that variable is what makes the completion do real work: it has to find
    # argument values for the inner applications that reach the bound, rather
    # than defaulting them.
    anchor = rng.choice(INT_VARIABLES)
    assertions.append(f"(= {anchor} {rng.choice(terms)})")
    assertions.append(f"(<= {anchor} {numeral(rng.randrange(-2, 7))})")
    pinned = rng.choice([v for v in INT_VARIABLES if v != anchor])
    assertions.append(f"(= {pinned} {numeral(rng.randrange(-2, 3))})")
    assertions.append(
        with_polarity(rng, f"(<= {rng.choice(terms)} {numeral(rng.randrange(-2, 5))})")
    )
    if index % 3 == 0:
        assertions.append(with_polarity(rng, f"(p {rng.choice(terms)})"))
    if index % 4 == 0:
        assertions.append(
            with_polarity(rng, f"(q {rng.choice(terms)} {rng.choice(terms)})")
        )
    if index % 5 == 0:
        assertions.append(f"(= (h b) {rng.choice(terms)})")
    return render(assertions)


def generate_case(rng: random.Random, index: int, shape: str, depth: int) -> str:
    if shape == "nonconvex":
        return generate_nonconvex_case(rng, index)
    if shape == "random":
        return generate_random_case(rng, index)
    if shape == "composed":
        return generate_composed_case(rng, index, depth)
    if shape == "extended":
        return generate_composed_case(rng, index, depth, extended=True)
    if index % 3 == 0:
        return generate_nonconvex_case(rng, index)
    # Half of the remaining cases use composed terms so the mixed shape, which
    # is what the gates run, covers nesting as well as the curated vocabulary.
    if index % 2 == 0:
        # Conforming on purpose: the gates run this shape through Dolmen model
        # validation, which refuses a file using div, mod or abs.
        return generate_composed_case(rng, index, depth)
    return generate_random_case(rng, index)


def run_solver(command: list[str], timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124
    except FileNotFoundError:
        return "MISSING", 127

    stdout = proc.stdout.strip()
    first = stdout.splitlines()[0] if stdout else ""
    # Statistic names such as fallback-unsupported are not solver diagnostics.
    combined = "\n".join(
        line for line in (proc.stdout + "\n" + proc.stderr).splitlines()
        if not line.lstrip().startswith(":")
    ).lower()
    if proc.returncode != 0 and first in {"sat", "unsat"}:
        return "CRASH", proc.returncode
    if "unsupported" in combined:
        return "UNSUPPORTED_INPUT", proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    if first in RESULTS:
        return first, proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, refs: list[str]) -> str:
    unusable = {"CRASH", "MISSING", "TIMEOUT", "UNSUPPORTED_INPUT", "UNKNOWN_OUTPUT"}
    concrete_refs = [result for result in refs if result in {"sat", "unsat"}]
    if ours in unusable:
        return ours
    if any(result in unusable for result in refs):
        return "REFERENCE_UNUSABLE"
    if len(set(concrete_refs)) > 1:
        return "REF_DISAGREEMENT"
    if not concrete_refs or ours == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == concrete_refs[0]:
        return "PASS"
    return "OUR_SAT_REF_UNSAT" if ours == "sat" else "OUR_UNSAT_REF_SAT"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", action="append", default=[])
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=20260901)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--shape",
        choices=["mixed", "nonconvex", "random", "composed", "extended"],
        default="mixed",
    )
    parser.add_argument(
        "--depth",
        type=int,
        default=3,
        help="UF application nesting depth for composed cases",
    )
    args = parser.parse_args()

    reference_names = args.ref or ["z3", "cvc5"]
    references: list[str] = []
    for reference in reference_names:
        resolved = shutil.which(reference)
        if resolved is None:
            print(f"required reference solver is unavailable: {reference}")
            return 2
        references.append(resolved)

    rng = random.Random(args.seed)
    cases_dir = args.out / "cases"
    bugs_dir = args.out / "bugs"
    cases_dir.mkdir(parents=True, exist_ok=True)
    bugs_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.out / "summary.csv"
    counts: dict[str, int] = {}
    fieldnames = ["case", "ours", "ours_code", "references", "outcome"]
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())
        for index in range(args.count):
            case_path = cases_dir / f"case-{index:05d}.smt2"
            case_path.write_text(
                generate_case(rng, index, args.shape, args.depth), encoding="utf-8"
            )
            ours, ours_code = run_solver(
                [args.solver, *args.solver_arg, str(case_path)], args.timeout
            )
            ref_results = [
                run_solver([reference, str(case_path)], args.timeout)[0]
                for reference in references
            ]
            outcome = classify(ours, ref_results)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "ours": ours,
                    "ours_code": ours_code,
                    "references": " ".join(ref_results),
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            if outcome not in {"PASS", "UNKNOWN_INVOLVED"}:
                shutil.copy2(case_path, bugs_dir / case_path.name)
            print(
                f"{case_path}: {outcome} ours={ours} refs={' '.join(ref_results)}",
                flush=True,
            )

    print(f"summary: {summary_path}")
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}")
    blocking = {
        "CRASH",
        "UNSUPPORTED_INPUT",
        "UNKNOWN_OUTPUT",
        "MISSING",
        "REFERENCE_UNUSABLE",
        "REF_DISAGREEMENT",
        "OUR_SAT_REF_UNSAT",
        "OUR_UNSAT_REF_SAT",
    }
    return 1 if any(counts.get(outcome, 0) for outcome in blocking) else 0


if __name__ == "__main__":
    raise SystemExit(main())
