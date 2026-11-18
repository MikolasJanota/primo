#!/usr/bin/env python3
"""Audit exact-option correctness and immutable paired conflict-Ackermann rows."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import compare  # noqa: E402
from scripts.qualification.arithmetic_combinations.audit import model_rows  # noqa: E402
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    outcome,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402

ACK = tuple(
    ":euf.conflict-ack." + k
    for k in (
        "conflicts",
        "trace-steps",
        "trace-kept",
        "trace-truncations",
        "prefix-steps",
        "semantic-pairs",
        "duplicate-pairs",
        "catalog-limit",
        "width-skips",
        "nonmatching-pairs",
        "tautologies",
        "missing-result",
        "missing-argument",
        "ready-clauses",
        "queued-clauses",
        "sent-clauses",
        "sent-lits",
        "peak-pairs",
        "peak-pending",
    )
)
METRICS = ACK + (
    ":euf.incremental.congruence-merges",
    ":euf.incremental.explain-congruence-edges",
    ":euf.incremental.explain-length",
    ":sat.conflicts",
    ":sat.decisions",
    ":sat.propagations",
    ":theory.conflicts",
    ":lia.branching.private-nodes",
    ":time.lia.branching.private-completion-us",
    ":time.qf-uflia.euf-check-us",
    ":lra.pivots",
)


def metric(row, key):
    if row["profile"] == "BASE" and key in ACK:
        return None
    if solved(row) and row["stats"]:
        return int(row["stats"].get(key, 0))
    return int(row["stats"][key]) if key in row["stats"] else None


def describe(rows):
    good = [r for r in rows if solved(r)]
    return {
        "solved": len(good),
        "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
        "mean_PAR2_seconds": st.mean(penalty(r, 30) for r in rows),
        "median_RSS_KiB": st.median(r["peak_rss_kib"] for r in rows),
        "max_RSS_KiB": max(r["peak_rss_kib"] for r in rows),
        "successful_counter_totals": {
            k: sum(v)
            if (v := [x for r in good if (x := metric(r, k)) is not None])
            else None
            for k in METRICS
            if k not in ACK[-2:]
        },
        "successful_storage_peaks": {
            k: max(v)
            if (v := [x for r in good if (x := metric(r, k)) is not None])
            else None
            for k in ACK[-2:]
        },
        "incomplete_printed_snapshots": {
            k: {"rows": len(v), "sum": sum(v)}
            for k in METRICS
            if (
                v := [
                    x for r in rows if not solved(r) and (x := metric(r, k)) is not None
                ]
            )
        },
        "scope": "Totals conditional on successful rows; storage peaks are maxima; incomplete printed snapshots separate; absent incomplete counters unavailable.",
    }


def paired(records, slots, before, after):
    report = compare(records, slots, before, after, 30)
    common = [
        i
        for i in slots
        if all(solved(records[r, i, p]) for r in range(3) for p in (before, after))
    ]
    report["matched_mechanism_counters"] = {
        "cases": len(common),
        "scope": "Same cases solved in all three repetitions by both profiles; sums across three repetitions except storage peaks (maxima). BASE Ackermann counters unavailable.",
        "profiles": {
            p: {
                k: (max(v) if k in ACK[-2:] else sum(v))
                if (
                    v := [
                        x
                        for i in common
                        for r in range(3)
                        if (x := metric(records[r, i, p], k)) is not None
                    ]
                )
                else None
                for k in METRICS
            }
            for p in (before, after)
        },
    }
    return report


def gates(args):
    campaign = json.loads((args.configs / "campaign.json").read_text())
    assert campaign["tasks"] == campaign["protocol"]["tasks"]
    assert len(campaign["tasks"]) >= 6
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
                ROOT / "scripts/qualification/conflict_ackermann/qualify.sbatch"
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
                    assert (
                        r["seed"] == campaign["protocol"]["mutation_seed_base"] + slot
                    )
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
    assert len(records) == 3 * len(campaign["tasks"])
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


def timings(a):
    campaign = json.loads((a.configs / "timing-campaign.json").read_text())
    selection = json.loads(a.selection.read_text())["cases"]
    assert len(selection) == 80 and len(campaign["mapping"]) == 80
    assert sha(a.selection) == campaign["protocol"]["selection_sha256"]
    records = {}
    receipts = []
    sat = 0
    for rep in range(3):
        for i, c in enumerate(selection):
            config_path = a.configs / f"case-{i}-{rep}.json"
            config = json.loads(config_path.read_text())
            case = dict(c, pilot_slot=i, repetition=rep)
            assert (
                config["cases"] == [case]
                and config["timeout"] == 30
                and config["address_space_bytes"] == 8589934592
            )
            d = a.rows / f"rep-{rep}/case-{i}/slot-0"
            source = d / "rows.jsonl"
            if a.partial and (
                not source.exists() or not (d / "provenance.json").exists()
            ):
                continue
            assert a.partial or (d / "timing-complete").exists()
            prov = json.loads((d / "provenance.json").read_text())
            assert (
                prov["config_sha256"] == sha(config_path)
                and prov["case"] == case
                and prov["profiles"] == config["profiles"]
            )
            assert prov["runner_sha256"] == sha(
                ROOT / "scripts/qualification/lia_hybrid/run.py"
            )
            rr = []
            lines = source.read_text().splitlines()
            for line_index, line in enumerate(lines):
                try:
                    rr.append(json.loads(line))
                except json.JSONDecodeError:
                    assert a.partial and line_index == len(lines) - 1
            assert [r["profile"] for r in rr] == measurement_order(config, 0)[: len(rr)]
            if not a.partial:
                assert len(rr) == 4
            for r in rr:
                try:
                    audit_row(r)
                except AssertionError:
                    raise RuntimeError(
                        f"Native failure rep={rep} case={i} profile={r['profile']} exit={r['exit_code']}"
                    ) from None
                p = config["profiles"][r["profile"]]
                assert (
                    r["args"] == p["args"]
                    and r["file"] == c["file"]
                    and r["logic"] == c["logic"]
                )
                if r["profile"] == "BASE":
                    assert (
                        p["git"] == "4f02a8b7"
                        and p["sha256"] == campaign["protocol"]["baseline_sha256"]
                        and p["args"] == ["--quiet", "--stats"]
                    )
                else:
                    assert (
                        p["git"] == campaign["git"]
                        and p["sha256"] == campaign["sha256"]
                        and p["args"] == campaign["protocol"]["profiles"][r["profile"]]
                    )
                r["seconds"] = float(r["seconds"])
                assert math.isfinite(r["seconds"]) and r["seconds"] > 0
                if solved(r) and c["expected"] in ("sat", "unsat"):
                    assert r["answer"] == c["expected"], (
                        "wrong answer",
                        rep,
                        i,
                        r["profile"],
                    )
                if r["status"] == "ok" and r["answer"] == "unknown":
                    stderr = d / (r["profile"] + ".stderr")
                    if stderr.exists() or not a.partial:
                        text = [
                            s
                            for s in stderr.read_text().splitlines()
                            if s.strip() and not s.startswith(":")
                        ]
                        assert text == ["integer branch-and-bound node limit"], (
                            rep,
                            i,
                            r["profile"],
                            text,
                        )
                records[rep, i, r["profile"]] = r
            assert len({r["answer"] for r in rr if solved(r)}) <= 1, (
                "disagreement",
                rep,
                i,
            )
            if not a.partial:
                selected = {
                    r["profile"]: r for r in rr if solved(r) and r["answer"] == "sat"
                }
                h = audit_models(d, selected, config, case)
                assert h is not None
                sat += len(selected)
                receipts.append(
                    {
                        "rep": rep,
                        "slot": i,
                        "rows_sha256": sha(source),
                        "provenance_sha256": sha(d / "provenance.json"),
                        "coverage_sha256": sha(d / "coverage.json"),
                        "model_metadata_sha256": h,
                    }
                )
    for i in range(80):
        assert (
            len(
                {
                    r["answer"]
                    for (rep, j, p), r in records.items()
                    if j == i and solved(r)
                }
            )
            <= 1
        ), ("cross-repetition disagreement", i)
    if a.partial:
        print(json.dumps({"audited_rows": len(records), "expected": 960}))
        return
    assert len(records) == 960 and len(receipts) == 240
    summaries = {}
    families = {}
    for logic in ("QF_UF", "QF_UFLIA"):
        slots = [i for i, c in enumerate(selection) if c["logic"] == logic]
        assert len(slots) == 40
        profiles = ("BASE", "OFF", "DIAG", "ON")
        summaries[logic] = {
            "cases": 40,
            "profiles": {
                p: {
                    "repetitions": [
                        describe([records[rep, i, p] for i in slots])
                        for rep in range(3)
                    ]
                }
                for p in profiles
            },
            "comparisons": {
                b + "->" + p: paired(records, slots, b, p)
                for b, p in (
                    ("BASE", "OFF"),
                    ("OFF", "DIAG"),
                    ("OFF", "ON"),
                    ("DIAG", "ON"),
                    ("BASE", "ON"),
                )
            },
        }
        groups = collections.defaultdict(list)
        for i in slots:
            groups[selection[i]["file"].split("/")[0]].append(i)
        families[logic] = {
            f: {
                "cases": len(ii),
                "profiles": {
                    p: [
                        {
                            k: v
                            for k, v in describe(
                                [records[rep, i, p] for i in ii]
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
            for f, ii in sorted(groups.items())
        }
    report = {
        "status": "qualified bounded prototype pilot",
        "candidate_source": campaign["git"],
        "native_sha256": campaign["sha256"],
        "analysis_script_sha256": sha(Path(__file__)),
        "campaign_sha256": sha(a.configs / "timing-campaign.json"),
        "native_rows": 960,
        "SAT_Dolmen_pass": sat,
        "receipts": receipts,
        "summaries": summaries,
        "families": families,
        "scope": "Selected80input30spilot; no global/default claim; delivered clauses do not establish backend retention/reuse.",
    }
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    fields = [
        "repetition",
        "slot",
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
    with (a.out / "measurements.csv").open("w") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for (rep, i, p), r in sorted(records.items()):
            w.writerow(
                {
                    **{k: r[k] for k in fields if k in r},
                    "repetition": rep,
                    "slot": i,
                    "solved": solved(r),
                    "PAR2_seconds": penalty(r, 30),
                    **{k: metric(r, k) for k in METRICS},
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
    for x in ("configs", "rows", "out"):
        p.add_argument("--" + x, type=Path, required=True)
    p.add_argument("--selection", type=Path)
    p.add_argument("--partial", action="store_true")
    a = p.parse_args()
    (gates if a.phase == "gates" else timings)(a)


if __name__ == "__main__":
    main()
