#!/usr/bin/env python3
"""Audit parameter gates and pinned pilot/probe rows without executing a solver."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import (  # noqa: E402
    METRICS,
    compare,
    describe,
    observed_metric,
)
from scripts.qualification.arithmetic_combinations.audit import model_rows  # noqa: E402
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def gates(args):
    campaign = json.loads((args.configs / "campaign.json").read_text())
    assert campaign["tasks"] == campaign["protocol"]["tasks"]
    assert len(campaign["tasks"]) == 32
    records = []
    totals = collections.Counter()
    models = 0
    unknowns = []
    for phase in ("generated", "seeds", "mutations"):
        for slot, task in enumerate(campaign["tasks"]):
            d = args.rows / phase / f"task-{slot}"
            p = d / "receipt.json"
            r = json.loads(p.read_text())
            assert r["task"] == task and r["phase"] == phase
            assert r["script_sha256"] == sha(
                ROOT / "scripts/qualification/arithmetic_combinations/qualify.py"
            )
            assert (
                r["source"] == campaign["git"]
                and r["native_sha256"] == campaign["sha256"]
            )
            assert r["campaign_sha256"] == sha(args.configs / "campaign.json")
            if phase == "seeds":
                assert r["solver_args"] == task["args"] + ["--check-model"] and r[
                    "logics"
                ] == [task["logic"]]
                n = model_rows(d / "models", r["single_query_sat_models"])
                models += n
                scoped = (
                    json.loads((d / "scoped-models/summary.json").read_text())
                    if r["scoped_sessions"]
                    else []
                )
                assert len(scoped) == r["scoped_sessions"] and all(
                    x["outcome"] == "pass"
                    and all(q["outcome"] == "pass" for q in x["queries"])
                    for x in scoped
                )
                models += sum(
                    q["answer"] == "sat" for x in scoped for q in x["queries"]
                )
                totals["tracked_regression_measurements"] += r["cases"]
            else:
                name = "summary.csv" if phase == "generated" else "mutant-answers.csv"
                path = d / name
                rows = list(csv.DictReader(path.open()))
                assert sha(path) == r["summary_sha256"]
                assert len(rows) == r["cases"]
                assert (
                    dict(collections.Counter(x["outcome"] for x in rows))
                    == r["outcomes"]
                )
                allowed = (
                    {"PASS", "TIMEOUT", "UNKNOWN_INVOLVED", "REFERENCE_UNUSABLE"}
                    if phase == "generated"
                    else {"PASS", "INCOMPLETE"}
                )
                assert set(r["outcomes"]) <= allowed
                if phase == "generated":
                    assert (
                        len(rows) == 1000
                        and r["seed"]
                        == campaign["protocol"]["generated_seed_base"] + slot
                    )
                else:
                    assert len(rows) >= 100
                    original = list(csv.DictReader((d / "seed-answers.csv").open()))
                    assert len(original) == 12 and {x["outcome"] for x in original} <= {
                        "PASS",
                        "INCOMPLETE",
                    }
                    selected = {
                        x["file"]
                        for x in original
                        if x["ours"] == "sat" and x["ours_exit"] == "0"
                    }
                    models += model_rows(
                        d / "seed-models",
                        r["original_models"]["SAT_Dolmen_pass"],
                        selected,
                    )
                    unknowns.extend(
                        {"task_slot": slot, "file": x["file"], "kind": "original"}
                        for x in original
                        if x["ours"] == "unknown"
                    )
                selected = {
                    x.get("case") or x["file"]
                    for x in rows
                    if (x.get("ours") or x.get("answer")) == "sat"
                }
                models += model_rows(d / "models", r["SAT_Dolmen_pass"], selected)
                if r["SAT_Dolmen_pass"]:
                    assert sha(d / "models/summary.csv") == r["model_summary_sha256"]
                unknowns.extend(
                    {"task_slot": slot, "file": x["file"], "kind": "mutant"}
                    for x in rows
                    if phase == "mutations" and x["ours"] == "unknown"
                )
                totals[phase + "_inputs"] += len(rows)
                for key, n in r["outcomes"].items():
                    totals[phase + "_" + key] += n
            records.append(
                {
                    "phase": phase,
                    "slot": slot,
                    "logic": task["logic"],
                    "profile": task["label"],
                    "receipt_sha256": sha(p),
                    "cases": r["cases"],
                    "source": r["source"],
                    "native_sha256": r["native_sha256"],
                }
            )
    assert len(records) == 96
    private = {
        "source": campaign["git"],
        "native_sha256": campaign["sha256"],
        "counts": dict(totals),
        "SAT_Dolmen_pass": models,
        "receipt_inventory": records,
        "mutation_unknown_replays_needed": unknowns,
    }
    args.out.write_text(json.dumps(private, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: v
                for k, v in private.items()
                if k not in ("receipt_inventory", "mutation_unknown_replays_needed")
            },
            indent=2,
        )
    )
    print("Unknown replay candidates:", len(unknowns))


def rows(args):
    c = json.loads((args.configs / "campaign.json").read_text())
    assert c["tasks"] == c["protocol"]["tasks"]
    assert args.selection is not None
    selection = json.loads(args.selection.read_text())
    assert sha(args.selection) == c["protocol"]["selection_sha256"]
    assert len(selection["cases"]) == 40 and len(c["mapping"]) == 80
    task_args = {(t["logic"], t["label"]): t["args"] for t in c["tasks"]}
    records = {}
    receipts = []
    all_sat = passed = 0
    for rep in range(3):
        for allocation, mapping in enumerate(c["mapping"]):
            path = args.configs / f"{mapping['cohort']}-{rep}.json"
            config = json.loads(path.read_text())
            case = config["cases"][0]
            index, context = divmod(allocation, 2)
            assert mapping == {"cohort": f"case-{index}-r{context}", "slot": 0}
            assert case == dict(
                selection["cases"][index],
                pilot_slot=index,
                row_context=context,
                repetition=rep,
            )
            assert (
                config["timeout"] == 30 and config["address_space_bytes"] == 8589934592
            )
            d = args.rows / f"rep-{rep}/{mapping['cohort']}/slot-0"
            source = d / "rows.jsonl"
            if args.partial and not source.exists():
                continue
            assert args.partial or (d / "timing-complete").is_file()
            prov = json.loads((d / "provenance.json").read_text())
            assert (
                prov["config_sha256"] == sha(path)
                and prov["case"] == case
                and prov["profiles"] == config["profiles"]
            )
            assert prov["runner_sha256"] == sha(
                ROOT / "scripts/qualification/lia_hybrid/run.py"
            )
            actual = []
            lines = source.read_text().splitlines()
            for i, line in enumerate(lines):
                try:
                    actual.append(json.loads(line))
                except json.JSONDecodeError:
                    assert args.partial and i == len(lines) - 1
            assert [x["profile"] for x in actual] == measurement_order(config, 0)[
                : len(actual)
            ]
            if not args.partial:
                assert len(actual) == 8
            for r in actual:
                try:
                    audit_row(r)
                except AssertionError:
                    raise RuntimeError(
                        f"Native failure rep={rep} allocation={allocation} profile={r['profile']} exit={r['exit_code']}"
                    ) from None
                p = config["profiles"][r["profile"]]
                assert p["git"] == c["git"] and p["sha256"] == c["sha256"]
                assert (
                    p["args"] == task_args[case["logic"], f"r{context}-{r['profile']}"]
                )
                assert (
                    r["args"] == p["args"] + case.get("extra_args", [])
                    and r["file"] == case["file"]
                    and r["logic"] == case["logic"]
                )
                r["seconds"] = float(r["seconds"])
                assert math.isfinite(r["seconds"]) and r["seconds"] > 0
                if solved(r) and case.get("expected") in ("sat", "unsat"):
                    assert r["answer"] == case["expected"], (
                        rep,
                        allocation,
                        r["profile"],
                    )
                if (
                    r["status"] == "ok"
                    and r["answer"] == "unknown"
                    or r["answer"] == "error"
                ):
                    stderr = d / (r["profile"] + ".stderr")
                    if not (args.partial and not stderr.exists()):
                        text = [
                            x
                            for x in stderr.read_text().splitlines()
                            if x.strip() and not x.startswith(":")
                        ]
                        expected = (
                            "integer branch-and-bound node limit"
                            if r["status"] == "ok"
                            else "std::bad_alloc"
                        )
                        assert text == [expected], (rep, allocation, r["profile"], text)
                records[rep, allocation, r["profile"]] = r
            assert len({x["answer"] for x in actual if solved(x)}) <= 1
            selected = {
                x["profile"]: x for x in actual if solved(x) and x["answer"] == "sat"
            }
            all_sat += len(selected)
            if not args.partial:
                h = audit_models(d, selected, config, case)
                assert h is not None
                passed += len(selected)
                receipts.append(
                    {
                        "rep": rep,
                        "allocation": allocation,
                        "rows_sha256": sha(source),
                        "provenance_sha256": sha(d / "provenance.json"),
                        "coverage_sha256": sha(d / "coverage.json"),
                        "model_metadata_sha256": h,
                    }
                )
    for index in range(40):
        answers = {
            r["answer"]
            for (rep, allocation, _), r in records.items()
            if allocation // 2 == index and solved(r)
        }
        assert len(answers) <= 1, ("cross-profile answer disagreement", index)
    if args.partial:
        print(json.dumps({"audited_rows": len(records), "expected": 1920}))
        return
    assert len(records) == 1920 and all_sat == passed
    summaries = {}
    families = {}
    for logic in ("QF_LIA", "QF_UFLIA"):
        for row in range(2):
            slots = [
                i
                for i, m in enumerate(c["mapping"])
                if m["cohort"].endswith(f"-r{row}")
                and json.loads((args.configs / f"{m['cohort']}-0.json").read_text())[
                    "cases"
                ][0]["logic"]
                == logic
            ]
            profiles = list(
                json.loads(
                    (
                        args.configs / f"{c['mapping'][slots[0]]['cohort']}-0.json"
                    ).read_text()
                )["profiles"]
            )
            result = {"cases": len(slots), "profiles": {}, "comparisons": {}}
            for p in profiles:
                rr = [
                    describe([records[rep, i, p] for i in slots], 30)
                    for rep in range(3)
                ]
                result["profiles"][p] = {"repetitions": rr}
                for base in ("H1", "PVT"):
                    if p != base:
                        result["comparisons"][base + "->" + p] = compare(
                            records, slots, base, p, 30
                        )
            for before, after in (
                ("H16", "H4096"),
                ("H1k", "H16k"),
                ("H16k", "H4096k"),
                ("H1k", "H1"),
                ("H16k", "H16"),
                ("H4096k", "H4096"),
            ):
                result["comparisons"][before + "->" + after] = compare(
                    records, slots, before, after, 30
                )
            summaries[f"{logic}/r{row}"] = result
            groups = collections.defaultdict(list)
            for allocation in slots:
                case = selection["cases"][allocation // 2]
                groups["/".join(case["file"].split("/")[:2])].append(allocation)
            families[f"{logic}/r{row}"] = {
                family: {
                    "cases": len(group),
                    "profiles": {
                        p: [
                            {
                                k: v
                                for k, v in describe(
                                    [records[rep, i, p] for i in group], 30
                                ).items()
                                if k
                                in (
                                    "solved",
                                    "outcomes",
                                    "mean_PAR2_seconds",
                                    "median_RSS_KiB",
                                    "max_RSS_KiB",
                                )
                            }
                            for rep in range(3)
                        ]
                        for p in profiles
                    },
                }
                for family, group in sorted(groups.items())
            }
    report = {
        "status": "qualified pilot only",
        "source": c["git"],
        "native_sha256": c["sha256"],
        "analysis_script_sha256": sha(Path(__file__)),
        "campaign_sha256": sha(args.configs / "campaign.json"),
        "native_rows": len(records),
        "SAT_Dolmen_pass": passed,
        "receipts": receipts,
        "summaries": summaries,
        "families": families,
        "scope": "Selected40-input30sdevelopmentpilot, no global optimum/defaultclaim; all original scores intact.",
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    fields = [
        "repetition",
        "allocation",
        "pilot_slot",
        "parent_slot",
        "rows",
        "profile",
        "logic",
        "file",
        "status",
        "answer",
        "exit_code",
        "seconds",
        "peak_rss_kib",
        "solved",
        "PAR2_seconds",
        *METRICS,
    ]
    with (args.out / "measurements.csv").open("w") as stream:
        w = csv.DictWriter(stream, fieldnames=fields)
        w.writeheader()
        for (rep, i, p), r in sorted(records.items()):
            w.writerow(
                {
                    **{k: r[k] for k in fields if k in r},
                    "repetition": rep,
                    "allocation": i,
                    "pilot_slot": i // 2,
                    "parent_slot": selection["cases"][i // 2]["parent_slot"],
                    "rows": i % 2,
                    "solved": solved(r),
                    "PAR2_seconds": penalty(r, 30),
                    **{k: observed_metric(r, k) for k in METRICS},
                }
            )
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k not in ("receipts", "summaries", "families")
            },
            indent=2,
        )
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("gates", "timings"))
    p.add_argument("--configs", type=Path, required=True)
    p.add_argument("--rows", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--partial", action="store_true")
    p.add_argument("--selection", type=Path)
    a = p.parse_args()
    (gates if a.phase == "gates" else rows)(a)


if __name__ == "__main__":
    main()
