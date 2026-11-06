#!/usr/bin/env python3
"""Stream an arithmetic certificate's declared CNF into its own SMT-LIB statement.

This adapter preserves the certificate verification boundary. It does not prove
that Primo's original SMT-LIB input implies the exported CNF. See
docs/reference/qf-lra-certificates.md, "Exported-CNF verification".
"""

from __future__ import annotations

import argparse
from pathlib import Path


def tokens(stream):
    """Keep only the current token, including quoted identifiers with newlines."""
    pending = ""
    while True:
        char = pending or stream.read(1)
        pending = ""
        if not char:
            return
        if char.isspace():
            continue
        if char == ";":
            while char and char != "\n":
                char = stream.read(1)
            continue
        if char in "()":
            yield char
            continue
        if char == '"':
            raise ValueError("strings are outside the arithmetic certificate grammar")
        token = [char]
        if char == "|":
            while True:
                char = stream.read(1)
                if not char or char == "\\":
                    raise ValueError("invalid quoted certificate identifier")
                token.append(char)
                if char == "|":
                    break
        else:
            while True:
                char = stream.read(1)
                if not char or char.isspace() or char in "();":
                    pending = char
                    break
                if char in '|"':
                    raise ValueError("invalid certificate token")
                token.append(char)
        yield "".join(token)


def write_cnf_statement(source, output, expected_logic):
    """Copy declarations/clauses only; stream and discard the conflict section."""
    lex = iter(tokens(source))

    def take():
        try:
            return next(lex)
        except StopIteration as error:
            raise ValueError("incomplete certificate") from error

    def expect(wanted):
        actual = take()
        if actual != wanted:
            raise ValueError(f"expected {wanted!r}, got {actual!r}")

    for token in ("(", "primo-conflicts", "(", "version"):
        expect(token)
    version = take()
    for token in (")", "(", "logic"):
        expect(token)
    logic = take()
    if logic != expected_logic or (logic, version) not in (
        ("QF_LRA", "2"),
        ("QF_LIA", "3"),
    ):
        raise ValueError("expected a native arithmetic certificate with matching logic")
    for token in (")", "(", "signature"):
        expect(token)
    output.write(f"(set-logic {logic})\n")
    declarations = 0
    while True:
        token = take()
        if token == ")":
            break
        if token != "(":
            raise ValueError("expected a declaration")
        expect("declare-fun")
        symbol = take()
        if symbol in ("(", ")"):
            raise ValueError("invalid declaration name")
        expect("(")
        expect(")")
        sort = take()
        if sort not in ("Bool", "Real" if logic == "QF_LRA" else "Int"):
            raise ValueError("invalid arithmetic declaration sort")
        expect(")")
        output.write(f"(declare-fun {symbol} () {sort})\n")
        declarations += 1
    expect("(")
    expect("cnf")
    clauses = 0
    while True:
        token = take()
        if token == ")":
            break
        if token != "(":
            raise ValueError("expected a clause")
        expect("clause")
        token = take()
        if token == ")":
            output.write("(assert false)\n")
        else:
            # A false disjunct preserves the clause and gives a unit clause
            # a binary `or`, as required by SMT-LIB's left-associative form.
            output.write("(assert (or false")
            depth = 0
            while token != ")" or depth:
                output.write(" " + token)
                depth += (token == "(") - (token == ")")
                token = take()
            output.write("))\n")
        clauses += 1
    expect("(")
    expect("conflicts")
    depth = 1
    while depth:
        token = take()
        depth += (token == "(") - (token == ")")
    expect(")")
    if next(lex, None) is not None:
        raise ValueError("trailing certificate tokens")
    output.write("(check-sat)\n")
    return {"logic": logic, "declarations": declarations, "clauses": clauses}


def convert_certificate(certificate, target, logic):
    """Clean up handled failures; never overwrite an existing target."""
    with target.open("x") as output:
        try:
            with certificate.open() as source:
                return write_cnf_statement(source, output, logic)
        except BaseException:
            target.unlink()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("certificate", type=Path)
    parser.add_argument("statement", type=Path)
    parser.add_argument("--logic", choices=("QF_LRA", "QF_LIA"), required=True)
    args = parser.parse_args()
    convert_certificate(args.certificate, args.statement, args.logic)


if __name__ == "__main__":
    main()
