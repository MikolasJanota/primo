#!/usr/bin/env python3
"""Targeted bound-flow, private-budget and Callgrind diagnostics, SLURM only."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import resource
import signal
import subprocess
import sys
from pathlib import Path

ROOT = next(
    p for p in Path(__file__).resolve().parents if (p / "CMakeLists.txt").is_file()
)
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402
from scripts.validate_qfuf_models_with_dolmen import command_spans  # noqa: E402

BASE = ["--quiet", "--stats", "--lra-propagation"]
ROWS = ["--lra-row-propagation", "--lra-bidirectional-row-propagation"]
PRE = ["--preprocess-bound-tightening"]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expanded_bounds(text: str) -> list[str]:
    """Expand just the proof exporter's new endpoints, not its whole input DAG."""
    definitions = {}
    for line in text.splitlines():
        match = re.fullmatch(r"\(define-fun (bound_probe_+\d+) \(\) \S+ (.*)\)", line)
        if match:
            definitions[match[1]] = match[2]
    match = re.search(r"\(assert \(not \(and true([^\n]*)\)\)\)\(check-sat\)", text)
    if not match:
        raise ValueError("unrecognized qualification-probe export")
    names = match[1].split()
    cache = {}
    token = re.compile(r"\bbound_probe_+\d+\b")
    for root in names:
        pending = [(root, False)]
        while pending:
            name, ready = pending.pop()
            if name in cache:
                continue
            body = definitions[name]
            if ready:
                cache[name] = token.sub(lambda m: cache[m[0]], body)
            else:
                pending.append((name, True))
                pending.extend((child, False) for child in token.findall(body))
    return [cache[name] for name in names]


