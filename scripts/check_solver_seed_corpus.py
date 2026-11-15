#!/usr/bin/env python3
"""Strictly compare every tracked regression seed with a reference solver."""

from __future__ import annotations

import argparse
import csv
import subprocess
from collections import Counter
from pathlib import Path

CONCRETE_ANSWERS = {"sat", "unsat"}


def declared_status(path: Path) -> str:
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if line.startswith("(set-info :status "):
            return line.removeprefix("(set-info :status ").rstrip(")").strip()
    return ""


def run(command: list[str], path: Path, timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            [*command, str(path)],
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "timeout", 124
    except FileNotFoundError:
        return "missing", 127

    first_line = proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""
    # Statistic names such as fallback-unsupported are not solver diagnostics.
    diagnostics = "\n".join(
        line for line in (proc.stdout + "\n" + proc.stderr).splitlines()
        if not line.lstrip().startswith(":")
    ).lower()
    if proc.returncode != 0 and first_line in {"sat", "unsat"}:
        return "crash", proc.returncode
    if "unsupported" in diagnostics:
        return "unsupported", proc.returncode
    if proc.returncode != 0:
        return "crash", proc.returncode
    if first_line in CONCRETE_ANSWERS:
        return first_line, proc.returncode
    return first_line or "unknown-output", proc.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", required=True)
    parser.add_argument("--ref-arg", action="append", default=[])
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--ignore-declared-status", action="store_true",
                        help="For mutated formulas only: compare solvers, not the seed's status")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    files = sorted(args.seeds.rglob("*.smt2"))
    if not files:
        raise SystemExit(f"no SMT-LIB seeds found under {args.seeds}")

    solver_command = [args.solver, *args.solver_arg]
    reference_command = [args.ref, *args.ref_arg]
    rows: list[dict[str, object]] = []
    counts: Counter[str] = Counter()
    for path in files:
        expected = "" if args.ignore_declared_status else declared_status(path)
        ours, ours_exit = run(solver_command, path, args.timeout)
        reference, reference_exit = run(reference_command, path, args.timeout)
        if ours not in CONCRETE_ANSWERS or reference not in CONCRETE_ANSWERS:
            outcome = "INCOMPLETE"
        elif ours != reference:
            outcome = "DISAGREEMENT"
        elif expected in CONCRETE_ANSWERS and ours != expected:
            outcome = "STATUS_MISMATCH"
        else:
            outcome = "PASS"
        counts[outcome] += 1
        rows.append(
            {
                "file": str(path),
                "expected": expected,
                "ours": ours,
                "ours_exit": ours_exit,
                "reference": reference,
                "reference_exit": reference_exit,
                "outcome": outcome,
            }
        )
        print(
            f"{path}: {outcome} ours={ours} reference={reference} expected={expected}",
            flush=True,
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"summary: {args.out}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)
    return 0 if counts == Counter({"PASS": len(files)}) else 1


if __name__ == "__main__":
    raise SystemExit(main())
