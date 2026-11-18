#!/usr/bin/env python3
"""Audit every paired timing/model row and summarize EUF milestone comparisons."""

from __future__ import annotations

import argparse
import collections
import json
import math
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.arithmetic_combinations.analyze import compare  # noqa: E402
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    outcome,
    penalty,
    sha,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402


def describe(rows):
    good = [r for r in rows if solved(r)]
    keys = sorted(
        {
            k
            for r in rows
            for k in r["stats"]
            if k.startswith((":euf.", ":sat.", ":time.", ":lia.branching."))
            and not k.endswith("policy-version")
        }
    )
    keys = [k for k in keys if not k.endswith("policy-version")]
    return {
        "observed_policy_versions": sorted(
            {
                int(v)
                for r in rows
                for k, v in r["stats"].items()
                if k.endswith("policy-version")
            }
        ),
        "solved": len(good),
        "outcomes": dict(collections.Counter(outcome(r) for r in rows)),
        "mean_PAR2_seconds": st.mean(penalty(r, 30) for r in rows),
        "median_RSS_KiB": st.median(r["peak_rss_kib"] for r in rows),
        "max_RSS_KiB": max(r["peak_rss_kib"] for r in rows),
        "successful_printed_counters": {
            k: {
                "rows": len(v),
                "value": max(v)
                if any(marker in k for marker in ("peak-", "max-", ".max"))
                else sum(v),
            }
            for k in keys
            if (v := [int(r["stats"][k]) for r in good if k in r["stats"]])
        },
        "incomplete_printed_counters": {
            k: {
                "rows": len(v),
                "value": max(v)
                if any(marker in k for marker in ("peak-", "max-", ".max"))
                else sum(v),
            }
            for k in keys
            if (
                v := [
                    int(r["stats"][k])
                    for r in rows
                    if not solved(r) and k in r["stats"]
                ]
            )
        },
        "counter_scope": "Only printed values are summarized; missing counters remain unavailable. Totals conditional on successful rows; incomplete snapshots separate. Peaks and maxima use maximum, not sum. Other per-case gauge sums are not event/work counts. Retired V1 and diagnostics-only counters must not be treated as V2 emitting zeros.",
    }


