#!/usr/bin/env python3
"""Summarize retained generated-answer/model and real-mutation gate evidence."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def rows(path: Path) -> list[dict[str, str]]:
    with path.open() as stream:
        return list(csv.DictReader(stream))


def identity(path: str) -> str:
    # Validator manifests make input paths absolute; generator CSVs can use the
    # same path relative to the workspace. Join only within a single profile.
    return path.split("results/arithmetic-overhead-20261109/", 1)[-1]


def summarize(primary: list[Path], models: list[Path]) -> dict:
    answers = [row for path in primary for row in rows(path)]
    outcomes = Counter(row["outcome"] for row in answers)
    model_rows = [row for path in models for row in rows(path)]
    model_outcomes = Counter(row["outcome"] for row in model_rows)
    passed = {
        identity(row["file"])
        for row in model_rows
        if row["outcome"] == "pass"
        and row["validator"] == "dolmen"
        and row["answer"] == "sat"
        and row["solver_exit"] == "0"
        and row["dolmen_exit"] == "0"
    }
    sat = {
        identity(row.get("case") or row["file"])
        for row in answers
        if row["outcome"] == "PASS"
        and (row.get("ours") or row.get("answer")) == "sat"
        and row.get("ours_code", "0") == "0"
    }
    # The conforming LRA generator checks the actual printed model in the same
    # solver invocation; it records direct Dolmen acceptance alongside the answer.
    integrated = {
        identity(row["case"])
        for row in answers
        if row["outcome"] == "PASS"
        and row.get("answer") == "sat"
        and row.get("dolmen") == "PASS"
    }
    passed |= integrated
    blocking = set(outcomes) - {
        "PASS",
        "TIMEOUT",
        "UNKNOWN_INVOLVED",
        "REFERENCE_UNUSABLE",
    }
    blocking_models = set(model_outcomes) - {
        "pass",
        "not-sat",
        "solver-timeout",
        "solver-unknown",
    }
    if blocking or blocking_models:
        raise ValueError(f"blocking outcomes: {blocking}, {blocking_models}")
    return {
        "cases": len(answers),
        "outcomes": dict(outcomes),
        "primary_successful_SAT": len(sat),
        "primary_SAT_direct_Dolmen": len(sat & passed),
        "uncovered_primary_SAT": sorted(sat - passed),
        "all_model_outcomes": dict(model_outcomes),
        "additional_Dolmen_passes_after_inconclusive_answers": len(passed - sat),
        "blocking_answer_or_model_outcomes": 0,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--stage", choices=["cache", "cite"], required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.root
    result = {"stage": args.stage, "generated": {}, "mutations": {}}
    for profile in (
        "lia-default",
        "lia-m124",
        "uflia-default",
        "uflia-m124",
        "lra-default",
        "uflra-default",
    ):
        base = root / "fuzz" / profile
        if profile.startswith("lia-"):
            primary = sorted(base.glob("fuzzsmt/shard-*/summary.csv"))
            models = sorted(base.glob("model-validation/shard-*/summary.csv"))
            if args.stage == "cache":
                models += sorted(
                    (root / "triage" / (profile + "-models")).glob("summary.csv")
                )
        elif profile.startswith("uflia-"):
            primary = sorted(base.glob("task-*/answer/summary.csv"))
            models = sorted(base.glob("task-*/model-validation/summary.csv"))
        elif profile == "lra-default":
            if args.stage == "cache":
                base = root / "lra-conforming/fuzz/lra-default"
            primary = sorted(base.glob("task-*/summary.csv"))
            models = []
        else:
            primary = sorted(base.glob("shard-*/summary.csv"))
            models = sorted(base.glob("shard-*/models/summary.csv"))
        if len(primary) != 4:
            raise ValueError("incomplete generated shards: " + profile)
        result["generated"][profile] = summarize(primary, models)
        if result["generated"][profile]["cases"] != 1000:
            raise ValueError("incorrect generated count: " + profile)
        base = root / "mutations" / profile
        if args.stage == "cache" and profile == "uflra-default":
            base = root / "uflra-mutation-retry/mutations/uflra-default"
        seeds = json.loads((base / "seed-provenance.json").read_text())
        result["mutations"][profile] = {
            "seeds": [
                {key: seed[key] for key in ("file", "logic", "sha256")}
                for seed in seeds
            ],
            "original_seeds": summarize(
                [base / "seeds-answers.csv"],
                [base / "seeds-models/summary.csv"]
                if (base / "seeds-models/summary.csv").exists()
                else [],
            ),
            "retained_mutants": summarize(
                [base / "mutant-answers.csv"],
                [base / "mutant-models/summary.csv"]
                if (base / "mutant-models/summary.csv").exists()
                else [],
            ),
        }
        if result["mutations"][profile]["retained_mutants"]["cases"] < 100:
            raise ValueError("insufficient retained real-seed mutations: " + profile)
    # Reference timeouts and integer-budget unknowns require their separately
    # retained baseline/reference triage; they are not agreements or passes.
    result["note"] = (
        "Timeout/unknown/reference-inconclusive rows remain separate from PASS. "
        "See run-log triage for exceptional rows; no blanket correctness inference."
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                "profile_case_checks": sum(
                    v["cases"] for v in result["generated"].values()
                ),
                "real_mutation_checks": sum(
                    v["retained_mutants"]["cases"] for v in result["mutations"].values()
                ),
                "uncovered_primary_SAT": sum(
                    len(v["uncovered_primary_SAT"])
                    for v in result["generated"].values()
                ),
            }
        )
    )


if __name__ == "__main__":
    main()
