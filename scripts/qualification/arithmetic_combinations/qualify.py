#!/usr/bin/env python3
"""Exact-profile generated/seed/mutation qualification with all-SAT Dolmen checks."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import run_solver  # noqa: E402
from scripts.classify_mutation_resources import resource_only  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command: list[str], *, allow_failure: bool = False) -> int:
    result = subprocess.run(command, check=False)
    if result.returncode and not allow_failure:
        raise RuntimeError((command, result.returncode))
    return result.returncode


def models(solver: str, flags: list[str], selected: list[str], out: Path) -> dict:
    assert len(selected) == len(set(selected))
    manifest = out.parent / (out.name + "-sat.txt")
    manifest.write_text("".join(p + "\n" for p in selected))
    if selected:
        execute(
            [
                sys.executable,
                "scripts/validate_model.py",
                "--solver",
                solver,
                *(f"--solver-arg={x}" for x in flags),
                "--expect-sat",
                "--manifest",
                str(manifest),
                "--require-dolmen-pass",
                "--dolmen-arg=--strict=false",
                "--timeout",
                "30",
                "--dolmen-timeout",
                "30",
                "--out",
                str(out),
            ]
        )
        rows = list(csv.DictReader((out / "summary.csv").open()))
        assert len(rows) == len(selected) and {r["file"] for r in rows} == set(selected)
        assert all(
            r["outcome"] == "pass"
            and r["validator"] == "dolmen"
            and r["solver_exit"] == r["dolmen_exit"] == "0"
            for r in rows
        )
    return {
        "SAT_Dolmen_pass": len(selected),
        "model_summary_sha256": sha(out / "summary.csv") if selected else None,
    }


def generated(campaign: dict, task: dict, slot: int, out: Path) -> dict:
    flags = [*task["args"], "--check-model"]
    common = ["--solver", campaign["solver"], *(f"--solver-arg={f}" for f in flags)]
    seed = campaign["protocol"]["generated_seed_base"] + slot
    tail = ["--count", "1000", "--seed", str(seed), "--timeout", "5", "--out", str(out)]
    logic = task["logic"]
    if logic == "QF_LIA":
        command = [
            sys.executable,
            "scripts/fuzz_external.py",
            "--fuzzer",
            "fuzzsmt",
            "--logic",
            logic,
            "--solver",
            shlex.join([campaign["solver"], *flags]),
            "--refs",
            "z3",
            "--fuzzer-args",
            "-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4",
            *tail,
        ]
    elif logic == "QF_UFLIA":
        command = [
            sys.executable,
            "scripts/qf_uflia_fuzz.py",
            *common,
            "--ref",
            "z3",
            "--shape",
            "mixed",
            "--depth",
            "3",
            *tail,
        ]
    elif logic == "QF_LRA":
        command = [
            sys.executable,
            "scripts/qf_lra_boolean_arithmetic_fuzz.py",
            *common,
            "--z3",
            "contrib/local/bin/z3",
            "--dolmen",
            "contrib/local/bin/dolmen",
            *tail,
        ]
    else:
        command = [
            sys.executable,
            "scripts/qf_uflra_smoke_fuzz.py",
            *common,
            "--ref",
            "z3",
            *tail,
        ]
    code = execute(command, allow_failure=True)
    rows = list(csv.DictReader((out / "summary.csv").open()))
    assert len(rows) == 1000
    unknowns = []
    for row in rows:
        outcome = row["outcome"]
        if outcome in ("PASS", "TIMEOUT"):
            continue
        if outcome == "REFERENCE_UNUSABLE":
            references = (
                row.get("refs") or row.get("references") or row.get("reference")
            )
            assert references == "TIMEOUT", row
            continue
        assert outcome == "UNKNOWN_INVOLVED" and row.get("ours") == "unknown", row
        source = Path(row.get("file") or row["case"])
        replay = run_solver([campaign["solver"], *flags], source, 30)
        assert (
            replay["status"] == "ok"
            and replay["exit_code"] == 0
            and replay["answer"] == "unknown"
        )
        assert "integer branch-and-bound node limit" in replay["stderr"].splitlines()
        assert int(replay["stats"].get(":lia.branching.budget-exits", 0)) > 0
        unknowns.append(
            {
                "input_sha256": sha(source),
                "reason": "integer branch-and-bound node limit",
            }
        )
    selected = [
        r.get("file") or r["case"]
        for r in rows
        if (r.get("ours") or r.get("answer")) == "sat"
    ]
    return {
        "command": command,
        "seed": seed,
        "generator_exit": code,
        "cases": len(rows),
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "resource_unknowns": unknowns,
        "summary_sha256": sha(out / "summary.csv"),
        **models(campaign["solver"], flags, selected, out / "models"),
    }


def seed_gate(campaign: dict, task: dict, out: Path) -> dict:
    cohort = "integers" if task["logic"] in ("QF_LIA", "QF_UFLIA") else "reals"
    execute(
        [
            sys.executable,
            "scripts/qualification/bound_propagation/seeds.py",
            "--profile",
            task["label"],
            "--profile-args-json",
            str(Path(campaign["configs"]) / f"profile-args-{cohort}.json"),
            "--logic",
            task["logic"],
            "--out",
            str(out),
        ]
    )
    return json.loads((out / "complete.json").read_text())


def mutations(campaign: dict, task: dict, slot: int, out: Path) -> dict:
    out.mkdir(parents=True, exist_ok=False)
    seeds = out / "seeds"
    seeds.mkdir()
    root = Path(campaign["roots"][task["logic"]])
    candidates = json.loads(Path(campaign["mutation_candidates"]).read_text())[
        task["logic"]
    ]
    families = collections.defaultdict(list)
    for c in candidates:
        p = root / c["file"]
        if p.stat().st_size <= 200000:
            families[c["file"].split("/")[0]].append((p.stat().st_size, c))
    for group in families.values():
        group.sort(key=lambda x: (x[0], x[1]["baseline_seconds"], x[1]["file"]))
    selected = []
    while len(selected) < 12:
        previous = len(selected)
        for family in sorted(families):
            if not families[family] or len(selected) == 12:
                continue
            _, case = families[family].pop(0)
            source = root / case["file"]
            assert sha(source) == case["sha256"]
            shutil.copyfile(source, seeds / f"{len(selected):03d}.smt2")
            selected.append(case)
        assert len(selected) > previous, "fewer than12eligible real seeds"
    (out / "seeds.json").write_text(json.dumps(selected, indent=2) + "\n")
    flags = [*task["args"], "--check-model"]

    def answers(directory: Path, target: Path, ignore: bool = False) -> list[dict]:
        command = [
            sys.executable,
            "scripts/check_solver_seed_corpus.py",
            "--seeds",
            str(directory),
            "--solver",
            campaign["solver"],
            *(f"--solver-arg={f}" for f in flags),
            "--ref",
            "contrib/local/bin/z3",
            "--timeout",
            "10",
            "--out",
            str(target),
        ]
        if ignore:
            command.append("--ignore-declared-status")
        code = execute(command, allow_failure=True)
        rows = list(csv.DictReader(target.open()))
        assert not code or resource_only(rows, "answers"), rows
        return rows

    originals = answers(seeds, out / "seed-answers.csv")
    original_models = models(
        campaign["solver"],
        flags,
        [r["file"] for r in originals if r["ours"] == "sat" and r["ours_exit"] == "0"],
        out / "seed-models",
    )
    seed = campaign["protocol"]["mutation_seed_base"] + slot
    execute(
        [
            "scripts/yinyang_fuzz.sh",
            "--seeds",
            str(seeds),
            "--typefuzz",
            str(ROOT / "scripts/seeded_opfuzz.py"),
            "--solver",
            shlex.join([campaign["solver"], *flags]),
            "--ref",
            shlex.join([str(ROOT / "contrib/local/bin/z3"), "model_validate=true"]),
            "--seconds",
            "600",
            "--iterations",
            "30",
            "--solver-timeout",
            "5",
            "--file-size-limit",
            "2000000",
            "--config",
            "scripts/fuzz_configs/model_linear_ops.txt",
            "--bugs",
            str(out / "bugs"),
            "--scratch",
            str(out / "scratch"),
            "--logs",
            str(out / "logs"),
            "--random-seed",
            str(seed),
            "--keep-logic",
            "--hoist-declarations",
            "--keep-mutants",
        ]
    )
    rows = answers(out / "scratch", out / "mutant-answers.csv", True)
    assert len(rows) >= 100
    assert not any(p.is_file() for p in (out / "bugs").rglob("*")), (
        "saved failures need classification"
    )
    selected_sat = [
        r["file"] for r in rows if r["ours"] == "sat" and r["ours_exit"] == "0"
    ]
    return {
        "cases": len(rows),
        "seed": seed,
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "original_outcomes": dict(collections.Counter(r["outcome"] for r in originals)),
        "original_models": original_models,
        "summary_sha256": sha(out / "mutant-answers.csv"),
        **models(campaign["solver"], flags, selected_sat, out / "models"),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("generated", "seeds", "mutations"))
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    campaign = json.loads(args.campaign.read_text())
    assert sha(Path(campaign["solver"])) == campaign["sha256"]
    task = campaign["tasks"][args.slot]
    out = Path(campaign["results"]) / args.phase / f"task-{args.slot}"
    result = {
        "task": task,
        "phase": args.phase,
        "job": os.environ["SLURM_JOB_ID"],
        "native_sha256": campaign["sha256"],
        "source": campaign["git"],
        "campaign_sha256": sha(args.campaign),
        "script_sha256": sha(Path(__file__)),
    }
    if args.phase == "generated":
        result.update(generated(campaign, task, args.slot, out))
    elif args.phase == "seeds":
        result.update(seed_gate(campaign, task, out))
    else:
        result.update(mutations(campaign, task, args.slot, out))
    (out / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {k: v for k, v in result.items() if k not in ("command", "task")}, indent=2
        )
    )


if __name__ == "__main__":
    main()
