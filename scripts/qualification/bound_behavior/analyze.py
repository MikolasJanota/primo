#!/usr/bin/env python3
"""Audit and export the fixed, selected bound-behavior diagnostic receipts.

Reads completed SLURM artifacts only; never executes a solver. Corpus inputs
stay in ignored results. Public exports contain their digests and six-bound
ablations, plus newly written small inputs, rather than copies of the corpus.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.bound_behavior.diagnose import append_bounds  # noqa: E402
from scripts.validate_qfuf_models_with_dolmen import (  # noqa: E402
    model_problem_text,
    solver_problem_text,
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path: Path):
    return json.loads(path.read_text())


def write(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def audit_model(directory: Path, row: dict, source: Path) -> dict:
    """Require direct validation of the exact adapted input and full flags."""
    metadata = read(directory / "metadata.json")
    assert metadata["status"] == "complete" and metadata["completed"] == 1
    assert metadata["solver_args"] == [*row["args"], "--check-model"]
    assert metadata["require_dolmen_pass"] and metadata["expect_sat"]
    assert metadata["dolmen_args"] == ["--strict=false"]
    assert metadata["outcomes"] == {"pass": 1}
    assert metadata["validators"] == {"dolmen": 1}
    with (directory / "summary.csv").open(newline="") as stream:
        summaries = list(csv.DictReader(stream))
    assert len(summaries) == 1
    summary = summaries[0]
    assert summary["answer"] == "sat" and summary["outcome"] == "pass"
    assert summary["validator"] == "dolmen"
    assert summary["solver_exit"] == summary["dolmen_exit"] == "0"
    assert not summary["message"]
    assert digest(source) == row["input_sha256"]
    solver_input = next(directory.glob("*.primo-input.smt2"))
    problem = next(directory.glob("*.smt2.smt2"))
    assert solver_input.read_text() == solver_problem_text(source.read_text())
    assert problem.read_text() == model_problem_text(source.read_text())
    return {
        "outcome": "pass",
        "validator": "dolmen",
        "solver_exit": 0,
        "dolmen_exit": 0,
        "replay_args": metadata["solver_args"],
        "artifacts_sha256": {
            p.name: digest(p) for p in sorted(directory.iterdir()) if p.is_file()
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    raw, out = args.results, args.out
    protocol = read(out / "protocol.json")
    cases = {c["slot"]: c for c in protocol["cases"]}
    records, receipts = [], []
    bounds = read(raw / "attempt2/task-0/bounds.json")
    micros = out / "micros"
    micros.mkdir(exist_ok=True)
    for attempt, task, slot, count in [(1, 0, None, 36), (2, 4, 438, 15), (2, 5, 4, 3)]:
        directory = raw / f"attempt{attempt}/task-{task}"
        provenance = read(directory / "provenance.json")
        assert provenance["native_sha256"] == protocol["native_sha256"]
        assert provenance["source_commit"] == protocol["source_base"]
        receipts.append({"attempt": attempt, "task": task, **provenance})
        rows = [
            json.loads(line)
            for line in (directory / "rows.jsonl").read_text().splitlines()
        ]
        assert len(rows) == count
        assert len({r["label"] for r in rows}) == count
        for row in rows:
            assert row["status"] == "ok" and row["answer"] == "sat"
            assert row["exit_code"] == 0 and row["timeout"] == "false"
            label = row["label"]
            if slot is None:
                # Profile suffixes have one or two words.
                for suffix in ("-pre-reverse", "-reverse", "-forward"):
                    if label.endswith(suffix):
                        source = directory / f"{label.removesuffix(suffix)}.smt2"
                        break
                (micros / source.name).write_bytes(source.read_bytes())
            else:
                source = directory / f"{label}.smt2"
                if not source.is_file():
                    source = raw / f"originals/{slot}.smt2"
                    assert digest(source) == cases[slot]["sha256"]
            if slot is not None and source.parent == directory:
                selected = bounds[str(slot)]
                if label == "fixed-as-equality":
                    selected = ["(= arg0 (- 19))", *selected[2:]]
                elif label.startswith("drop-"):
                    index = int(label.removeprefix("drop-"))
                    selected = selected[:index] + selected[index + 1 :]
                else:
                    assert label == "explicit-all"
                assert source.read_text() == append_bounds(
                    (raw / f"originals/{slot}.smt2").read_text(), selected
                )
                assert (directory / f"{label}.reference").read_text().strip() == "sat"
            validation = audit_model(directory / "models" / label, row, source)
            records.append(
                {
                    "attempt": attempt,
                    "task": task,
                    "slot": slot,
                    **{k: v for k, v in row.items() if k != "stderr_head"},
                    "validation": validation,
                }
            )
    assert len(list(micros.glob("*.smt2"))) == 12
    inspection = raw / "attempt2/task-0"
    bounds = read(inspection / "bounds.json")
    proofs = []
    for slot in cases:
        answer = (inspection / f"proof-{slot}.reference").read_text().strip()
        assert answer == (
            "unsat" if slot in (4, 438, 213) else "timeout:30seconds;not a proof pass"
        )
        proofs.append(
            {
                "slot": slot,
                "bounds_count": len(bounds[str(slot)]),
                "reference": answer,
                "proof_sha256": digest(inspection / f"proof-{slot}.smt2"),
            }
        )
    profiles = []
    patterns = [
        "compute_implied_bound(",
        "compute_implied_child_bound(",
        "LraTableau::check(",
        "LraTableau::pivot(",
        "branch_and_bound(",
    ]
    for task in (1, 2, 3):
        directory = raw / f"attempt2/task-{task}"
        command = read(directory / "profile-command.json")
        assert command["partial"] == (task != 1) and command["exit"] == 0
        text = (directory / "callgrind-full.txt").read_text()
        total = re.search(r"([\d,]+) \(100.0%\)  PROGRAM TOTALS", text)
        assert total
        functions = {}
        for pattern in patterns:
            lines = [line for line in text.splitlines() if pattern in line]
            if not lines:
                functions[pattern] = None  # Absent from the complete annotation.
                continue
            assert len(lines) == 1, (task, pattern, lines)
            match = re.match(r"\s*([\d,]+) \(\s*([\d.]+)%\)", lines[0])
            assert match
            functions[pattern] = {
                "Ir": int(match[1].replace(",", "")),
                "percent": float(match[2]),
            }
        profiles.append(
            {
                "task": task,
                "slot": 4 if task == 3 else 438,
                "partial": command["partial"],
                "collection_wall_cap_seconds": 60,
                "instruction_events": int(total[1].replace(",", "")),
                "inclusive_functions": functions,
                "raw_profile_sha256": digest(directory / command["profile"]),
                "annotation_sha256": digest(directory / "callgrind-full.txt"),
                "provenance": read(directory / "provenance.json"),
            }
        )
    trace = read(raw / "root-bound-trace/trace.json")
    assert trace["job"] == "392150" and trace["source_snapshot"] == "6936bfbe"
    for row in trace["rows"]:
        assert (
            row["status"] == "ok"
            and row["answer"] == "unknown"
            and row["exit_code"] == 0
        )
    trace["rows"] = [
        {k: r[k] for k in ("profile", "args", "answer", "exit_code")}
        for r in trace["rows"]
    ]
    trace["lines"] = {
        profile: [
            line
            for line in (raw / f"root-bound-trace/{profile}.stderr")
            .read_text()
            .splitlines()
            if line.startswith("TRACE_FMT1 ")
        ]
        for profile in ("rows", "both")
    }
    assert "asserted=18 delta=0 before=18 after=18" in "\n".join(trace["lines"]["both"])
    write(
        out / "observations.json",
        {
            "source_base": protocol["source_base"],
            "native_sha256": protocol["native_sha256"],
            "scope": "Selected diagnostics; contended wall times; no default or general speed claim",
            "native_sat_rows": 54,
            "direct_dolmen_passes": 54,
            "micros": 12,
            "receipts": receipts,
            "rows": records,
            "bound_proofs": proofs,
            "ablation_bounds": {slot: bounds[slot] for slot in ("4", "438")},
            "profiles": profiles,
            "root_bound_trace": trace,
        },
    )
    counters = [
        ":lra.pivots",
        ":lra.row-propagations",
        ":lra.bidirectional-row-propagations",
        ":sat.decisions",
        ":lia.branching.private-nodes",
        ":combination.model.complete-fallbacks",
        ":combination.model.materialized-pairs",
    ]
    with (out / "measurements.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=["slot", "label", "seconds", *counters]
        )
        writer.writeheader()
        for record in records:
            writer.writerow(
                {
                    **{k: record[k] for k in ("slot", "label", "seconds")},
                    **{k: record["stats"].get(k, "0") for k in counters},
                }
            )
    (out / "accounting.psv").write_bytes((raw / "accounting.psv").read_bytes())
    print(
        "audited 54 SAT rows, 54 direct Dolmen replays, 3 profiles and bounded root-bound trace"
    )


if __name__ == "__main__":
    main()
