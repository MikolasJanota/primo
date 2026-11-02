#!/usr/bin/env python3
"""Black-box export regressions and an optional reference-checked CNF fuzz gate."""

from __future__ import annotations

import argparse
import json
import random
import re
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

PRELUDE = """(set-logic QF_UF)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun f (U) U)
(declare-fun p (U) Bool)
(declare-fun h (Bool) U)
"""
CONGRUENCE = "(assert (= a b))(assert (not (= (f a) (f b))))(check-sat)"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse(text: str) -> list:
    roots: list = []
    stack: list[list] = []
    for token in re.findall(r"\|[^|]*\||[()]|[^\s()]+", text):
        if token == "(":
            node: list = []
            (stack[-1] if stack else roots).append(node)
            stack.append(node)
        elif token == ")":
            require(bool(stack), "unexpected closing parenthesis")
            stack.pop()
        else:
            require(bool(stack), "token outside expression")
            stack[-1].append(token)
    require(not stack and len(roots) == 1, "expected one complete expression")
    return roots[0]


def text(node: list | str) -> str:
    return "(" + " ".join(map(text, node)) + ")" if isinstance(node, list) else node


def complement(literal: list | str) -> list | str:
    return (
        literal[1]
        if isinstance(literal, list) and literal[0] == "not"
        else ["not", literal]
    )


