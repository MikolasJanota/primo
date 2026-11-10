#!/usr/bin/env python3
"""Simplify version-2 LRA weights, optionally using separate integer optimization.

Only weight byte spans change. See docs/leads/farkas-coefficient-minimization.md.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import signal
import tempfile
import time
from collections import Counter
from fractions import Fraction
from pathlib import Path

from check_lra_certificate import inequality, name
from farkas_coefficients import (
    better,
    cheap,
    cost,
    model_weights,
    optimization_problem,
    valid,
    weight_text,
)
from micromath_stress import digest, invoke

ATOM = re.compile(rb'[^\s();|"\\]+')
SPACE = re.compile(rb"\s+")


class Lexer:
    """Chunked tokens with original byte offsets; only a current token can span blocks."""

    def __init__(self, stream):
        self.stream = stream
        self.data = b""
        self.index = self.base = 0
        self.saved = None

    def fill(self):
        if self.index == len(self.data):
            self.base += len(self.data)
            self.data = self.stream.read(65536)
            self.index = 0
        return bool(self.data)

    def next(self):
        if self.saved is not None:
            token, self.saved = self.saved, None
            return token
        while self.fill():
            whitespace = SPACE.match(self.data, self.index)
            if whitespace:
                self.index = whitespace.end()
                continue
            if self.data[self.index] == ord(";"):
                while self.fill():
                    end = self.data.find(b"\n", self.index)
                    self.index = len(self.data) if end < 0 else end + 1
                    if end >= 0:
                        break
                continue
            start = self.base + self.index
            char = self.data[self.index : self.index + 1]
            if char in (b"(", b")"):
                self.index += 1
                return char.decode(), start, start + 1
            pieces = []
            token_bytes = 0
            if char == b"|":
                pieces.append(char)
                self.index += 1
                while self.fill():
                    end = self.data.find(b"|", self.index)
                    part = (
                        self.data[self.index :]
                        if end < 0
                        else self.data[self.index : end + 1]
                    )
                    if b"\\" in part:
                        raise ValueError("backslash in quoted symbol")
                    pieces.append(part)
                    token_bytes += len(part)
                    if token_bytes > 1024**2:
                        raise ValueError("certificate token exceeds 1 MiB")
                    self.index += len(part)
                    if end >= 0:
                        return b"".join(pieces).decode(), start, self.base + self.index
                raise ValueError("unterminated quoted symbol")
            while self.fill():
                atom = ATOM.match(self.data, self.index)
                if atom is None:
                    break
                pieces.append(atom[0])
                token_bytes += len(atom[0])
                if token_bytes > 1024**2:
                    raise ValueError("certificate token exceeds 1 MiB")
                self.index = atom.end()
                if self.index < len(self.data):
                    break
            if not pieces:
                raise ValueError("unsupported certificate token")
            return b"".join(pieces).decode(), start, self.base + self.index
        return None

    def peek(self):
        if self.saved is None:
            self.saved = self.next()
        return None if self.saved is None else self.saved[0]

    def take(self, expected=None):
        token = self.next()
        if token is None or (expected is not None and token[0] != expected):
            raise ValueError(f"expected {expected or 'token'}, got {token}")
        return token

    def expression(self):
        first = self.take()
        if first[0] != "(":
            if first[0] == ")":
                raise ValueError("unexpected closing parenthesis")
            return first[0], first[1], first[2]
        stack = [[]]
        while stack:
            token = self.take()
            if token[2] - first[1] > 64 * 1024**2:
                raise ValueError("certificate expression exceeds 64 MiB")
            if token[0] == "(":
                stack.append([])
            elif token[0] == ")":
                node = stack.pop()
                if not stack:
                    return node, first[1], token[2]
                stack[-1].append(node)
            else:
                stack[-1].append(token[0])
        raise ValueError("incomplete expression")

    def skip_body(self):
        depth = 1
        while depth:
            token = self.take()
            depth += (token[0] == "(") - (token[0] == ")")
        return token[2]


def coefficient(node):
    if isinstance(node, str) and re.fullmatch(r"[0-9]+", node):
        return Fraction(int(node))
    if (
        isinstance(node, list)
        and len(node) == 3
        and node[0] == "/"
        and all(isinstance(x, str) and re.fullmatch(r"[0-9]+", x) for x in node[1:])
        and int(node[2]) > 0
    ):
        return Fraction(int(node[1]), int(node[2]))
    raise ValueError("invalid nonnegative rational coefficient")


class Optimizer:
    def __init__(self, args, directory):
        self.args, self.directory = args, directory
        self.counts = Counter()
        self.seconds = 0.0

    def improve(self, index, premises, incumbent):
        args = self.args
        if not (args.optimizer or args.problems_dir):
            return incumbent
        if (
            cost(incumbent)[0] < args.min_bits
            or len(premises) > args.max_premises
            or self.counts["queries"] >= args.max_queries
        ):
            self.counts["skipped"] += 1
            return incumbent
        program = optimization_problem(premises, incumbent, args.max_bits)
        if program is None:
            self.counts["encoding_budget"] += 1
            return incumbent
        directory = self.directory / f"lemma-{index:06d}"
        directory.mkdir()
        query = directory / "minimize.smt2"
        query.write_text(program)
        self.counts["queries"] += 1
        if not args.optimizer:
            return incumbent
        remaining = args.total_seconds - self.seconds
        if remaining <= 0:
            self.counts["time_budget"] += 1
            return incumbent
        stage = invoke(
            [
                "prlimit",
                "--as=3221225472",
                "--fsize=16777216",
                "--",
                args.optimizer,
                *args.optimizer_arg,
                str(query),
            ],
            directory,
            directory / "optimizer",
            min(args.call_seconds, remaining),
        )
        self.seconds += stage["seconds"]
        self.counts[stage["state"]] += 1
        if stage["state"] != "ok":
            return incumbent
        try:
            output = (directory / "optimizer.out").read_text()
            if output.strip().splitlines()[0] in ("unknown", "unsat"):
                self.counts[output.strip().splitlines()[0]] += 1
                return incumbent
            weights = model_weights(output, len(premises))
            if not valid(premises, weights):
                self.counts["invalid_witness"] += 1
                return incumbent
        except (ValueError, TypeError, KeyError, UnicodeError, IndexError):
            self.counts["invalid_model"] += 1
            return incumbent
        weights = cheap(premises, weights, args.max_bits)
        if better(weights, incumbent):
            self.counts["adopted"] += 1
            return weights
        self.counts["not_better"] += 1
        return incumbent


def transform(source, destination, optimizer):
    """Copy input bytes except checked weights; hold only declarations and one lemma."""
    before = source.stat()
    stats = {
        "conflicts": 0,
        "changed_conflicts": 0,
        "entries": 0,
        "before_max_bits": 0,
        "after_max_bits": 0,
        "before_weight_bytes": 0,
        "after_weight_bytes": 0,
        "before_nonzeros": 0,
        "after_nonzeros": 0,
    }
    with source.open("rb") as stream, source.open("rb") as original:
        lex = Lexer(stream)

        def copy_to(end):
            while original.tell() < end:
                data = original.read(min(1024**2, end - original.tell()))
                if not data:
                    raise ValueError("input truncated during copying")
                destination.write(data)

        for token in (
            "(",
            "primo-conflicts",
            "(",
            "version",
            "2",
            ")",
            "(",
            "logic",
            "QF_LRA",
            ")",
            "(",
            "signature",
        ):
            lex.take(token)
        sorts = {}
        while lex.peek() != ")":
            declaration, _, _ = lex.expression()
            if (
                not isinstance(declaration, list)
                or len(declaration) != 4
                or declaration[0] != "declare-fun"
                or declaration[2] != []
                or declaration[3] not in ("Bool", "Real")
                or name(declaration[1]) in sorts
            ):
                raise ValueError("invalid declaration")
            sorts[name(declaration[1])] = declaration[3]
        lex.take(")")
        lex.take("(")
        lex.take("cnf")
        while lex.peek() != ")":
            lex.take("(")
            lex.take("clause")
            lex.skip_body()
        copy_to(lex.take(")")[2])
        lex.take("(")
        lex.take("conflicts")
        while lex.peek() != ")":
            record_start = lex.take("(")[1]
            lex.take("conflict-lra")
            premises, weights, spans = [], [], []
            while lex.peek() != ")":
                lex.take("(")
                literal, _, _ = lex.expression()
                weight, start, end = lex.expression()
                if end - record_start > 64 * 1024**2:
                    raise ValueError("weighted conflict exceeds 64 MiB")
                lex.take(")")
                premises.append(inequality(literal, sorts))
                weights.append(coefficient(weight))
                spans.append((start, end))
            lex.take(")")
            candidate = cheap(premises, weights, optimizer.args.max_bits)
            candidate = optimizer.improve(stats["conflicts"], premises, candidate)
            if not valid(premises, candidate):
                raise ValueError("replacement weighted conflict is invalid")
            for label, vector in (("before", weights), ("after", candidate)):
                height, size, support = cost(vector)
                stats[label + "_max_bits"] = max(stats[label + "_max_bits"], height)
                stats[label + "_weight_bytes"] += size
                stats[label + "_nonzeros"] += support
            stats["conflicts"] += 1
            stats["entries"] += len(weights)
            stats["changed_conflicts"] += candidate != weights
            for (start, end), weight in zip(spans, candidate):
                copy_to(start)
                destination.write(weight_text(weight).encode())
                original.seek(end)
        lex.take(")")
        lex.take(")")
        if lex.next() is not None:
            raise ValueError("trailing certificate expressions")
        copy_to(before.st_size)
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("input changed during coefficient processing")
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--optimizer", help="Optimize-compatible executable accepting a .smt2 file"
    )
    parser.add_argument("--optimizer-arg", action="append", default=[])
    parser.add_argument("--problems-dir", type=Path)
    parser.add_argument("--call-seconds", type=float, default=1)
    parser.add_argument("--total-seconds", type=float, default=30)
    parser.add_argument("--max-queries", type=int, default=32)
    parser.add_argument("--max-premises", type=int, default=256)
    parser.add_argument("--max-bits", type=int, default=4096)
    parser.add_argument("--min-bits", type=int, default=8)
    args = parser.parse_args()
    args.source, args.output = args.source.resolve(), args.output.resolve()
    if args.problems_dir:
        args.problems_dir = args.problems_dir.resolve()
    if args.report:
        args.report = args.report.resolve()
        if args.report in (args.source, args.output):
            parser.error("report must differ from source and output")
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must differ")
    if (
        min(
            args.call_seconds,
            args.total_seconds,
            args.max_queries,
            args.max_premises,
            args.max_bits,
        )
        <= 0
        or args.min_bits < 0
    ):
        parser.error("positive budgets and nonnegative min-bits required")
    if not all(math.isfinite(t) for t in (args.call_seconds, args.total_seconds)):
        parser.error("optimizer time budgets must be finite")
    if args.optimizer:
        args.optimizer = shutil.which(args.optimizer)
        if args.optimizer is None:
            parser.error("optimizer executable not found")
        args.optimizer = str(Path(args.optimizer).resolve())
    if args.problems_dir:
        args.problems_dir.mkdir(parents=True, exist_ok=False)

    def interrupted(_signal, _frame):
        raise InterruptedError("interrupted")

    for caught in (signal.SIGTERM, signal.SIGINT):
        signal.signal(caught, interrupted)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(
        prefix=".lra-coefficients-", dir=args.output.parent
    ) as work:
        work = Path(work)
        optimizer = Optimizer(args, args.problems_dir or work)
        temporary = work / "output.sexp"
        with temporary.open("wb") as destination:
            stats = transform(args.source, destination, optimizer)
            destination.flush()
            os.fsync(destination.fileno())
        stats.update(
            {
                "optimizer": dict(optimizer.counts),
                "optimizer_seconds": optimizer.seconds,
                "seconds": time.monotonic() - started,
                "input_sha256": digest(args.source),
                "output_sha256": digest(temporary),
                "input_bytes": args.source.stat().st_size,
                "output_bytes": temporary.stat().st_size,
            }
        )
        if args.report:
            args.report.write_text(json.dumps(stats, indent=2) + "\n")
        os.replace(temporary, args.output)
    print(json.dumps(stats))


if __name__ == "__main__":
    main()