def audit(a):
    protocol = json.loads((a.configs / "protocol.json").read_text())
    records, receipts = {}, []
    total_sat = 0
    cases_seen = 0
    summary = {}
    for group in ("pilot", "controls"):
        master = json.loads((a.configs / f"{group}-campaign.json").read_text())
        cases = master["selection"]
        profiles = master["profiles"]
        expected = 80 if group == "pilot" else 737
        assert len(cases) == expected
        expected_profiles = (
            protocol["sources"]
            if group == "pilot"
            else {k: protocol["sources"][k] for k in ("BASE", "OFF")}
        )
        assert profiles == expected_profiles
        pilot = json.loads(
            (ROOT / "benchmarks/conflict-ackermann-20261117/selection.json").read_text()
        )["cases"]
        controls = json.loads(
            (ROOT / "benchmarks/bound-propagation-20261113/selection.json").read_text()
        )["cases"]
        expected_selection = (
            pilot
            if group == "pilot"
            else controls + [c for c in pilot if c["logic"] == "QF_UF"]
        )
        assert cases == expected_selection
        for rep in range(3):
            for i, case in enumerate(cases):
                entry = master["mapping"][i]
                config_path = a.configs / f"{entry['cohort']}-{rep}.json"
                config = json.loads(config_path.read_text())
                assert config["profiles"] == profiles
                assert (
                    config["timeout"] == 30
                    and config["address_space_bytes"] == 8589934592
                )
                assert config["cases"][entry["slot"]] == dict(case, repetition=rep)
                cohort = f"case-{i}" if group == "pilot" else "controls"
                d = a.rows / group / f"rep-{rep}" / cohort / f"slot-{entry['slot']}"
                if a.partial and not (d / "timing-complete").exists():
                    continue
                assert (d / "timing-complete").exists(), d
                provenance = json.loads((d / "provenance.json").read_text())
                assert provenance["config_sha256"] == sha(config_path)
                assert provenance["runner_sha256"] == sha(
                    ROOT / "scripts/qualification/lia_hybrid/run.py"
                )
                assert provenance["case"] == config["cases"][entry["slot"]]
                assert provenance["profiles"] == config["profiles"]
                rr = [
                    json.loads(line)
                    for line in (d / "rows.jsonl").read_text().splitlines()
                ]
                assert [r["profile"] for r in rr] == measurement_order(
                    config, entry["slot"]
                )
                assert len(rr) == len(profiles)
                for row in rr:
                    audit_row(row)
                    label = row["profile"]
                    assert row["args"] == profiles[label]["args"] + case.get(
                        "extra_args", []
                    )
                    assert row["logic"] == case["logic"] and row["file"] == case["file"]
                    row["seconds"] = float(row["seconds"])
                    assert math.isfinite(row["seconds"]) and row["seconds"] > 0
                    if solved(row) and case.get("expected") in ("sat", "unsat"):
                        assert row["answer"] == case["expected"], (group, rep, i, row)
                    if row["status"] == "ok" and row["answer"] == "unknown":
                        diagnostic = [
                            line
                            for line in (d / (label + ".stderr"))
                            .read_text()
                            .splitlines()
                            if line.strip() and not line.startswith(":")
                        ]
                        assert diagnostic == ["integer branch-and-bound node limit"], (
                            group,
                            rep,
                            i,
                            label,
                            diagnostic,
                        )
                    records[group, rep, i, label] = row
                assert len({r["answer"] for r in rr if solved(r)}) <= 1
                cases_seen += 1
                if not a.partial:
                    selected = {
                        r["profile"]: r
                        for r in rr
                        if solved(r) and r["answer"] == "sat"
                    }
                    models = audit_models(
                        d, selected, config, config["cases"][entry["slot"]]
                    )
                    assert models is not None, d
                    total_sat += len(selected)
                    receipts.append(
                        {
                            "group": group,
                            "rep": rep,
                            "slot": i,
                            "rows_sha256": sha(d / "rows.jsonl"),
                            "provenance_sha256": sha(d / "provenance.json"),
                            "coverage_sha256": sha(d / "coverage.json"),
                            "model_metadata_sha256": models,
                        }
                    )
        if a.partial:
            continue
        local = {
            (rep, i, p): records[group, rep, i, p]
            for rep in range(3)
            for i in range(expected)
            for p in profiles
        }
        for i in range(expected):
            assert (
                len(
                    {
                        local[r, i, p]["answer"]
                        for r in range(3)
                        for p in profiles
                        if solved(local[r, i, p])
                    }
                )
                <= 1
            )
        by_logic = collections.defaultdict(list)
        for i, case in enumerate(cases):
            by_logic[case["logic"]].append(i)
        comparisons = [
            ("BASE", "MECH"),
            ("MECH", "OFF"),
            ("BASE", "OFF"),
            ("OLDON", "ON"),
            ("OFF", "ON"),
            ("ON", "DEMAND"),
            ("OFF", "DEMAND"),
        ]
        summary[group] = {}
        for logic, indices in by_logic.items():
            report = {
                "cases": len(indices),
                "profiles": {
                    p: {
                        "repetitions": [
                            describe([local[r, i, p] for i in indices])
                            for r in range(3)
                        ]
                    }
                    for p in profiles
                },
                "comparisons": {},
            }
            for before, after in comparisons:
                if before not in profiles or after not in profiles:
                    continue
                comparison = compare(local, indices, before, after, 30)
                comparison.pop("matched_complete_counter_totals")
                comparison["case_identity"] = {str(i): cases[i] for i in indices}
                report["comparisons"][before + "->" + after] = comparison
            summary[group][logic] = report
    # Reject disagreements across repetitions/cohorts even in an early audit.
    # A declared-unknown input must not hide opposite answers in different runs.
    answers = collections.defaultdict(set)
    for row in records.values():
        if solved(row):
            answers[row["logic"], row["file"]].add(row["answer"])
    assert all(len(values) <= 1 for values in answers.values()), {
        key: sorted(values) for key, values in answers.items() if len(values) > 1
    }
    if a.partial:
        print(
            json.dumps(
                {
                    "audited_rows": len(records),
                    "expected_rows": 5862,
                    "complete_case_repetitions": cases_seen,
                }
            )
        )
        return
    assert len(records) == protocol["pilot_rows"] + protocol["control_rows"] == 5862
    assert len(receipts) == 3 * (80 + 737)
    a.out.mkdir(parents=True, exist_ok=False)
    (a.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (a.out / "receipts.json").write_text(
        json.dumps(
            {
                "native_rows": len(records),
                "SAT_Dolmen_pass": total_sat,
                "receipts": receipts,
            },
            indent=2,
        )
        + "\n"
    )
    print(json.dumps({"rows": len(records), "SAT_Dolmen_pass": total_sat}))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("configs", "rows", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--partial", action="store_true")
    audit(p.parse_args())


if __name__ == "__main__":
    main()
