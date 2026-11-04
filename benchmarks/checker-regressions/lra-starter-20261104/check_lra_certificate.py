#!/usr/bin/env python3
"""Independently check version-2 arithmetic witnesses; optionally solve its Boolean CNF.

Uses only Python's exact Fraction arithmetic. This checks the exported starting
CNF, not source-to-CNF conversion. --sat-solver confirms propositional UNSAT
with a separate SAT solver; it is not a proof-kernel check of that solver.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from fractions import Fraction
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse(source):
    tokens = []
    index = 0
    while index < len(source):
        char = source[index]
        if char.isspace():
            index += 1
        elif char == ";":
            end = source.find("\n", index)
            index = len(source) if end < 0 else end + 1
        elif char in "()":
            tokens.append(char)
            index += 1
        elif char == "|":
            end = source.find("|", index + 1)
            require(end >= 0, "unterminated quoted symbol")
            tokens.append(source[index : end + 1])
            index = end + 1
        else:
            end = index
            while (
                end < len(source)
                and not source[end].isspace()
                and source[end] not in "();"
            ):
                end += 1
            require(end > index and char != '"', "unsupported token")
            tokens.append(source[index:end])
            index = end
    roots = []
    stack = []
    for token in tokens:
        if token == "(":
            node = []
            (stack[-1] if stack else roots).append(node)
            stack.append(node)
        elif token == ")":
            require(bool(stack), "unmatched closing parenthesis")
            stack.pop()
        else:
            require(bool(stack), "token outside expression")
            stack[-1].append(token)
    require(not stack and len(roots) == 1, "expected one complete expression")
    return roots[0]


def spelling(node):
    return "(" + " ".join(map(spelling, node)) + ")" if isinstance(node, list) else node


def name(token):
    require(isinstance(token, str), "expected symbol")
    return token[1:-1] if token.startswith("|") and token.endswith("|") else token


def combine(left, right, scale=Fraction(1)):
    result = dict(left)
    for variable, value in right.items():
        result[variable] = result.get(variable, Fraction(0)) + scale * value
        if result[variable] == 0:
            del result[variable]
    return result


def affine(term, sorts):
    if isinstance(term, str):
        if re.fullmatch(r"-?[0-9]+(?:\.[0-9]+)?", term):
            return {}, Fraction(term)
        variable = name(term)
        require(
            sorts.get(variable) == "Real", "undeclared/non-Real arithmetic variable"
        )
        return {variable: Fraction(1)}, Fraction(0)
    require(bool(term), "empty arithmetic term")
    operator, *args = term
    if operator in ("+", "-"):
        require(bool(args), "missing arithmetic operands")
        result, constant = affine(args[0], sorts)
        if operator == "-" and len(args) == 1:
            return {v: -c for v, c in result.items()}, -constant
        for arg in args[1:]:
            coefficients, value = affine(arg, sorts)
            scale = Fraction(-1 if operator == "-" else 1)
            result = combine(result, coefficients, scale)
            constant += scale * value
        return result, constant
    require(operator in ("*", "/") and len(args) == 2, "unsupported affine operator")
    left, lc = affine(args[0], sorts)
    right, rc = affine(args[1], sorts)
    if operator == "/":
        require(not right and rc != 0, "division needs nonzero rational denominator")
        return {v: c / rc for v, c in left.items()}, lc / rc
    require(not left or not right, "nonlinear product")
    return combine({v: c * rc for v, c in left.items()}, right, lc), lc * rc


def inequality(literal, sorts):
    negative = isinstance(literal, list) and literal and literal[0] == "not"
    atom = literal[1] if negative and len(literal) == 2 else literal
    require(
        isinstance(atom, list) and len(atom) == 3 and atom[0] in ("<=", "<", ">=", ">"),
        "expected comparison literal",
    )
    operator, lhs, rhs = atom
    if negative:
        operator = {"<=": ">", "<": ">=", ">=": "<", ">": "<="}[operator]
    left, lc = affine(lhs, sorts)
    right, rc = affine(rhs, sorts)
    coefficients = combine(left, right, Fraction(-1))
    bound = rc - lc
    if operator in (">", ">="):
        coefficients = {v: -c for v, c in coefficients.items()}
        bound = -bound
    return coefficients, bound, operator in ("<", ">")


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
        root[1] == ["version", "2"] and root[2] == ["logic", "QF_LRA"],
        "wrong version/logic",
    )
    sorts = {}
    for declaration in root[3][1:]:
        require(
            len(declaration) == 4
            and declaration[0] == "declare-fun"
            and declaration[2] == []
            and declaration[3] in ("Real", "Bool"),
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
    entries = 0
    for conflict in root[5][1:]:
        require(conflict and conflict[0] == "conflict-lra", "invalid conflict")
        coefficients = {}
        rhs = Fraction(0)
        strict = False
        clause = []
        for pair in conflict[1:]:
            require(
                isinstance(pair, list) and len(pair) == 2, "invalid weighted premise"
            )
            literal, weight = pair
            # Public coefficient grammar is deliberately narrower than affine constants.
            require(
                (isinstance(weight, str) and re.fullmatch(r"[0-9]+", weight))
                or (
                    isinstance(weight, list)
                    and len(weight) == 3
                    and weight[0] == "/"
                    and all(
                        isinstance(x, str) and re.fullmatch(r"[0-9]+", x)
                        for x in weight[1:]
                    )
                    and int(weight[2]) > 0
                ),
                "invalid coefficient",
            )
            constant_terms, scale = affine(weight, {})
            require(not constant_terms and scale >= 0, "invalid coefficient")
            polynomial, bound, is_strict = inequality(literal, sorts)
            coefficients = combine(coefficients, polynomial, scale)
            rhs += scale * bound
            strict |= scale > 0 and is_strict
            clause.append(-abstract(literal))
            entries += 1
        require(
            not coefficients and (rhs < 0 or (rhs == 0 and strict)),
            "invalid weighted contradiction",
        )
        clauses.append(clause)
    if "true" in atoms:
        clauses.append([atoms["true"]])
    if "false" in atoms:
        clauses.append([-atoms["false"]])
    return {
        "conflicts": len(root[5]) - 1,
        "entries": entries,
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
        subprocess.TimeoutExpired,
    ) as error:
        parser.exit(1, f"certificate rejected: {error}\n")


if __name__ == "__main__":
    main()
