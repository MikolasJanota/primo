#!/usr/bin/env python3
"""Build a ground-theory gallery from qualified metadata; never execute solvers."""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import html
import json
import math
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.plot_cactus import order_series_by_solved, svg  # noqa: E402

LOGICS = ("QF_UF", "QF_LRA", "QF_LIA", "QF_UFLRA", "QF_UFLIA")
REFERENCES = {"z3": "Z3", "cvc5": "cvc5", "yices": "Yices", "opensmt": "OpenSMT"}
COLOURS = {"Primo": "#0057b8", "Primo hybrid": "#0057b8", "Primo private": "#6b4c9a",
           "Z3": "#b14f14", "cvc5": "#00805e", "Yices": "#8a6500", "OpenSMT": "#b04c83"}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict:
    return json.loads(path.read_text())


def solved(row: dict) -> bool:
    return (row["status"] == "ok" and str(row["exit_code"]) == "0"
            and row["answer"] in {"sat", "unsat"} and float(row["seconds"]) <= 120)


def metrics(rows: list[dict]) -> dict:
    good = [row for row in rows if solved(row)]
    def memory(row: dict) -> bool:
        return row.get("memory_exhausted") == "true" or row.get("memory_out") is True

    return {
        "inputs": len(rows), "solved": len(good),
        "sat": sum(row["answer"] == "sat" for row in good),
        "unsat": sum(row["answer"] == "unsat" for row in good),
        "timeouts": sum(row["status"] == "timeout" for row in rows),
        "unknown": sum(row["status"] == "ok" and row["answer"] == "unknown" for row in rows),
        "memory_out": sum(memory(row) for row in rows),
        "unsupported": sum(row["status"] == "unsupported" for row in rows),
        "errors": sum(row["status"] not in {"ok", "timeout", "unsupported"} and not memory(row) for row in rows),
        "par2_sum": math.fsum(float(row["seconds"]) if solved(row) else 240 for row in rows),
    }


def audit_series(series: dict[str, dict[str, dict]]) -> None:
    hashes = None
    answers = {}
    for rows in series.values():
        current = {name: row["sha256"] for name, row in rows.items()}
        assert hashes is None or current == hashes, "different input cohorts or bytes"
        hashes = current
        for name, row in rows.items():
            assert row.get("wrong", "false") == "false"
            assert math.isfinite(float(row["seconds"])) and float(row["seconds"]) >= 0
            if solved(row):
                assert answers.setdefault(name, row["answer"]) == row["answer"], "answer disagreement"


def full_corpora() -> tuple[dict, dict]:
    base = ROOT / "benchmarks/uniform120-20261109/report"
    proof, summary = load(base / "provenance.json"), load(base / "summary.json")
    assert summary["status"] == "complete" and summary["ground_models_complete"]
    assert sha(base / "summary.json") == proof["qualified_audit_sha256"]
    assert sha(base / "timing.csv.gz") == proof["timing_table_sha256"]
    data = {}
    with gzip.open(base / "timing.csv.gz", "rt") as stream:
        for row in csv.DictReader(stream):
            rows = data.setdefault(row["logic"], {}).setdefault(row["profile"], {})
            assert row["file"] not in rows, "duplicate timing row"
            rows[row["file"]] = row
    assert set(data) == set(LOGICS)
    for logic, series in data.items():
        audit_series(series)
        for profile, rows in series.items():
            actual, expected = metrics(list(rows.values())), summary["runs"][logic][profile]
            for key in ("inputs", "solved", "sat", "unsat", "timeouts", "unknown", "memory_out", "unsupported", "errors"):
                assert actual[key] == expected[key], (logic, profile, key)
            assert math.isclose(actual["par2_sum"], expected["par2_sum"], abs_tol=1e-6)
            if profile.startswith("primo"):
                assert expected["direct_Dolmen_models"] == actual["sat"]
    return data, proof


def hybrid_selections(full: dict) -> dict:
    base = ROOT / "benchmarks/lia-hybrid-20261111"
    qualification = load(base / "qualification.json")
    for name in ("paired/timing-rows.jsonl", "paired/summary.json", "selection.json"):
        assert sha(base / name) == qualification["artifact_sha256"][name]
    selection, summary = load(base / "selection.json"), load(base / "paired/summary.json")
    assert not summary["model_coverage_incomplete_slots"]
    records = [json.loads(line) for line in (base / "paired/timing-rows.jsonl").read_text().splitlines()]
    assert len(records) == len(selection["cases"]) == 512
    result = {}
    for slot, record in enumerate(records):
        assert record["slot"] == slot and record["case"] == selection["cases"][slot]
        case = record["case"]
        if case["group"] == "certora":
            continue
        assert case["group"] in {"lia", "uflia"} and case["repetition"] == 0
        profiles = result.setdefault(case["logic"], {label: {} for label in ("Primo hybrid", "Primo private", *REFERENCES.values())})
        assert len(record["rows"]) == 2 and {row["profile"] for row in record["rows"]} == {"hybrid", "private"}
        sat = {row["profile"] for row in record["rows"] if solved(row) and row["answer"] == "sat"}
        assert record["coverage"]["selected_sat"] == record["coverage"]["direct_dolmen_pass"] == len(sat)
        assert {row["profile"] for row in record["coverage"]["rows"]} == sat
        for row in record["rows"]:
            assert row["binary_sha256"] == qualification["binaries"][row["profile"]]
            label = "Primo " + row["profile"]
            assert case["file"] not in profiles[label]
            profiles[label][case["file"]] = dict(row, sha256=case["sha256"])
        for profile, label in REFERENCES.items():
            row = full[case["logic"]][profile][case["file"]]
            assert row["sha256"] == case["sha256"]
            profiles[label][case["file"]] = row
    for logic, series in result.items():
        audit_series(series)
        group = "lia" if logic == "QF_LIA" else "uflia"
        for profile in ("hybrid", "private"):
            assert metrics(list(series["Primo " + profile].values()))["solved"] == summary["groups"][group][profile]["solved"]
    assert {logic: len(series["Primo hybrid"]) for logic, series in result.items()} == {"QF_LIA": 200, "QF_UFLIA": 300}
    return result


