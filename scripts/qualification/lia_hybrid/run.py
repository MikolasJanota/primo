#!/usr/bin/env python3
"""Paired, content-pinned hybrid comparison and a separate Dolmen replay phase."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import multiprocessing
import os
import resource
import socket
import subprocess
import sys
from pathlib import Path

ROOT = next(
    p
    for p in Path(__file__).resolve().parents
    if (p / "scripts/bench_qf_uf_full.py").is_file()
)
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402
from scripts.qualification.long_limits.campaign import memory_exhausted  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decisive(row: dict) -> bool:
    return (
        row["status"] == "ok"
        and row["exit_code"] == 0
        and row["answer"] in ("sat", "unsat")
    )


def measure(command: list[str], source: Path, timeout: float, connection) -> None:
    # A fresh supervisor has exactly one native child, so RUSAGE_CHILDREN's
    # high-water mark is this solver's RSS, including on timeout. Keep the
    # solver itself directly supervised by the existing timeout harness.
    row = run_solver(command, source, timeout)
    row["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    connection.send(row)
    connection.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("timing", "models"))
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args()
    assert os.environ.get("SLURM_JOB_ID")
    config = json.loads(args.config.read_text())
    case = config["cases"][args.slot]
    source = Path(config["roots"][case["logic"]]) / case["file"]
    assert sha(source) == case["sha256"]
    expected = case.get("expected", "unknown")
    declared = parse_smtlib_status(source)
    if expected in ("sat", "unsat") and declared in ("sat", "unsat"):
        assert expected == declared, (
            "declared status contradicts pinned reference metadata"
        )
    if expected not in ("sat", "unsat"):
        expected = declared
    out = Path(config["out"]) / f"slot-{args.slot}"
    profiles = list(config["profiles"])
    for profile in config["profiles"].values():
        assert sha(Path(profile["solver"])) == profile["sha256"]
    if args.phase == "timing":
        out.mkdir(parents=True, exist_ok=False)
        order = (
            profiles[args.slot % len(profiles) :]
            + profiles[: args.slot % len(profiles)]
        )
        provenance = {
            "config_sha256": sha(args.config),
            "runner_sha256": sha(Path(__file__)),
            "job": os.environ["SLURM_JOB_ID"],
            "host": socket.gethostname(),
            "affinity": sorted(os.sched_getaffinity(0)),
            "case": case,
            "order": order,
            "profiles": config["profiles"],
        }
        (out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        resource.setrlimit(resource.RLIMIT_AS, (config["address_space_bytes"],) * 2)
        answers = set()
        with (out / "rows.jsonl").open("w") as stream:
            for label in order:
                profile = config["profiles"][label]
                arguments = [*profile["args"], *case.get("extra_args", [])]
                command = [profile["solver"], *arguments]
                receive, send = multiprocessing.Pipe(duplex=False)
                worker = multiprocessing.Process(
                    target=measure, args=(command, source, config["timeout"], send)
                )
                worker.start()
                send.close()
                row = receive.recv()
                worker.join()
                receive.close()
                assert worker.exitcode == 0
                row.update(
                    profile=label,
                    file=case["file"],
                    logic=case["logic"],
                    args=arguments,
                )
                for kind in ("stdout", "stderr"):
                    (out / f"{label}.{kind}").write_text(str(row.pop(kind)))
                row["stdout_log"] = str(out / f"{label}.stdout")
                row["stderr_log"] = str(out / f"{label}.stderr")
                row["memory_out"] = memory_exhausted(row)
                stream.write(json.dumps(row) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
                assert (
                    row["status"] in ("ok", "timeout", "unsupported")
                    or row["memory_out"]
                ), row
                if decisive(row):
                    assert (
                        expected not in ("sat", "unsat") or row["answer"] == expected
                    ), row
                    answers.add(row["answer"])
                    assert len(answers) == 1, "paired solver disagreement"
        (out / "timing-complete").write_text(
            "Timing only; SAT models require separate validation.\n"
        )
        return
    assert (out / "timing-complete").is_file()
    provenance = json.loads((out / "provenance.json").read_text())
    assert provenance["config_sha256"] == sha(args.config)
    rows = [json.loads(line) for line in (out / "rows.jsonl").read_text().splitlines()]
    selected = [row for row in rows if decisive(row) and row["answer"] == "sat"]
    validated = []
    for row in selected:
        profile = config["profiles"][row["profile"]]
        target = out / "models" / row["profile"]
        command = [
            sys.executable,
            "scripts/validate_model.py",
            "--solver",
            profile["solver"],
            *(f"--solver-arg={a}" for a in row["args"]),
            "--solver-arg=--check-model",
            "--expect-sat",
            "--require-dolmen-pass",
            "--dolmen-arg=--strict=false",
            "--timeout",
            "180",
            "--dolmen-timeout",
            "60",
            "--out",
            str(target),
            str(source),
        ]
        subprocess.run(command, check=True)
        with (target / "summary.csv").open() as stream:
            (model,) = csv.DictReader(stream)
        assert model["outcome"] == "pass" and model["validator"] == "dolmen"
        assert model["solver_exit"] == model["dolmen_exit"] == "0"
        validated.append(
            {"profile": row["profile"], "summary_sha256": sha(target / "summary.csv")}
        )
    (out / "coverage.json").write_text(
        json.dumps(
            {
                "selected_sat": len(selected),
                "direct_dolmen_pass": len(validated),
                "rows": validated,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
