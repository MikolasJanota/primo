#!/usr/bin/env python3
"""Generate QF_UFLRA smoke cases and compare Primo with Z3."""

from __future__ import annotations

import argparse
import csv
import os
import random
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}


EUF_ATOMS = [
    "(= a b)",
    "(= b c)",
    "(= a c)",
    "(= (f a) (f b))",
    "(= (f b) (f c))",
    "(p a)",
    "(p b)",
]

LRA_ATOMS = [
    "(< 0 x)",
    "(< x 4)",
    "(< y 3)",
    "(< (+ x y) 5)",
    "(<= (- x y) 2)",
    "(< x 0)",
    "(< 2 x)",
    "(< 1 y)",
    "(< y 1)",
]


def atom_with_polarity(rng: random.Random, atom: str) -> str:
    if rng.randrange(2) == 0:
        return atom
    return f"(not {atom})"


PURIFIED_EUF_ATOMS = [
    "(= a b)",
    "(= b c)",
    "(= (h a) (h b))",
    "(p a)",
    "(p (h b))",
]

PURIFIED_LRA_ATOMS = [
    "(< (r a) 3)",
    "(< 0 (r b))",
    "(< (+ (r a) x) 5)",
    "(<= (- (s (h a)) y) 2)",
    "(< (+ (r a) (r b)) 4)",
    "(< (s c) (r a))",
    "(< x (r c))",
]


SHARED_REAL_EUF_ATOMS = [
    "(p x)",
    "(p y)",
    "(p z)",
    "(q x y)",
    "(q y x)",
]

SHARED_REAL_LRA_ATOMS = [
    "(<= x y)",
    "(<= y x)",
    "(< x y)",
    "(< y z)",
    "(<= z x)",
    "(< 0 x)",
    "(< y 4)",
    "(< z 5)",
]

SCALED_LRA_ATOMS = [
    "(<= (+ (* 2 x) (* 2 y)) (* 2 z))",
    "(<= (+ (* 4 x) (* 4 y)) (* 4 z))",
    "(< (* (- 3) x) (* (- 3) y))",
    "(< (* (/ 2 3) x) (* (/ 4 5) y))",
    "(= (+ (* 2 x) (* 2 y)) (* 2 z))",
    "(= (* 6 z) (+ (* 6 x) (* 6 y)))",
    "(<= (+ x (* (- 1) x)) 0)",
    "(<= (* 2 x) 2)",
    "(<= (* 4 x) 8)",
]


NONCONSTANT_REAL_EUF_ATOMS = [
    "(p (+ x 1))",
    "(p (+ y 1))",
    "(p (* 2 x))",
    "(p 0)",
    "(q (+ x 1) y)",
    "(q (+ x y) z)",
    "(q (* 3 z) (+ x 2))",
]

NONCONSTANT_REAL_LRA_ATOMS = [
    "(<= (+ x 1) y)",
    "(<= y (+ x 1))",
    "(< (+ x y) 5)",
    "(< 0 x)",
    "(< y 4)",
    "(< z 3)",
    "(<= (+ (* 2 x) y) 7)",
]

BOOL_ARGUMENT_EUF_ATOMS = [
    "(= (f p) (f q))",
    "(= (f q) (f r))",
    "(g p q)",
    "(g q p)",
    "(f (and p q))",
    "(f (or q r))",
    "(= (f (not p)) (f (not q)))",
    "(= (f (g p q)) (f (g q p)))",
    "(= (f (ite p q r)) (f (ite q q r)))",
    "(= (k (f p) q) (k (f q) q))",
    "(= (rr p) (rr q))",
    "(= (f p) true)",
    "(= (f q) false)",
    "(or (= p q) (not (= (f p) (f q))))",
]

BOOL_ARGUMENT_LRA_ATOMS = [
    "(< 0 x)",
    "(< x 4)",
    "(<= (- x y) 2)",
    "(< (+ x y) 5)",
    "(< y 3)",
]