def append_bounds(source: str, bounds: list[str]) -> str:
    queries = [
        (start, end)
        for start, end, words in command_spans(source)
        if words[0] == "check-sat"
    ]
    if len(queries) != 1:
        raise ValueError("requires one original query")
    start, _ = queries[0]
    return (
        source[:start]
        + "\n".join(f"(assert {b})" for b in bounds)
        + "\n"
        + source[start:]
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--mode", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID"), "solver diagnostics belong in SLURM"
    config = json.loads(args.config.read_text())
    native = Path(config["native"])
    assert sha(native) == config["native_sha256"]
    out = Path(config["out"]) / f"task-{args.mode}"
    out.mkdir(parents=True, exist_ok=False)
    (out / "provenance.json").write_text(
        json.dumps(
            {
                "job": os.environ["SLURM_JOB_ID"],
                "mode": args.mode,
                "runner_sha256": sha(Path(__file__)),
                "config_sha256": sha(args.config),
                "native_sha256": sha(native),
                "source_commit": config["source_commit"],
            },
            indent=2,
        )
        + "\n"
    )
    cases = {c["slot"]: c for c in config["cases"]}

    def source(slot: int) -> Path:
        case = cases[slot]
        path = Path(config["roots"][case["logic"]]) / case["file"]
        assert sha(path) == case["sha256"]
        return path

    def limited() -> None:
        resource.setrlimit(resource.RLIMIT_AS, (8 * 1024**3,) * 2)

    if args.mode in (0, 4, 5):
        limited()

    def run(label: str, path: Path, flags: list[str], timeout: float = 120) -> dict:
        # Keep the usual harness's timeout/exit precedence and complete raw logs.
        row = run_solver([str(native), *flags], path, timeout)
        for kind in ("stdout", "stderr"):
            (out / f"{label}.{kind}").write_text(row.pop(kind))
        row.update(label=label, input_sha256=sha(path), args=flags)
        with (out / "rows.jsonl").open("a") as stream:
            stream.write(json.dumps(row) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        assert row["status"] in ("ok", "timeout"), row
        assert row["status"] != "ok" or row["exit_code"] == 0, row
        assert row["answer"] in ("sat", "unknown"), row
        print(label, row["status"], row["answer"], row["seconds"], flush=True)
        if row["status"] == "ok" and row["answer"] == "sat":
            subprocess.run(
                [
                    sys.executable,
                    "scripts/validate_model.py",
                    "--solver",
                    str(native),
                    *(f"--solver-arg={f}" for f in flags),
                    "--solver-arg=--check-model",
                    "--expect-sat",
                    "--require-dolmen-pass",
                    "--dolmen-arg=--strict=false",
                    "--timeout",
                    "180",
                    "--dolmen-timeout",
                    "30",
                    "--out",
                    str(out / "models" / label),
                    str(path),
                ],
                check=True,
            )
        return row

    if args.mode == 0:
        # These are new small probes, not copied solver or benchmark source.
        rows = []
        for logic in ("QF_LRA", "QF_UFLRA", "QF_LIA", "QF_UFLIA"):
            sort = "Int" if "LIA" in logic else "Real"
            prefix = (
                f"(set-logic {logic})\n(declare-const u {sort})\n"
                f"(declare-const v {sort})\n(declare-const t {sort})\n"
            )
            basic = "(assert (>= (+ u v) 5))\n"
            child = "(assert (<= u 2))\n"
            target = "(assert (or (<= v 2) (>= t 20)))\n"
            fixtures = {
                "basic-first": basic + child + target,
                "basic-last": child + basic + target,
            }
            if sort == "Int":
                for threshold in (2, 3):
                    fixtures[f"round-{threshold}"] = (
                        "(assert (<= (+ (* 2 u) v) 5))\n(assert (>= v 0))\n"
                        f"(assert (or (<= u {threshold}) (>= t 20)))\n"
                    )
            for name, body in fixtures.items():
                path = out / f"{logic}-{name}.smt2"
                path.write_text(prefix + body + "(check-sat)\n")
                ref = subprocess.run(
                    [config["z3"], str(path)],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=15,
                )
                assert ref.returncode == 0 and ref.stdout.strip() == "sat", ref
                for label, flags in [
                    ("forward", ROWS[:1]),
                    ("reverse", ROWS),
                    ("pre-reverse", [*ROWS, *PRE]),
                ]:
                    row = run(
                        f"{logic}-{name}-{label}",
                        path,
                        [
                            *BASE,
                            "--preprocessing-mode=off",
                            "--no-monotone-elimination",
                            "--no-lra-model-phase",
                            *flags,
                        ],
                    )
                    assert row["answer"] == "sat"
                    rows.append(row)
        assert rows
        probe = Path(config["probe"])
        assert sha(probe) == config["probe_sha256"]
        collected = {}
        for slot in cases:
            with (out / f"proof-{slot}.smt2").open("w") as stream:
                result = subprocess.run(
                    [str(probe), str(source(slot))],
                    check=False,
                    stdout=stream,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=60,
                )
            assert result.returncode == 0, result.stderr
            text = (out / f"proof-{slot}.smt2").read_text()
            bounds = expanded_bounds(text)
            collected[str(slot)] = bounds
            (out / f"bounds-{slot}.smt2").write_text(
                "\n".join(f"(assert {b})" for b in bounds) + "\n"
            )
            proof = subprocess.run(
                [config["z3"], str(out / f"proof-{slot}.smt2")],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            (out / f"proof-{slot}.reference").write_text(proof.stdout + proof.stderr)
            assert proof.returncode == 0 and proof.stdout.strip() == "unsat", proof
            print("emitted", slot, len(bounds), result.stderr.strip(), flush=True)
        (out / "bounds.json").write_text(json.dumps(collected, indent=2) + "\n")
    elif args.mode in (1, 2, 3):
        slot = 4 if args.mode == 3 else 438
        flags = [*BASE, *ROWS, *(PRE if args.mode == 2 else [])]
        command = [
            "valgrind",
            "--tool=callgrind",
            "--dump-instr=no",
            "--collect-jumps=no",
            "--cache-sim=no",
            f"--callgrind-out-file={out / 'callgrind.out'}",
            str(native),
            *flags,
            str(source(slot)),
        ]
        with (
            (out / "profile.stdout").open("w") as stdout,
            (out / "profile.stderr").open("w") as stderr,
        ):
            process = subprocess.Popen(
                command, stdout=stdout, stderr=stderr, start_new_session=True
            )
            partial = False
            try:
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    partial = True
                    subprocess.run(
                        ["callgrind_control", "-d", str(process.pid)],
                        check=True,
                        timeout=15,
                    )
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
        assert partial or process.returncode == 0, process.returncode
        profile = out / ("callgrind.out.1" if partial else "callgrind.out")
        assert profile.is_file()
        (out / "profile-command.json").write_text(
            json.dumps(
                {
                    "command": command,
                    "exit": process.returncode,
                    "partial": partial,
                    "profile": profile.name,
                },
                indent=2,
            )
            + "\n"
        )
        with (out / "callgrind-inclusive.txt").open("w") as stream:
            subprocess.run(
                [
                    "callgrind_annotate",
                    "--inclusive=yes",
                    "--threshold=85",
                    str(profile),
                ],
                stdout=stream,
                check=True,
            )
        with (out / "callgrind-exclusive.txt").open("w") as stream:
            subprocess.run(
                [
                    "callgrind_annotate",
                    "--inclusive=no",
                    "--threshold=80",
                    str(profile),
                ],
                stdout=stream,
                check=True,
            )
    elif args.mode in (4, 5):
        slot = 438 if args.mode == 4 else 4
        original = source(slot)
        text = original.read_text()
        bounds = json.loads((Path(config["out"]) / "task-0/bounds.json").read_text())[
            str(slot)
        ]
        run("rows-original", original, [*BASE, *ROWS])
        run("both-original", original, [*BASE, *ROWS, *PRE])
        if slot == 438:
            for limit in (1, 100, 1000):
                run(
                    f"both-cap-{limit}",
                    original,
                    [*BASE, *ROWS, *PRE, f"--lia-branch-limit={limit}"],
                )
            for factor in (0, 1):
                run(
                    f"both-hybrid-{factor}",
                    original,
                    [
                        *BASE,
                        *ROWS,
                        *PRE,
                        "--lia-branching=hybrid",
                        f"--lia-private-branch-factor={factor}",
                    ],
                )
        variants = {"explicit-all": bounds}
        if slot == 438:
            variants.update(
                {f"drop-{i}": bounds[:i] + bounds[i + 1 :] for i in range(len(bounds))}
            )
        for name, selected in variants.items():
            path = out / f"{name}.smt2"
            path.write_text(append_bounds(text, selected))
            # Check that the appended atoms use original declarations.
            reference = subprocess.run(
                [config["z3"], str(path)],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            (out / f"{name}.reference").write_text(reference.stdout + reference.stderr)
            assert reference.returncode == 0 and reference.stdout.strip() == "sat", (
                reference
            )
            run(name, path, [*BASE, *ROWS])
    else:
        raise ValueError(args.mode)
    (out / "complete").write_text("diagnostic only; no performance promotion\n")


if __name__ == "__main__":
    main()