def invoke(
    command: list[str], source: str, timeout: int = 10
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        input=source,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def export(
    solver: str, source: str, output: Path, *options: str
) -> subprocess.CompletedProcess[str]:
    return invoke([solver, "--quiet", "--export-cnf", str(output), *options], source)


def document(path: Path) -> list:
    root = parse(path.read_text())
    require(root[0] == "primo-conflicts", "wrong document tag")
    require(
        [x[0] for x in root[1:]]
        == ["version", "logic", "signature", "cnf", "conflicts"],
        "wrong sections",
    )
    require(
        root[1] == ["version", "1"] and root[2] == ["logic", "QF_UF"],
        "wrong version/logic",
    )
    require(all(c[0] == "clause" for c in root[4][1:]), "wrong clause tag")
    require(all(c[0] == "conflict" for c in root[5][1:]), "wrong conflict tag")
    names = [(d[0], d[1]) for d in root[3][1:]]
    require(len(names) == len(set(names)), "duplicate declaration name")
    return root


def reference(reference_solver: str, source: str) -> str:
    result = invoke([reference_solver, "-in"], source)
    answer = result.stdout.strip()
    require(
        result.returncode == 0 and answer in {"sat", "unsat"},
        f"reference failure: {result}",
    )
    return answer


def validate_artifact(root: list, reference_solver: str) -> None:
    signature = "(set-logic QF_UF)\n" + "\n".join(map(text, root[3][1:])) + "\n"
    for conflict in root[5][1:]:
        facts = "".join("(assert " + text(lit) + ")" for lit in conflict[1:])
        require(
            reference(reference_solver, signature + facts + "(check-sat)") == "unsat",
            "invalid theory conflict",
        )

    # Abstract every atom to a fresh Boolean: the final check must establish
    # propositional UNSAT, without relying on the reference's EUF reasoning.
    atoms: dict[str, str] = {}

    def abstract(lit: list | str) -> str:
        if isinstance(lit, list) and lit[0] == "not":
            return "(not " + abstract(lit[1]) + ")"
        if lit in ("true", "false"):
            return str(lit)
        key = text(lit)
        return atoms.setdefault(key, "v" + str(len(atoms)))

    clauses = [c[1:] for c in root[4][1:]]
    clauses += [[complement(lit) for lit in c[1:]] for c in root[5][1:]]
    assertions = [
        "(assert (or " + " ".join(map(abstract, clause)) + "))" for clause in clauses
    ]
    declarations = [f"(declare-fun {name} () Bool)" for name in atoms.values()]
    source = "(set-logic QF_UF)" + "".join(declarations + assertions) + "(check-sat)"
    require(
        reference(reference_solver, source) == "unsat",
        "exported lemmas do not refute propositional CNF",
    )


def regressions(solver: str, directory: Path) -> None:
    output = directory / "certificate.sexp"
    result = export(solver, PRELUDE + CONGRUENCE, output)
    require(result.returncode == 0 and result.stdout.strip() == "unsat", str(result))
    root = document(output)
    expected = {"(= a b)", "(not (= (f a) (f b)))"}
    require(
        any({text(lit) for lit in c[1:]} == expected for c in root[5][1:]),
        "wrong conflict polarity",
    )
    require(len(root[4]) == 3, "input clauses lost or extra clauses exported")
    output.unlink()

    for body in (
        "(assert false)(check-sat)",
        (
            "(define-fun alias ((t U)) U (f t))"
            "(assert (= a b))(assert (not (= (alias a) (alias b))))(check-sat)"
        ),
        (
            "(declare-fun x () U)"
            "(assert (or (and (= a x) (= x c)) (= a b)))"
            "(assert (not (= (f a) (f b))))"
            "(assert (not (= (f a) (f c))))(check-sat)"
        ),
        (
            "(declare-fun primo_cnf_0 () Bool)"
            "(assert (or (and (= a b) (= b c)) (= a c)))"
            "(assert (not (= (f a) (f c))))(check-sat)"
        ),
        (
            "(declare-fun q () Bool)(declare-fun r () Bool)"
            "(assert q)(assert (= q r))(assert (not r))(check-sat)"
        ),
        (
            "(assert (not (= (h (p a)) (h true))))"
            "(assert (not (= (h (p a)) (h false))))(check-sat)"
        ),
        "(assert (p a))(assert (not (= (h (p a)) (h true))))(check-sat)",
        "(assert (= a b))(assert (= b c))(assert (not (= a c)))(check-sat)",
        "(declare-sort |odd sort| 0)(declare-fun |odd name| () |odd sort|)"
        + CONGRUENCE,
        "(assert (and (= a b) (or (not (= (f a) (f b))) false)))(check-sat)",
    ):
        result = export(solver, PRELUDE + body, output)
        require(
            result.returncode == 0 and result.stdout.strip() == "unsat", str(result)
        )
        document(output)
        output.unlink()

    for source, options in (
        (PRELUDE + "(check-sat)", ()),
        (PRELUDE + "(assert (= a b))(check-sat)", ()),
        (PRELUDE + "(assert (or (and (p a) (p b)) (p c)))(check-sat)", ()),
        (PRELUDE + "(assert (= (h (and (p a) (p b))) a))(check-sat)", ()),
        (PRELUDE + CONGRUENCE, ("--max-theory-conflicts=1",)),
    ):
        result = export(solver, source, output, *options)
        require(
            result.returncode == 0 and result.stdout.strip() in {"sat", "unknown"},
            str(result),
        )
        require(not output.exists(), "SAT/unknown wrote an artifact")

    for source, options in (
        (PRELUDE + "(push 1)(pop 1)" + CONGRUENCE, ()),
        (PRELUDE + CONGRUENCE + "(check-sat)", ()),
        (PRELUDE + CONGRUENCE + "(assert true)", ()),
        (PRELUDE + "(reset)" + CONGRUENCE, ()),
        (PRELUDE + CONGRUENCE + "(reset-assertions)", ()),
        (PRELUDE + "(assert (ite (p a) (p b) (p c)))(check-sat)", ()),
        (PRELUDE.replace("QF_UF", "QF_LRA") + CONGRUENCE, ()),
        (PRELUDE, ()),
        (PRELUDE + CONGRUENCE + "(exit)(assert true)", ()),
        (PRELUDE + CONGRUENCE, ("--incremental-mode=assumptions",)),
        (PRELUDE + CONGRUENCE, ("--preprocess-only",)),
    ):
        output.write_text("sentinel")
        result = export(solver, source, output, *options)
        require(
            result.returncode != 0, f"accepted unsupported request: {source} {options}"
        )
        require(output.read_text() == "sentinel", "rejected request touched output")
        output.unlink()

    result = export(solver, PRELUDE + CONGRUENCE, directory / "absent" / "out")
    require(result.returncode != 0, "output open error ignored")
    input_file = directory / "input.smt2"
    input_file.write_text(PRELUDE + CONGRUENCE)
    result = invoke(
        [solver, "--quiet", "--export-cnf", str(input_file), str(input_file)], ""
    )
    require(
        result.returncode != 0 and input_file.read_text() == PRELUDE + CONGRUENCE,
        "input overwritten",
    )
    result = invoke(
        [
            solver,
            "--quiet",
            "--export-cnf",
            str(output),
            str(input_file),
            str(input_file),
        ],
        "",
    )
    require(result.returncode != 0 and not output.exists(), "multiple files accepted")
    print("CNF export CLI regressions passed")


def fuzz(args: argparse.Namespace) -> None:
    require(
        bool(args.reference) and args.out is not None,
        "fuzzing requires --reference and --out",
    )
    args.out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    terms = ["a", "b", "c", "(f a)", "(f b)", "(f c)", "(h true)", "(h (p a))"]
    atoms = [f"(= {a} {b})" for a in terms for b in terms if a != b] + [
        "(p a)",
        "(p b)",
        "(p c)",
    ]
    summary = {
        "pass": 0,
        "wrong_answer": 0,
        "crash": 0,
        "parser_type_error": 0,
        "timeout": 0,
        "unsupported": 0,
        "reference_disagreement": 0,
        "invalid_artifact": 0,
    }
    for index in range(args.fuzz_count):
        clauses = []
        for _ in range(rng.randint(1, 20)):
            literals = [rng.choice(atoms) for _ in range(rng.randint(1, 4))]
            literals = [
                lit if rng.getrandbits(1) else f"(not {lit})" for lit in literals
            ]
            clauses.append("(assert (or " + " ".join(literals) + "))")
        source = PRELUDE + "\n".join(clauses)
        if index % 2:
            source += "(assert (or (and (= a b) (= b c)) (and (p a) (not (p c)))))"
        if index % 3 == 0:
            source += "(assert (= a b))(assert (not (= (f a) (f b))))"
        source += "(check-sat)"
        stem = args.out / f"case-{index:05d}"
        stem.with_suffix(".smt2").write_text(source)
        output = stem.with_suffix(".sexp")
        try:
            expected = reference(args.reference, source)
            result = export(args.solver, source, output)
            if result.returncode != 0:
                category = "crash" if result.returncode < 0 else "unsupported"
                summary[category] += 1
                raise AssertionError(str(result))
            if result.stdout.strip() != expected:
                summary["wrong_answer"] += 1
                raise AssertionError(f"expected {expected}, got {result}")
            if expected == "unsat":
                try:
                    validate_artifact(document(output), args.reference)
                except AssertionError:
                    summary["invalid_artifact"] += 1
                    raise
            else:
                require(not output.exists(), "SAT run created certificate")
            summary["pass"] += 1
        except subprocess.TimeoutExpired:
            summary["timeout"] += 1
        except AssertionError as error:
            stem.with_suffix(".error").write_text(str(error))
            raise
        finally:
            (args.out / "summary.json").write_text(
                json.dumps(
                    {"seed": args.seed, "requested": args.fuzz_count, **summary},
                    indent=2,
                )
                + "\n"
            )
    print(json.dumps(summary, sort_keys=True))
    require(summary["pass"] + summary["timeout"] == args.fuzz_count, "incomplete gate")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--fuzz-count", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20261102)
    parser.add_argument("--reference")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    with TemporaryDirectory(prefix="primo-cnf-export-") as directory:
        regressions(args.solver, Path(directory))
    if args.fuzz_count:
        fuzz(args)


if __name__ == "__main__":
    main()
