#!/usr/bin/env python3
"""Check version-3 integer cut certificates; optionally confirm Boolean UNSAT.

All arithmetic uses Python integers, including mathematical floor division.
The external SAT check trusts the SAT solver, not a proof kernel. The exported
CNF is the starting problem; source-to-CNF proof is outside this format.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from check_lra_certificate import name, parse, require, spelling

# The certificate arithmetic is arbitrary precision; process resource limits
# bound hostile inputs. Python 3.10 has no decimal-conversion digit limit.
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)


def add(left, right, scale=1):
    result = dict(left)
    for variable, coefficient in right.items():
        result[variable] = result.get(variable, 0) + scale * coefficient
        if result[variable] == 0:
            del result[variable]
    return result


def polynomial(term, sorts):
    if isinstance(term, str):
        if re.fullmatch(r"[0-9]+", term):
            return {}, int(term)
        variable = name(term)
        require(sorts.get(variable) == "Int", "undeclared/non-Int arithmetic variable")
        return {variable: 1}, 0
    require(isinstance(term, list) and bool(term), "empty arithmetic term")
    operator, *args = term
    if operator in ("+", "-"):
        require(bool(args), "missing arithmetic operands")
        result, constant = polynomial(args[0], sorts)
        if operator == "-" and len(args) == 1:
            return {v: -c for v, c in result.items()}, -constant
        for arg in args[1:]:
            coefficients, value = polynomial(arg, sorts)
            scale = -1 if operator == "-" else 1
            result = add(result, coefficients, scale)
            constant += scale * value
        return result, constant
    require(operator == "*" and len(args) == 2, "unsupported integer affine operator")
    left, lc = polynomial(args[0], sorts)
    right, rc = polynomial(args[1], sorts)
    require(not left or not right, "nonlinear product")
    return add({v: c * rc for v, c in left.items() if c * rc}, right, lc), lc * rc


def inequality(literal, sorts):
    negative = False
    atom = literal
    while isinstance(atom, list) and atom and atom[0] == "not":
        require(len(atom) == 2, "invalid negation")
        negative = not negative
        atom = atom[1]
    require(
        isinstance(atom, list) and len(atom) == 3 and atom[0] in ("<=", "<", ">=", ">"),
        "expected integer comparison literal",
    )
    operator, lhs, rhs = atom
    if negative:
        operator = {"<=": ">", "<": ">=", ">=": "<", ">": "<="}[operator]
    left, lc = polynomial(lhs, sorts)
    right, rc = polynomial(rhs, sorts)
    coefficients = add(left, right, -1)
    bound = rc - lc
    if operator in (">", ">="):
        coefficients = {v: -c for v, c in coefficients.items()}
        bound = -bound
    if operator in ("<", ">"):
        bound -= 1
    return coefficients, bound


def natural(token):
    require(
        isinstance(token, str) and re.fullmatch(r"[0-9]+", token),
        "expected nonnegative integer",
    )
    return int(token)


def validate(root):
    require(
        isinstance(root, list) and len(root) == 6 and root[0] == "primo-conflicts",
        "wrong container",
    )
    require(
        [section[0] for section in root[1:]]
        == ["version", "logic", "signature", "cnf", "conflicts"],
        "wrong sections",
    )
    require(
        root[1] == ["version", "3"] and root[2] == ["logic", "QF_LIA"],
        "wrong version/logic",
    )
    sorts = {}
    for declaration in root[3][1:]:
        require(
            isinstance(declaration, list)
            and len(declaration) == 4
            and declaration[0] == "declare-fun"
            and declaration[2] == []
            and declaration[3] in ("Int", "Bool"),
            "invalid declaration",
        )
        symbol = name(declaration[1])
        require(symbol not in sorts, "duplicate declaration")
        sorts[symbol] = declaration[3]
    atoms = {}

    def abstract(literal):
        if isinstance(literal, list) and literal and literal[0] == "not":
            require(len(literal) == 2, "invalid negation")
            return -abstract(literal[1])
        if literal in ("true", "false"):
            key = str(literal)
        elif isinstance(literal, str):
            require(sorts.get(name(literal)) == "Bool", "undeclared Boolean atom")
            key = "bool:" + name(literal)
        else:
            inequality(literal, sorts)
            key = spelling(literal)
        if key not in atoms:
            atoms[key] = len(atoms) + 1
        return atoms[key]

    clauses = []
    for clause in root[4][1:]:
        require(clause and clause[0] == "clause", "invalid clause")
        clauses.append([abstract(literal) for literal in clause[1:]])
    cuts = 0
    entries = 0
    for conflict in root[5][1:]:
        require(
            isinstance(conflict, list)
            and len(conflict) == 3
            and conflict[0] == "conflict-lia"
            and conflict[1]
            and conflict[1][0] == "premises"
            and conflict[2]
            and conflict[2][0] == "proof",
            "invalid LIA conflict",
        )
        rows = {}
        clause = []
        for premise in conflict[1][1:]:
            require(
                isinstance(premise, list) and len(premise) == 2, "invalid named premise"
            )
            label = name(premise[0])
            require(label not in rows, "duplicate proof label")
            rows[label] = inequality(premise[1], sorts)
            clause.append(-abstract(premise[1]))
            entries += 1
        proof = conflict[2][1:]
        require(bool(proof), "missing contradiction")
        for instruction in proof[:-1]:
            require(
                isinstance(instruction, list)
                and len(instruction) >= 3
                and instruction[0] == "cut",
                "invalid cut",
            )
            label = name(instruction[1])
            require(label not in rows, "duplicate proof label")
            divisor = natural(instruction[2])
            require(divisor > 0, "invalid cut divisor")
            coefficients = {}
            bound = 0
            for pair in instruction[3:]:
                require(
                    isinstance(pair, list) and len(pair) == 2,
                    "invalid weighted reference",
                )
                reference = name(pair[0])
                require(reference in rows, "forward/unknown proof reference")
                weight = natural(pair[1])
                polynomial_coefficients, rhs = rows[reference]
                coefficients = add(coefficients, polynomial_coefficients, weight)
                bound += weight * rhs
            require(
                all(c % divisor == 0 for c in coefficients.values()),
                "inexact coefficient division",
            )
            rows[label] = (
                {v: c // divisor for v, c in coefficients.items()},
                bound // divisor,
            )
            cuts += 1
        last = proof[-1]
        require(
            isinstance(last, list) and len(last) == 2 and last[0] == "contradiction",
            "missing final contradiction",
        )
        reference = name(last[1])
        require(reference in rows, "unknown contradiction reference")
        coefficients, bound = rows[reference]
        require(not coefficients and bound < 0, "invalid integer contradiction")
        clauses.append(clause)
    if "true" in atoms:
        clauses.append([atoms["true"]])
    if "false" in atoms:
        clauses.append([-atoms["false"]])
    return {
        "conflicts": len(root[5]) - 1,
        "entries": entries,
        "cuts": cuts,
        "variables": len(atoms),
    }, clauses


def check_file(path, sat_solver=None):
    report, clauses = validate(parse(Path(path).read_text()))
    if sat_solver:
        with tempfile.TemporaryDirectory() as temporary:
            cnf = Path(temporary) / "certificate.cnf"
            cnf.write_text(
                f"p cnf {report['variables']} {len(clauses)}\n"
                + "".join(" ".join(map(str, c)) + " 0\n" for c in clauses)
            )
            result = subprocess.run(
                [str(sat_solver), str(cnf)],
                capture_output=True,
                text=True,
                timeout=60,
                check=False,
            )
            require(
                result.returncode == 20
                and "s UNSATISFIABLE" in result.stdout.splitlines(),
                "propositional UNSAT not confirmed",
            )
        report["propositional_unsat"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("certificate", type=Path)
    parser.add_argument("--sat-solver", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(check_file(args.certificate, args.sat_solver), sort_keys=True))
    except (
        OSError,
        ValueError,
        TypeError,
        IndexError,
        KeyError,
        subprocess.TimeoutExpired,
    ) as error:
        parser.exit(1, f"certificate rejected: {error}\n")


if __name__ == "__main__":
    main()
