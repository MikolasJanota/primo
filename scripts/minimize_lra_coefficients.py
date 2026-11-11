#!/usr/bin/env python3
"""Simplify version-2 LRA weights or version-3 LIA cuts with exact replay.

Only weight/divisor byte spans change. The historical CLI name is retained.
See docs/leads/farkas-coefficient-minimization.md.
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

from check_lia_certificate import inequality as integer_inequality
from check_lia_certificate import natural
from check_lra_certificate import inequality, name
from farkas_coefficients import (
    better,
    cheap,
    cost,
    model_values,
    optimization_problem,
    valid,
    weight_text,
)
from lia_coefficients import (
    cheap_cut,
    cut_optimization_problem,
    cut_vector,
    replay_cut,
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

    def solve(self, index, premises, incumbent, build_program, names):
        args = self.args
        if not (args.optimizer or args.problems_dir):
            return None
        if (
            cost(incumbent)[0] < args.min_bits
            or len(premises) > args.max_premises
            or self.counts["queries"] >= args.max_queries
        ):
            self.counts["skipped"] += 1
            return None
        program = build_program()
        if program is None:
            self.counts["encoding_budget"] += 1
            return None
        directory = self.directory / f"lemma-{index:06d}"
        directory.mkdir()
        query = directory / "minimize.smt2"
        query.write_text(program)
        self.counts["queries"] += 1
        if not args.optimizer:
            return None
        remaining = args.total_seconds - self.seconds
        if remaining <= 0:
            self.counts["time_budget"] += 1
            return None
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
            return None
        try:
            output = (directory / "optimizer.out").read_text()
            if output.strip().splitlines()[0] in ("unknown", "unsat"):
                self.counts[output.strip().splitlines()[0]] += 1
                return None
            return model_values(output, names)
        except (ValueError, TypeError, KeyError, UnicodeError, IndexError):
            self.counts["invalid_model"] += 1
            return None

    def improve(self, index, premises, incumbent):
        args = self.args
        weights = self.solve(
            index,
            premises,
            incumbent,
            lambda: optimization_problem(
                premises, incumbent, args.max_bits, args.objective
            ),
            [f"w{i}" for i in range(len(premises))],
        )
        if weights is None:
            return incumbent
        if not valid(premises, weights):
            self.counts["invalid_witness"] += 1
            return incumbent
        weights = cheap(premises, weights, args.max_bits, args.objective)
        if better(weights, incumbent, args.objective):
            self.counts["adopted"] += 1
            return weights
        self.counts["not_better"] += 1
        return incumbent

    def improve_cut(self, index, premises, weights, divisor):
        args = self.args
        target = replay_cut(premises, weights, divisor)
        candidate = self.solve(
            index,
            premises,
            cut_vector(weights, divisor),
            lambda: cut_optimization_problem(
                premises, weights, divisor, args.max_bits, args.objective
            ),
            [f"w{i}" for i in range(len(premises))] + ["divisor"],
        )
        if candidate is None:
            return weights, divisor
        values, d = [int(w) for w in candidate[:-1]], int(candidate[-1])
        try:
            if replay_cut(premises, values, d) != target:
                raise ValueError("integer cut conclusion changed")
        except ValueError:
            self.counts["invalid_witness"] += 1
            return weights, divisor
        values, d = cheap_cut(premises, values, d, args.max_bits, args.objective)
        if better(cut_vector(values, d), cut_vector(weights, divisor), args.objective):
            self.counts["adopted"] += 1
            return values, d
        self.counts["not_better"] += 1
        return weights, divisor


def update_statistics(stats, weights, candidate, divisor=None, new_divisor=None):
    for label, vector, d in (
        ("before", weights, divisor),
        ("after", candidate, new_divisor),
    ):
        height, size, support = cost(list(map(Fraction, vector)))
        stats[label + "_weight_bytes"] += size
        stats[label + "_nonzeros"] += support
        if d is not None:
            height = max(height, d.bit_length())
            stats[label + "_divisor_bytes"] += len(str(d))
        stats[label + "_max_bits"] = max(stats[label + "_max_bits"], height)
    stats["entries"] += len(weights)


def integer_instruction(lex):
    """Read one proof instruction with exact numeral byte spans."""
    lex.take("(")
    kind = lex.take()[0]
    label = name(lex.take()[0])
    if kind == "contradiction":
        lex.take(")")
        return kind, label, None, [], [], []
    if kind != "cut":
        raise ValueError("expected cut or contradiction")
    token = lex.take()
    divisor = natural(token[0])
    spans = [(token[1], token[2])]
    references, weights = [], []
    while lex.peek() != ")":
        lex.take("(")
        references.append(name(lex.take()[0]))
        token = lex.take()
        weights.append(natural(token[0]))
        spans.append((token[1], token[2]))
        lex.take(")")
    lex.take(")")
    return kind, label, divisor, references, weights, spans


def transform_integer(lex, source, sorts, optimizer, stats, replace, record_start):
    """Two proof scans; VIPR §5 motivates releasing rows after their last use."""
    lex.take("(")
    lex.take("premises")
    rows = {}
    while lex.peek() != ")":
        premise, _, end = lex.expression()
        if end - record_start > 64 * 1024**2:
            raise ValueError("integer conflict exceeds 64 MiB")
        if not isinstance(premise, list) or len(premise) != 2:
            raise ValueError("invalid named integer premise")
        label = name(premise[0])
        if label in rows:
            raise ValueError("duplicate proof label")
        rows[label] = integer_inequality(premise[1], sorts)
    lex.take(")")
    proof_start = lex.take("(")[1]
    lex.take("proof")
    available, last_use = set(rows), {}
    step = 0
    # First scan retains label/index metadata, never the proof syntax or history.
    while True:
        kind, label, _, references, _, _ = integer_instruction(lex)
        if kind == "contradiction":
            if label not in available:
                raise ValueError("unknown contradiction reference")
            last_use[label] = step
            break
        if label in available:
            raise ValueError("duplicate proof label")
        for reference in references:
            if reference not in available:
                raise ValueError("forward/unknown proof reference")
            last_use[reference] = step
        available.add(label)
        step += 1
    lex.take(")")
    end = lex.take(")")[2]
    if end - record_start > 64 * 1024**2:
        raise ValueError("integer conflict exceeds 64 MiB")
    del available
    stats["peak_live_rows"] = max(stats["peak_live_rows"], len(rows))
    for label in list(rows):
        if label not in last_use:
            del rows[label]
            stats["released_rows"] += 1
    changed = False
    with source.open("rb") as stream:
        stream.seek(proof_start)
        replay = Lexer(stream)
        # Offsets returned by this lexer must still refer to the original file.
        replay.base = proof_start
        replay.take("(")
        replay.take("proof")
        step = 0
        while True:
            kind, label, divisor, references, weights, spans = integer_instruction(
                replay
            )
            if kind == "contradiction":
                coefficients, bound = rows[label]
                if coefficients or bound >= 0:
                    raise ValueError("invalid integer contradiction")
                break
            premises = [rows[reference] for reference in references]
            target = replay_cut(premises, weights, divisor)
            candidate, d = cheap_cut(
                premises,
                weights,
                divisor,
                optimizer.args.max_bits,
                optimizer.args.objective,
            )
            candidate, d = optimizer.improve_cut(stats["cuts"], premises, candidate, d)
            if replay_cut(premises, candidate, d) != target:
                raise ValueError("replacement integer cut conclusion changed")
            update_statistics(stats, weights, candidate, divisor, d)
            for span, value in zip(spans, [d, *candidate]):
                replace(*span, str(value))
            cut_changed = (candidate, d) != (weights, divisor)
            changed |= cut_changed
            stats["cuts"] += 1
            stats["changed_cuts"] += cut_changed
            # Drop borrowed operand rows before retiring their owning table entries.
            del premises
            for reference in set(references):
                if last_use[reference] == step:
                    del rows[reference]
                    stats["released_rows"] += 1
            if label in last_use:
                rows[label] = target
            else:
                stats["released_rows"] += 1
            stats["peak_live_rows"] = max(stats["peak_live_rows"], len(rows))
            step += 1
    return changed


def transform(source, destination, optimizer):
    """Copy input bytes except checked weights; hold only declarations and one lemma."""
    before = source.stat()
    stats = {
        "conflicts": 0,
        "changed_conflicts": 0,
        "entries": 0,
        "cuts": 0,
        "changed_cuts": 0,
        "peak_live_rows": 0,
        "released_rows": 0,
        "before_max_bits": 0,
        "after_max_bits": 0,
        "before_weight_bytes": 0,
        "after_weight_bytes": 0,
        "before_nonzeros": 0,
        "after_nonzeros": 0,
        "before_divisor_bytes": 0,
        "after_divisor_bytes": 0,
    }
    with source.open("rb") as stream, source.open("rb") as original:
        lex = Lexer(stream)

        def copy_to(end):
            while original.tell() < end:
                data = original.read(min(1024**2, end - original.tell()))
                if not data:
                    raise ValueError("input truncated during copying")
                destination.write(data)

        def replace(start, end, text):
            copy_to(start)
            destination.write(text.encode())
            original.seek(end)

        for token in ("(", "primo-conflicts", "(", "version"):
            lex.take(token)
        version = lex.take()[0]
        lex.take(")")
        lex.take("(")
        lex.take("logic")
        logic = lex.take()[0]
        if (version, logic) not in (("2", "QF_LRA"), ("3", "QF_LIA")):
            raise ValueError("expected version-2 QF_LRA or version-3 QF_LIA")
        stats["logic"] = logic
        lex.take(")")
        lex.take("(")
        lex.take("signature")
        sorts = {}
        while lex.peek() != ")":
            declaration, _, _ = lex.expression()
            if (
                not isinstance(declaration, list)
                or len(declaration) != 4
                or declaration[0] != "declare-fun"
                or declaration[2] != []
                or declaration[3]
                not in ("Bool", "Real" if logic == "QF_LRA" else "Int")
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
            if logic == "QF_LIA":
                lex.take("conflict-lia")
                stats["changed_conflicts"] += transform_integer(
                    lex, source, sorts, optimizer, stats, replace, record_start
                )
                stats["conflicts"] += 1
                continue
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
            candidate = cheap(
                premises, weights, optimizer.args.max_bits, optimizer.args.objective
            )
            candidate = optimizer.improve(stats["conflicts"], premises, candidate)
            if not valid(premises, candidate):
                raise ValueError("replacement weighted conflict is invalid")
            update_statistics(stats, weights, candidate)
            stats["conflicts"] += 1
            stats["changed_conflicts"] += candidate != weights
            for (start, end), weight in zip(spans, candidate):
                replace(start, end, weight_text(weight))
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
    parser.add_argument(
        "--objective",
        choices=("size", "support"),
        default="size",
        help="support prioritizes zero weights within the same size guards",
    )
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
                "objective": args.objective,
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
