#!/usr/bin/env python3
"""Differential fuzzing for finite-domain preprocessing in QF_UF/QF_UFLRA.

Every generated case has a construction-known verdict and is assigned to a
deterministic logic/scenario/verdict slot. Randomness changes sizes and syntax,
not whether a campaign reaches a scenario. Primo statistics are part of the
oracle: a correct answer without the intended finite-domain activation is a
coverage failure.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import json
import os
import random
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}
SCENARIOS = (
    "flat-choice",
    "nested-choice",
    "residual-choice",
    "observed-equalities",
    "define-fun-aliases",
    "shared-domain",
    "transitive-domain",
)
STAT_FIELDS = {
    "raw_choices": ":preprocessing.finite-domain.raw-choices",
    "raw_amo": ":preprocessing.finite-domain.raw-amo-clauses",
    "post_amo": ":preprocessing.finite-domain.post-amo-clauses",
    "amo": ":preprocessing.finite-domain.amo-clauses",
    "real_lowered": ":preprocessing.real-equality-lowering.lowered",
    "solver_qf_uf": ":solver.qf-uf",
    "solver_qf_uflra": ":solver.qf-uflra",
    "structural_selector": ":preprocessing.auto.selector.structural",
    "structural_domains": ":preprocessing.auto.analysis.finite-domain-domains",
    "profile_large_skip": ":preprocessing.auto.profile.large-skip",
    "profile_finite_domain_basic": ":preprocessing.auto.profile.finite-domain-basic",
    "profile_compact_clause_basic": ":preprocessing.auto.profile.compact-clause-basic",
    "profile_strong": ":preprocessing.auto.profile.strong",
}

ACTIVATION_PROFILES = ("amo", "structural", "answers")


@dataclasses.dataclass(frozen=True)
class GeneratedCase:
    logic: str
    scenario: str
    expected: str
    text: str


@dataclasses.dataclass(frozen=True)
class SolverRun:
    answer: str
    returncode: int
    stats: dict[str, int]


def nested_or(items: list[str]) -> str:
    result = items[-1]
    for item in reversed(items[:-1]):
        result = f"(or {item} {result})"
    return result


def pairwise_disequalities(values: list[str]) -> list[str]:
    return [
        f"(not (= {values[i]} {values[j]}))"
        for i in range(len(values))
        for j in range(i + 1, len(values))
    ]


def scenario_slot(index: int, logics: tuple[str, ...]) -> tuple[str, str, str]:
    slots = [
        (logic, scenario, expected)
        for logic in logics
        for scenario in SCENARIOS
        for expected in ("sat", "unsat")
    ]
    return slots[index % len(slots)]


def generate_case(
    rng: random.Random, index: int, logics: tuple[str, ...]
) -> GeneratedCase:
    logic, scenario, expected = scenario_slot(index, logics)
    is_real = logic == "QF_UFLRA"
    sort = "Real" if is_real else "U"
    domain_size = 3 + rng.randrange(3)
    base_values = [f"v{i}" for i in range(domain_size)]
    values = list(base_values)

    declarations: list[str] = []
    if not is_real:
        declarations.append("(declare-sort U 0)")
    declarations.extend(f"(declare-fun {value} () {sort})" for value in base_values)
    declarations.extend(
        [
            f"(declare-fun x () {sort})",
            f"(declare-fun y () {sort})",
            f"(declare-fun z () {sort})",
            f"(declare-fun f ({sort}) {sort})",
            f"(declare-fun p ({sort}) Bool)",
            "(declare-fun guard0 () Bool)",
            "(declare-fun guard1 () Bool)",
        ]
    )

    if scenario == "define-fun-aliases":
        values = [f"a{i}" for i in range(domain_size)]
        if len(values) != len(base_values):
            raise AssertionError("finite-domain aliases must match their base values")
        declarations.extend(
            f"(define-fun {values[i]} () {sort} {base_values[i]})"
            for i in range(len(values))
        )

    facts = pairwise_disequalities(values)
    equalities = [f"(= x {value})" for value in values]
    if scenario == "flat-choice" or scenario == "define-fun-aliases":
        facts.append(f"(or {' '.join(equalities)})")
    elif scenario == "nested-choice":
        facts.append(nested_or(equalities))
    elif scenario == "residual-choice":
        facts.append(f"(or {' '.join(equalities)})")
        facts.append(f"(not (= x {values[-1]}))")
    elif scenario == "observed-equalities":
        for offset, equality in enumerate(equalities):
            facts.append(f"(or {equality} guard{offset % 2})")
    elif scenario == "shared-domain":
        facts.append(f"(or {' '.join(equalities)})")
        facts.append(f"(or {' '.join(f'(= y {value})' for value in values)})")
        facts.extend([f"(= y {values[1]})", "(not (= x y))"])
    elif scenario == "transitive-domain":
        facts.append(nested_or(equalities))
        facts.extend(["(= x y)", "(= y z)", f"(= z {values[0]})"])

    # The UNSAT variant pins x and contradicts congruence through a predicate.
    # The SAT control leaves the finite choice unresolved so the post-pass AMO
    # recognizer remains exercised while keeping the same UF applications.
    if expected == "unsat":
        facts.append(f"(= x {values[0]})")
    facts.append("(p x)")
    if expected == "unsat":
        facts.append(f"(not (p {values[0]}))")
    else:
        facts.append(f"(p {values[0]})")
    facts.append(f"(= (f x) (f {values[0]}))")
    if is_real:
        facts.append("(< (+ (f x) 1) (+ (f x) 2))")

    fact_text = "\n    ".join(facts)
    assertion = f"(assert (and\n    {fact_text}))"
    text = "\n".join(
        [
            f"; finite-domain scenario: {scenario}",
            f"; expected: {expected}",
            f"(set-logic {logic})",
            *declarations,
            assertion,
            "(check-sat)",
            "",
        ]
    )
    return GeneratedCase(logic=logic, scenario=scenario, expected=expected, text=text)


def preflight_generation(seed: int, logics: tuple[str, ...]) -> list[GeneratedCase]:
    """Generate one complete coverage cycle before invoking any solver."""
    rng = random.Random(seed)
    slot_count = len(logics) * len(SCENARIOS) * 2
    return [generate_case(rng, index, logics) for index in range(slot_count)]


def parse_stats(output: str) -> dict[str, int]:
    stats: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.strip().split()
        if len(fields) != 2 or not fields[0].startswith(":"):
            continue
        try:
            stats[fields[0]] = int(fields[1])
        except ValueError:
            continue
    return stats


def run_solver(command: list[str], timeout: float, collect_stats: bool) -> SolverRun:
    try:
        proc = subprocess.run(
            command, check=False, text=True, capture_output=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        return SolverRun("TIMEOUT", 124, {})
    except FileNotFoundError:
        return SolverRun("MISSING", 127, {})

    output = f"{proc.stdout}\n{proc.stderr}"
    lowered = output.lower()
    answer = next(
        (line.strip() for line in proc.stdout.splitlines() if line.strip() in RESULTS),
        "",
    )
    if "unsupported" in lowered:
        answer = "UNSUPPORTED_INPUT"
    elif proc.returncode != 0:
        answer = "CRASH"
    elif not answer:
        answer = "UNKNOWN_OUTPUT"
    return SolverRun(
        answer, proc.returncode, parse_stats(output) if collect_stats else {}
    )


def activation_failure(
    case: GeneratedCase, stats: dict[str, int], activation_profile: str = "amo"
) -> str | None:
    if activation_profile == "answers":
        return None
    required: list[str] = []
    if activation_profile == "amo":
        required.extend([STAT_FIELDS["raw_amo"], STAT_FIELDS["amo"]])
    elif activation_profile == "structural":
        required.extend(
            [STAT_FIELDS["structural_selector"], STAT_FIELDS["structural_domains"]]
        )
    else:
        raise ValueError(f"unknown activation profile: {activation_profile}")
    if case.logic == "QF_UFLRA":
        required.extend([STAT_FIELDS["real_lowered"], STAT_FIELDS["solver_qf_uflra"]])
    else:
        required.append(STAT_FIELDS["solver_qf_uf"])
    missing = [name for name in required if stats.get(name, 0) <= 0]
    if activation_profile == "structural" and not any(
        stats.get(STAT_FIELDS[field], 0) > 0
        for field in (
            "profile_large_skip",
            "profile_finite_domain_basic",
            "profile_compact_clause_basic",
            "profile_strong",
        )
    ):
        missing.append(":preprocessing.auto.profile.<selected>")
    return ",".join(missing) if missing else None


def classify(
    case: GeneratedCase,
    ours: SolverRun,
    references: dict[str, SolverRun],
    activation_profile: str = "amo",
) -> str:
    if ours.answer in {"UNSUPPORTED_INPUT", "CRASH", "UNKNOWN_OUTPUT", "MISSING"}:
        return ours.answer
    if ours.answer == "TIMEOUT":
        return "TIMEOUT"
    if ours.answer != case.expected:
        return "WRONG_ANSWER"
    for reference in references.values():
        if reference.answer in {"TIMEOUT", "MISSING", "CRASH", "UNKNOWN_OUTPUT"}:
            return "REFERENCE_UNUSABLE"
        if reference.answer != case.expected:
            return "REFERENCE_DISAGREEMENT"
    if activation_failure(case, ours.stats, activation_profile):
        return "ACTIVATION_MISSING"
    return "PASS"


def write_metadata(path: Path, data: dict[str, object]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def git_revision() -> str:
    proc = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=False, text=True, capture_output=True
    )
    return proc.stdout.strip() if proc.returncode == 0 else "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", action="append", default=[])
    parser.add_argument(
        "--logic", choices=["QF_UF", "QF_UFLRA", "mixed"], default="mixed"
    )
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--seed", type=int, default=2026080801)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--require-post-amo", action="store_true")
    parser.add_argument(
        "--activation-profile", choices=ACTIVATION_PROFILES, default="amo"
    )
    parser.add_argument("--out", type=Path, default=Path("fuzz-results/finite-domain"))
    args = parser.parse_args()

    if args.activation_profile == "answers" and args.require_post_amo:
        parser.error("answers profile cannot require post-AMO activation")

    references = args.ref or ["z3"]
    logics = ("QF_UF", "QF_UFLRA") if args.logic == "mixed" else (args.logic,)
    minimum_count = len(logics) * len(SCENARIOS) * 2
    if args.count < minimum_count:
        parser.error(
            f"--count must be at least {minimum_count} to cover every logic/scenario/verdict slot"
        )
    preflight_generation(args.seed, logics)
    args.out.mkdir(parents=True, exist_ok=True)
    cases_dir = args.out / "cases"
    failures_dir = args.out / "failures"
    cases_dir.mkdir(exist_ok=True)
    failures_dir.mkdir(exist_ok=True)

    metadata = {
        "status": "running",
        "commit": git_revision(),
        "seed": args.seed,
        "count": args.count,
        "logics": logics,
        "scenarios": SCENARIOS,
        "solver": args.solver,
        "solver_args": args.solver_arg,
        "references": references,
        "timeout_seconds": args.timeout,
        "require_post_amo": args.require_post_amo,
        "activation_profile": args.activation_profile,
    }
    metadata_path = args.out / "metadata.json"
    write_metadata(metadata_path, metadata)

    rng = random.Random(args.seed)
    counts: dict[str, int] = {}
    activation_totals = {field: 0 for field in STAT_FIELDS}
    summary_path = args.out / "summary.csv"
    fieldnames = [
        "case",
        "logic",
        "scenario",
        "expected",
        "ours",
        "ours_code",
        "references",
        "outcome",
        "activation_missing",
        *STAT_FIELDS,
    ]
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())
        for index in range(args.count):
            case = generate_case(rng, index, logics)
            case_path = cases_dir / f"case-{index:05d}.smt2"
            case_path.write_text(case.text, encoding="utf-8")
            ours = run_solver(
                [args.solver, "--quiet", "--stats", *args.solver_arg, str(case_path)],
                args.timeout,
                True,
            )
            ref_runs = {
                ref: run_solver(
                    [shutil.which(ref) or ref, str(case_path)], args.timeout, False
                )
                for ref in references
            }
            outcome = classify(case, ours, ref_runs, args.activation_profile)
            counts[outcome] = counts.get(outcome, 0) + 1
            missing = (
                activation_failure(case, ours.stats, args.activation_profile)
                if ours.answer == case.expected
                else None
            )
            row: dict[str, object] = {
                "case": str(case_path),
                "logic": case.logic,
                "scenario": case.scenario,
                "expected": case.expected,
                "ours": ours.answer,
                "ours_code": ours.returncode,
                "references": json.dumps(
                    {name: run.answer for name, run in ref_runs.items()}, sort_keys=True
                ),
                "outcome": outcome,
                "activation_missing": missing or "",
            }
            row.update(
                {field: ours.stats.get(stat, 0) for field, stat in STAT_FIELDS.items()}
            )
            for field in STAT_FIELDS:
                activation_totals[field] += int(row[field])
            writer.writerow(row)
            summary.flush()
            os.fsync(summary.fileno())
            if outcome != "PASS":
                shutil.copy2(
                    case_path, failures_dir / f"{outcome.lower()}-{case_path.name}"
                )
            print(
                f"{case_path}: {outcome} logic={case.logic} scenario={case.scenario} "
                f"expected={case.expected} ours={ours.answer}",
                flush=True,
            )

    metadata["status"] = "complete"
    metadata["outcomes"] = counts
    metadata["activation_totals"] = activation_totals
    aggregate_activation_failure = (
        args.require_post_amo and activation_totals["post_amo"] == 0
    )
    metadata["aggregate_activation_failure"] = aggregate_activation_failure
    write_metadata(metadata_path, metadata)
    print(f"summary: {summary_path}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)

    failures = {
        "ACTIVATION_MISSING",
        "CRASH",
        "MISSING",
        "REFERENCE_DISAGREEMENT",
        "REFERENCE_UNUSABLE",
        "UNKNOWN_OUTPUT",
        "UNSUPPORTED_INPUT",
        "WRONG_ANSWER",
    }
    return (
        1
        if aggregate_activation_failure
        or any(counts.get(outcome, 0) for outcome in failures)
        else 0
    )


if __name__ == "__main__":
    raise SystemExit(main())