def generate_separable_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 10 == 0:
        assertions.extend(["(= a b)", "(= b c)", "(not (= a c))"])
    if index % 14 == 0:
        assertions.extend(["(< x 0)", "(< 2 x)"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun f (U) U)
(declare-fun p (U) Bool)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def generate_purified_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(PURIFIED_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(PURIFIED_LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 9 == 0:
        assertions.extend(["(= a b)", "(< (r a) 0)", "(< 1 (r b))"])
    if index % 13 == 0:
        assertions.extend(["(not (= a b))", "(= (h a) (h b))"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun h (U) U)
(declare-fun p (U) Bool)
(declare-fun r (U) Real)
(declare-fun s (U) Real)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def generate_shared_real_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(SHARED_REAL_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(SHARED_REAL_LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 8 == 0:
        assertions.extend(["(<= x y)", "(<= y x)", "(p x)", "(not (p y))"])
    if index % 11 == 0:
        assertions.extend(["(< x y)", "(p x)", "(not (p y))"])
    if index % 17 == 0:
        assertions.extend(["(<= y z)", "(<= z y)", "(q x y)", "(not (q x z))"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun p (Real) Bool)
(declare-fun q (Real Real) Bool)
{body}
(check-sat)
"""


def generate_scaled_arithmetic_case(rng: random.Random, index: int) -> str:
    atoms = rng.sample(SCALED_LRA_ATOMS, 3 + rng.randrange(4))
    assertions = [atom_with_polarity(rng, atom) for atom in atoms]
    assertions.extend([atom_with_polarity(rng, "(p x)"), atom_with_polarity(rng, "(p y)")])

    # Equivalent positive scales must remain one Boolean atom even under
    # opposite polarity. Offset equality exercises cancellation before the
    # Nelson-Oppen paired-bound producer.
    if index % 5 == 0:
        assertions.extend(
            [
                "(<= (+ (* 2 x) (* 2 y)) (* 2 z))",
                "(not (<= (+ (* 4 x) (* 4 y)) (* 4 z)))",
            ]
        )
    if index % 7 == 0:
        assertions.extend(["(= (+ x 1) (+ y 1))", "(p x)", "(not (p y))"])
    if index % 11 == 0:
        assertions.extend(["(<= (* (- 2) x) (* (- 2) y))", "(< y x)"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun p (Real) Bool)
{body}
(check-sat)
"""


def generate_nonconstant_real_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(NONCONSTANT_REAL_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(NONCONSTANT_REAL_LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 8 == 0:
        assertions.extend(["(<= (+ x 1) y)", "(<= y (+ x 1))", "(p (+ x 1))", "(not (p y))"])
    if index % 13 == 0:
        assertions.extend(["(< x y)", "(p (+ x 1))", "(not (p (+ y 1)))"])
    if index % 17 == 0:
        assertions.extend(["(q (+ x y) z)", "(not (q (+ x y) z))"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun p (Real) Bool)
(declare-fun q (Real Real) Bool)
{body}
(check-sat)
"""


def generate_bool_argument_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(3)
    euf_atoms = rng.sample(BOOL_ARGUMENT_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(BOOL_ARGUMENT_LRA_ATOMS, lra_count)

    # Keep every case in the supported mixed-EUF fragment, even when the
    # random sample happens to select only equality atoms.
    assertions = ["(f p)"]
    assertions.extend(atom_with_polarity(rng, atom) for atom in euf_atoms)
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    # Force congruence and transitive Boolean-equality shapes regularly enough
    # that a random smoke run cannot accidentally omit the critical cases.
    if index % 5 == 0:
        assertions.extend(["(= p q)", "(not (= (f p) (f q)))"])
    if index % 7 == 0:
        assertions.extend(["(= p q)", "(= q r)", "(not (= (g p p) (g r r)))"])
    if index % 11 == 0:
        assertions.extend(["(= (and p q) (and q p))", "(f (and p q))"])
    if index % 13 == 0:
        assertions.extend(
            [
                "(= p q)",
                "(= q r)",
                "(not (= (f p) (f r)))",
                "(or (not p) q)",
                "(or (not q) r)",
                "(or (not r) p)",
            ]
        )
    if index % 17 == 0:
        assertions.extend(
            [
                "(or (not p) q)",
                "(or (not q) r)",
                "(or (not r) p)",
                "(not (= (f p) (f r)))",
            ]
        )

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(declare-sort U 0)
(declare-fun k (Bool Bool) U)
(declare-fun rr (Bool) Real)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def recursive_bool_term(rng: random.Random, depth: int) -> str:
    variables = ["b1", "b2", "b3", "b4", "b5"]
    if depth <= 0 or rng.randrange(4) == 0:
        return rng.choice(variables)

    choice = rng.randrange(8)
    if choice == 0:
        return f"(not {recursive_bool_term(rng, depth - 1)})"
    if choice == 1:
        lhs = recursive_bool_term(rng, depth - 1)
        rhs = recursive_bool_term(rng, depth - 1)
        return f"(and {lhs} {rhs})"
    if choice == 2:
        lhs = recursive_bool_term(rng, depth - 1)
        rhs = recursive_bool_term(rng, depth - 1)
        return f"(or {lhs} {rhs})"
    if choice == 3:
        lhs = recursive_bool_term(rng, depth - 1)
        rhs = recursive_bool_term(rng, depth - 1)
        return f"(=> {lhs} {rhs})"
    if choice == 4:
        lhs = recursive_bool_term(rng, depth - 1)
        rhs = recursive_bool_term(rng, depth - 1)
        return f"(xor {lhs} {rhs})"
    if choice == 5:
        return f"(f {recursive_bool_term(rng, depth - 1)})"
    if choice == 6:
        lhs = recursive_bool_term(rng, depth - 1)
        rhs = recursive_bool_term(rng, depth - 1)
        return f"(g {lhs} {rhs})"
    condition = recursive_bool_term(rng, depth - 1)
    then_term = recursive_bool_term(rng, depth - 1)
    else_term = recursive_bool_term(rng, depth - 1)
    return f"(ite {condition} {then_term} {else_term})"


def generate_recursive_bool_case(rng: random.Random, index: int) -> str:
    terms = [recursive_bool_term(rng, 3) for _ in range(6)]
    assertions = [
        f"(= (f {terms[0]}) (f {terms[1]}))",
        f"(not (= (f {terms[2]}) (f {terms[3]})))",
        f"(g {terms[4]} {terms[5]})",
        f"(or (not (f {terms[0]})) (f {terms[2]}))",
        f"(= (f (ite b1 {terms[1]} {terms[2]})) (f (ite b1 {terms[1]} {terms[2]})))",
    ]
    if index % 3 == 0:
        assertions.extend(
            [
                "(or (not b1) b2)",
                "(or (not b2) b3)",
                "(or (not b3) b4)",
                "(or (not b4) b1)",
                "(not (= (f b1) (f b4)))",
            ]
        )
    if index % 5 == 0:
        assertions.extend(
            [
                "(= b1 b2)",
                "(= b2 b3)",
                "(= b3 b4)",
                "(not (= (g b1 b5) (g b4 b5)))",
            ]
        )
    assertions.extend(
        [
            atom_with_polarity(rng, "(< 0 x)"),
            atom_with_polarity(rng, "(< y 4)"),
            atom_with_polarity(rng, "(< (+ x y) 5)"),
        ]
    )
    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun b1 () Bool)
(declare-fun b2 () Bool)
(declare-fun b3 () Bool)
(declare-fun b4 () Bool)
(declare-fun b5 () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def generate_case(rng: random.Random, index: int, shape: str) -> str:
    if shape == "separable":
        return generate_separable_case(rng, index)
    if shape == "purified":
        return generate_purified_case(rng, index)
    if shape == "shared-real":
        return generate_shared_real_case(rng, index)
    if shape == "nonconstant-real":
        return generate_nonconstant_real_case(rng, index)
    if shape == "bool-arguments":
        return generate_bool_argument_case(rng, index)
    if shape == "recursive-bool":
        return generate_recursive_bool_case(rng, index)
    if shape == "scaled-arithmetic":
        return generate_scaled_arithmetic_case(rng, index)
    choice = rng.randrange(7)
    if choice == 0:
        return generate_separable_case(rng, index)
    if choice == 1:
        return generate_purified_case(rng, index)
    if choice == 2:
        return generate_shared_real_case(rng, index)
    if choice == 3:
        return generate_nonconstant_real_case(rng, index)
    if choice == 4:
        return generate_bool_argument_case(rng, index)
    if choice == 5:
        return generate_recursive_bool_case(rng, index)
    return generate_scaled_arithmetic_case(rng, index)


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

    first = proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""
    # Statistic names such as fallback-unsupported are not solver diagnostics.
    text = "\n".join(
        line for line in (proc.stdout + "\n" + proc.stderr).splitlines()
        if not line.lstrip().startswith(":")
    ).lower()
    if proc.returncode != 0 and first in {"sat", "unsat"}:
        return "CRASH", proc.returncode
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT", proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    if first in RESULTS:
        return first, proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, ref: str) -> str:
    if ours in {
        "UNSUPPORTED_INPUT",
        "CRASH",
        "MISSING",
        "TIMEOUT",
        "UNKNOWN_OUTPUT",
    }:
        return ours
    if ref in {"MISSING", "TIMEOUT", "CRASH", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT"}:
        return "REFERENCE_UNUSABLE"
    if ours == "unknown" or ref == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == ref:
        return "PASS"
    if ours == "sat" and ref == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and ref == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--ref", default="z3")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20260730)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, default=Path("fuzz-results/qf_uflra-smoke"))
    parser.add_argument(
        "--shape",
        choices=[
            "separable",
            "purified",
            "shared-real",
            "nonconstant-real",
            "bool-arguments",
            "recursive-bool",
            "scaled-arithmetic",
            "mixed",
        ],
        default="separable",
    )
    parser.add_argument("--solver-arg", action="append", default=[])
    args = parser.parse_args()

    ref = shutil.which(args.ref) or args.ref
    rng = random.Random(args.seed)
    cases_dir = args.out / "cases"
    cases_dir.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    summary_path = args.out / "summary.csv"
    fieldnames = [
        "case",
        "ours",
        "ours_code",
        "reference",
        "reference_code",
        "outcome",
    ]
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())

        for index in range(args.count):
            case_path = cases_dir / f"case-{index:04d}.smt2"
            case_path.write_text(generate_case(rng, index, args.shape), encoding="utf-8")
            ours, ours_code = run_solver(
                [args.solver, *args.solver_arg, str(case_path)], args.timeout
            )
            reference, ref_code = run_solver([ref, str(case_path)], args.timeout)
            outcome = classify(ours, reference)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "ours": ours,
                    "ours_code": ours_code,
                    "reference": reference,
                    "reference_code": ref_code,
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            print(f"{case_path}: {outcome} ours={ours} ref={reference}", flush=True)

    print(f"summary: {summary_path}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)

    bad = {
        "UNSUPPORTED_INPUT",
        "CRASH",
        "MISSING",
        "OUR_SAT_REF_UNSAT",
        "OUR_UNSAT_REF_SAT",
        "UNKNOWN_OUTPUT",
        "REFERENCE_UNUSABLE",
    }
    return 1 if any(counts.get(outcome, 0) for outcome in bad) else 0


if __name__ == "__main__":
    raise SystemExit(main())