def table_rows(scores: dict, cohort: int) -> str:
    rows = []
    for label, score in sorted(scores.items(), key=lambda item: (-item[1]["solved"], item[1]["par2_sum"], item[0])):
        cells = [f'<th scope="row">{html.escape(label)}</th>', f'<td>{score["solved"]:,} / {cohort:,}</td>',
                 f'<td>{score["par2_sum"] / cohort:.3f}</td>' if cohort else '<td>—</td>']
        cells += [f'<td>{score[key]:,}</td>' for key in ("sat", "unsat", "timeouts", "unknown", "memory_out", "unsupported", "errors")]
        rows.append(f'<tr class="{"native" if label.startswith("Primo") else "reference"}">{"".join(cells)}</tr>')
    return "".join(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "docs/_static/ground-comparison")
    parser.add_argument("--provenance", type=Path, default=ROOT / "benchmarks/ground-comparison-20261112/provenance.json")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    full, proof = full_corpora()
    hybrids = hybrid_selections(full)
    panels, overview, report = [], [], {"timeout_seconds": 120, "memory_gib": 8, "cohorts": {}}
    for group in ("full", "hybrid"):
        report["cohorts"][group] = {}
        for logic in LOGICS if group == "full" else ("QF_LIA", "QF_UFLIA"):
            if group == "full":
                scores = {label: metrics(list(rows.values())) for label, rows in full[logic].items()}
                chosen = min((label for label in scores if label.startswith("primo")),
                             key=lambda label: (-scores[label]["solved"], scores[label]["par2_sum"], label))
                series = {"Primo": full[logic][chosen], **{label: full[logic][profile] for profile, label in REFERENCES.items()}}
                description = ("Full corpus · native measured 9 Nov 2026 · Primo 7dd8874c (implementation cab1ebc4). "
                               + ("Fixed integer profile." if chosen == "primo-integer" else "Default profile."))
                arguments = proof["configuration"]["native_profiles"][chosen]
                highlighted = "Primo"
            else:
                chosen, series = "hybrid + private", hybrids[logic]
                description = ("Selected inputs · native measured 12 Nov 2026 · hybrid 9e01197e vs private c49585a0. "
                               "Hybrid is opt-in; factor 1, unlimited generated atoms.")
                arguments = {"hybrid": ["--quiet", "--stats", "--lia-branching=hybrid"], "private": ["--quiet", "--stats"]}
                highlighted = "Primo hybrid"
            names = set(next(iter(series.values())))
            easy = {name for name in names if all(solved(rows[name]) and float(rows[name]["seconds"]) < 1 for rows in series.values())}
            report["cohorts"][group][logic] = {"profile": chosen, "arguments": arguments, "description": description, "views": {}}
            for view, cohort in (("all", names), ("hard", names - easy)):
                scores = {label: metrics([rows[name] for name in sorted(cohort)]) for label, rows in series.items()}
                report["cohorts"][group][logic]["views"][view] = {"inputs": len(cohort), "removed_easy": len(easy) if view == "hard" else 0,
                    "input_cohort_sha256": hashlib.sha256(json.dumps(sorted((name, series[highlighted][name]["sha256"]) for name in cohort)).encode()).hexdigest(), "series": scores}
                curves = [(label, sorted(float(rows[name]["seconds"]) for name in cohort if solved(rows[name]))) for label, rows in series.items()]
                filename = f"{group}-{view}-{logic.lower()}.svg"
                caption = [f'{len(cohort):,} inputs; 120s cutoff; enforced 8 GiB address space; one physical CPU.',
                           "Native: " + ("9 Nov 2026, 7dd8874c; one fixed profile per logic." if group == "full" else "12 Nov 2026; hybrid 9e01197e vs private c49585a0."),
                           "References: 24 Oct 2026, exact input hashes matched; 1200s runs censored at 120s.",
                           f'{len(easy) if view == "hard" else 0:,} shared easy inputs removed (<1s by every displayed solver). Native SAT models checked separately.']
                (args.out / filename).write_text(svg(order_series_by_solved(curves), 120, len(cohort), f'{logic}: {"all inputs" if view == "all" else "shared easy inputs removed"}', caption,
                                                       highlighted=highlighted, colour_by_name=COLOURS, percentage=True))
                identity = f"{group}-{view}-{logic}"
                hidden = "" if group == "full" and view == "all" else " hidden"
                panels.append(f'''<section class="panel" id="{identity}" data-group="{group}" data-view="{view}"{hidden}>
<div class="panel-heading"><h2>{logic}</h2><span class="badge">{len(cohort):,} inputs</span></div>
<p class="muted">{html.escape(description)}</p>
<div class="chart" tabindex="0" aria-label="{logic} cactus chart; scroll horizontally on small screens"><img src="{filename}" width="900" height="574" alt="{logic} cactus comparison; solved percentages versus per-instance time budget"></div>
<div class="chart-links"><a href="{filename}">Open SVG</a><a href="{filename}" download>Download SVG</a></div>
<details><summary>Results, resource outcomes and profile arguments</summary><p>All numbers below use the displayed input cohort. PAR2 mean is seconds per input; unsolved costs 240 seconds.</p>
<div class="table-scroll" tabindex="0"><table><thead><tr><th scope="col">Solver</th><th scope="col">Solved</th><th scope="col">PAR2 mean (s)</th><th scope="col">SAT</th><th scope="col">UNSAT</th><th scope="col">Timeout</th><th scope="col">Unknown</th><th scope="col">Memory out</th><th scope="col">Unsupported</th><th scope="col">Other error</th></tr></thead><tbody>{table_rows(scores, len(cohort))}</tbody></table></div>
<p><strong>Primo arguments:</strong> <code>{html.escape(json.dumps(arguments))}</code></p></details></section>''')
        for view in ("all", "hard"):
            rows = []
            for logic, data in report["cohorts"][group].items():
                values = data["views"][view]
                native = "Primo" if group == "full" else "Primo hybrid"
                values_html = "".join(f'<td>{values["series"][label]["solved"]:,}</td>' for label in (native, *REFERENCES.values()))
                private = f'<td>{values["series"]["Primo private"]["solved"]:,}</td>' if group == "hybrid" else ""
                rows.append(f'<tr><th scope="row"><a href="#{group}-{view}-{logic}">{logic}</a></th><td>{values["inputs"]:,}</td>{values_html}{private}</tr>')
            hidden = "" if group == "full" and view == "all" else " hidden"
            overview.append(f'<div class="overview" data-group="{group}" data-view="{view}"{hidden}><h2>Solved inputs</h2><div class="table-scroll" tabindex="0"><table><thead><tr><th scope="col">Logic</th><th scope="col">Inputs</th><th scope="col">{"Primo hybrid" if group == "hybrid" else "Primo"}</th><th scope="col">Z3</th><th scope="col">cvc5</th><th scope="col">Yices</th><th scope="col">OpenSMT</th>{"<th scope=col>Primo private</th>" if group == "hybrid" else ""}</tr></thead><tbody>{"".join(rows)}</tbody></table></div></div>')
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    template = Path(__file__).with_name("gallery.html").read_text()
    page = template.replace("@@OVERVIEW@@", "\n".join(overview)).replace("@@PANELS@@", "\n".join(panels))
    (args.out / "index.html").write_text(page)
    source_paths = [Path(__file__), Path(__file__).with_name("gallery.html"), ROOT / "scripts/plot_cactus.py",
                    ROOT / "benchmarks/uniform120-20261109/report/timing.csv.gz", ROOT / "benchmarks/uniform120-20261109/report/summary.json",
                    ROOT / "benchmarks/uniform120-20261109/report/provenance.json", ROOT / "benchmarks/lia-hybrid-20261111/qualification.json",
                    ROOT / "benchmarks/lia-hybrid-20261111/paired/timing-rows.jsonl", ROOT / "benchmarks/lia-hybrid-20261111/paired/summary.json", ROOT / "benchmarks/lia-hybrid-20261111/selection.json"]
    provenance = {"kind": "derived qualified metadata; zero new solver executions", "command": "python3 scripts/qualification/ground_comparison/report.py",
                  "cohorts": "Full-corpus archive across all five supported ground divisions; separate latest hybrid selections for LIA/UFLIA. No per-input Primo portfolio.",
                  "native_dates": {"full": "2026-11-09", "hybrid": "2026-11-12"}, "reference_date": "2026-10-24",
                  "reference_versions": {label: proof["tools"][profile]["version"] for profile, label in REFERENCES.items()},
                  "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in source_paths},
                  "output_sha256": {p.name: sha(p) for p in sorted(args.out.iterdir()) if p.is_file() and p.name != "provenance.json"}}
    args.provenance.parent.mkdir(parents=True, exist_ok=True)
    args.provenance.write_text(json.dumps(provenance, indent=2) + "\n")
    shutil.copyfile(args.provenance, args.out / "provenance.json")
    print("Wrote", args.out, "with 14 audited plots; no solver execution")


if __name__ == "__main__":
    main()
