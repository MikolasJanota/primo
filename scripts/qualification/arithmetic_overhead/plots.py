#!/usr/bin/env python3
"""Plot accepted native timings against fresh and explicitly historical references.

Reads metadata only. Historical LRA input hashes must match the frozen current
selection. Historical LIA references have filename coverage only in this export;
the comparison is contextual, with that limitation printed on the figure.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import itertools
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.analyze_lia_cubes import cost, solved  # noqa: E402
from scripts.plot_cactus import order_series_by_solved, svg  # noqa: E402


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(name: str, logic: str) -> str:
    for prefix in (f"sandbox/non-incremental/{logic}/", f"tmp/{logic}/", f"{logic}/"):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lia-references", type=Path, required=True)
    parser.add_argument("--lra-references", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows_path = ROOT / "benchmarks/arithmetic-overhead-20261109/cache/rows.csv.gz"
    experiment = ROOT / "benchmarks/arithmetic-overhead-20261109/experiment.json"
    frozen = json.loads(experiment.read_text())["primary_cache_suites"]
    with gzip.open(rows_path, "rt") as stream:
        all_rows = list(csv.DictReader(stream))
    output = {"current_source": "cab1ebc45fdf5d93940b36f574e2675be7ef4b4b",
              "kind": "existing-timings-only; contextual historical-reference comparison",
              "timeout_seconds": 60, "source_sha256": {str(rows_path.relative_to(ROOT)): digest(rows_path),
                                str(experiment.relative_to(ROOT)): digest(experiment)},
              "limitations": ["Historical references differ in date and memory allocation.",
                              "LIA historical input content hashes are unavailable in this export; filenames match.",
                              "LRA historical 1200s timings are censored at60s, not fresh60s runs.",
                              "No new solver execution or model-validation claim."], "suites": {}}
    exported = []
    for suite, logic in [("lia-development", "QF_LIA"), ("lra-selection60", "QF_LRA")]:
        expected = {normalize(x["logical_file"], logic): x["sha256"] for x in frozen[suite]["inputs"]}
        selected = [r for r in all_rows if r["suite"] == suite and r["repetition"] == "0"]
        profiles = {"candidate-default": "Primo default", "z3": "Z3 5.1.0"}
        if logic == "QF_LIA":
            profiles["candidate-m124-uniform"] = "Primo M124+uniform"
        series = {}
        origins = {}
        for profile, label in profiles.items():
            series[label] = {normalize(r["file"], logic): dict(r) for r in selected if r["profile"] == profile}
            origins[label] = {"date": "2026-11-09", "memory_requested_GiB": 12,
                              "CPU_requested": 1, "profile": profile,
                              "identity": "frozen input SHA256; prior complete timing/model audit"}
        references = {"opensmt": "OpenSMT 2.9.2*", "cvc5": "cvc5 1.3.4*"}
        if logic == "QF_LRA":
            references["yices"] = "Yices 2.7.0*"
        for solver, label in references.items():
            base = (args.lia_references if logic == "QF_LIA" else args.lra_references) / solver
            paths = sorted(base.glob("*.csv")) if logic == "QF_LIA" else sorted(base.glob("shard-*/answers.csv"))
            paths = [p for p in paths if not p.stem.endswith("-summary")]
            values = {}
            hashes = {}
            for path in paths:
                if logic == "QF_LRA":
                    proof = json.loads(path.with_name("provenance.json").read_text())
                    assert proof["status"] == "complete" and proof["inputs_unchanged"]
                    assert proof["timeout"] == 1200
                    for name, value in proof["inputs"].items():
                        assert name not in hashes
                        hashes[name] = value
                accepted = False
                for row in csv.DictReader(path.open()):
                    name = normalize(row["file"], logic)
                    if name not in expected:
                        continue
                    assert name not in values, (solver, name)
                    values[name] = dict(row)
                    accepted = True
                if accepted:
                    key = str(Path("historical") / logic / solver / path.parent.name / path.name)
                    output["source_sha256"][key] = digest(path)
            assert set(values) == set(expected), (logic, solver, "missing inputs")
            if logic == "QF_LRA":
                assert hashes == expected, "historical/current LRA input-byte mismatch"
            series[label] = values
            origins[label] = {"date": "2026-10-10" if logic == "QF_LRA" else
                              ("2026-10-09" if solver == "opensmt" else "2026-08-21"),
                              "memory_requested_GiB": 7 if logic == "QF_LRA" else 3,
                              "CPU_requested": 1, "original_timeout_seconds": 1200 if logic == "QF_LRA" else 60,
                              "identity": "input SHA256 matches current" if logic == "QF_LRA" else "filenames only"}
        assert all(set(values) == set(expected) for values in series.values())
        for label, values in series.items():
            for row in values.values():
                assert row["wrong"] == "false" and row["status"] in {"ok", "timeout"}, (label, row)
                assert math.isfinite(float(row["seconds"])) and float(row["seconds"]) >= 0
                if row["status"] == "ok":
                    assert row["exit_code"] == "0" and row["timeout"] == "false"
        for left, right in itertools.combinations(series.values(), 2):
            for name in expected:
                a, b = left[name], right[name]
                if all(r["status"] == "ok" and r["answer"] in {"sat", "unsat"} for r in [a, b]):
                    assert a["answer"] == b["answer"], (logic, name, "answer disagreement")
        metrics = {}
        curves = []
        for label, values in series.items():
            rows = list(values.values())
            metrics[label] = cost(rows, 60)
            metrics[label]["provenance"] = origins[label]
            curves.append((label, sorted(float(r["seconds"]) for r in rows if solved(r, 60))))
            for name, row in sorted(values.items()):
                exported.append(dict(suite=suite, series=label, file=f"{logic}/{name}",
                                     **{k: row[k] for k in ["status", "answer", "seconds", "exit_code", "timeout", "wrong"]}))
        notes = [f"{len(expected)} matched inputs; 60s per-instance cutoff; no easy-input filter.",
                 "Current Primo: accepted cab1ebc4; CITE reverted. Native SAT rows separately Dolmen-validated.",
                 "Fresh native/Z3: 9 Nov, 12 GiB requested. * Archived references: different dates/resources."]
        if logic == "QF_LIA":
            notes += ["* OpenSMT: 9 Oct; cvc5: 21 Aug; both 3 GiB. Archived input identity: filenames only.",
                      "M124+uniform is an optional static profile; default is highlighted. No per-input portfolio."]
        else:
            notes += ["* References: 10 Oct, 7 GiB; input SHA256 matches. Original 1200s runs censored at60s."]
        path = args.out / (logic.lower() + "-60.svg")
        path.write_text(svg(order_series_by_solved(curves), 60, len(expected),
                            f"{logic}: current Primo vs reference solvers", notes,
                            highlighted="Primo default"))
        output["suites"][suite] = {"inputs": len(expected), "series": metrics,
                                    "plot": path.name, "plot_sha256": digest(path)}
    buffer = io.StringIO()
    fields = ["suite", "series", "file", "status", "answer", "seconds", "exit_code", "timeout", "wrong"]
    writer = csv.DictWriter(buffer, fieldnames=fields)
    writer.writeheader()
    writer.writerows(exported)
    (args.out / "rows.csv.gz").write_bytes(gzip.compress(buffer.getvalue().encode(), mtime=0))
    (args.out / "summary.json").write_text(json.dumps(output, indent=2) + "\n")
    for suite, data in output["suites"].items():
        print(suite, {label: values["solved"] for label, values in data["series"].items()})


if __name__ == "__main__":
    main()
