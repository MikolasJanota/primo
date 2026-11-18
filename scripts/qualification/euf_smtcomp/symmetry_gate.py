#!/usr/bin/env python3
"""Fresh seeded overlapping-domain permutations, checked against Z3 and Dolmen."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import sys
from pathlib import Path

ROOT = Path(os.environ.get("PRIMO_WORKDIR", Path(__file__).resolve().parents[3]))
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402
from scripts.qualification.arithmetic_combinations.qualify import models  # noqa: E402


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(index, rng):
    size = rng.randrange(3, 9)
    values = [f"v_{index}_{j}" for j in range(size)]
    declarations = list(values)
    rng.shuffle(declarations)
    expected = "sat" if index % 2 == 0 else "unsat"
    facts = ["(distinct " + " ".join(values) + ")"]
    facts.append("(distinct " + " ".join(f"(f {v})" for v in values) + ")")
    for value in values:
        order = list(values)
        rng.shuffle(order)
        facts.append("(or " + " ".join(f"(= (f {value}) {v})" for v in order) + ")")
        facts.append(f"(not (= (f {value}) {value}))")
    if expected == "unsat":
        facts.append(f"(= (f {values[0]}) (f {values[1]}))")
    rng.shuffle(facts)
    if rng.randrange(2):
        assertions = ["(assert (and " + " ".join(facts) + "))"]
    else:
        assertions = [f"(assert {fact})" for fact in facts]
    # SAT witness: the cycle v_i -> v_(i+1 mod n). The UNSAT variant
    # contradicts injectivity explicitly; neither verdict depends on a solver.
    return expected, "\n".join(
        [
            "(set-logic QF_UF)",
            f"(set-info :status {expected})",
            "(declare-sort U 0)",
            *(f"(declare-const {v} U)" for v in declarations),
            "(declare-fun f (U) U)",
            *assertions,
            "(check-sat)",
            "",
        ]
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    profile = config["profiles"][args.slot]
    solver = Path(config["solver"])
    assert sha(solver) == config["sha256"]
    seed = config["seed_base"] + args.slot
    rng = random.Random(seed)
    out = Path(config["out"]) / f"task-{args.slot}"
    out.mkdir(parents=True, exist_ok=False)
    cases = out / "cases"
    cases.mkdir()
    flags = [*profile["args"], "--check-model"]
    selected, bindings = [], []
    with (out / "rows.jsonl").open("w") as stream:
        for index in range(1000):
            expected, text = generate(index, rng)
            path = cases / f"{index:04d}.smt2"
            path.write_text(text)
            native = run_solver([str(solver), *flags], path, 5)
            reference = run_solver(["contrib/local/bin/z3"], path, 5)
            passed = all(
                r["status"] == "ok" and r["exit_code"] == 0 and r["answer"] == expected
                for r in (native, reference)
            )
            if expected == "sat":
                passed = (
                    passed
                    and int(
                        native["stats"].get(
                            ":preprocessing.uf-symmetry.rejected-prior-constraints", 0
                        )
                    )
                    > 0
                )
                selected.append(str(path))
            row = {
                "index": index,
                "expected": expected,
                "input_sha256": sha(path),
                "passed": passed,
                "native": {k: v for k, v in native.items() if k != "stdout"},
                "reference": {k: v for k, v in reference.items() if k != "stdout"},
            }
            stream.write(json.dumps(row) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
            assert passed, (index, expected, native, reference)
            bindings.append(row["input_sha256"])
    validated = models(str(solver), flags, selected, out / "models")
    (out / "receipt.json").write_text(
        json.dumps(
            {
                "job": os.environ["SLURM_JOB_ID"],
                "source": config["git"],
                "native_sha256": sha(solver),
                "profile": profile,
                "seed": seed,
                "count": 1000,
                "PASS": 1000,
                "SAT": 500,
                "UNSAT": 500,
                "SAT_guard_activations": 500,
                "config_sha256": sha(args.config),
                "generator_sha256": sha(Path(__file__)),
                "rows_sha256": sha(out / "rows.jsonl"),
                "input_sha256": bindings,
                **validated,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
