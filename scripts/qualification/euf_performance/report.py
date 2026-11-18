#!/usr/bin/env python3
"""Export audited native measurements, tables and optional median cactus plots.

The CSV/table path uses the standard library. --figures additionally requires
Matplotlib (install it in a separate plotting virtual environment). Run only
after audit.py has accepted all timing rows and their separate Dolmen replays.
"""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
import statistics as st
from pathlib import Path

LABELS = ("BASE", "MECH", "OFF", "OLDON", "ON", "DEMAND")
COUNTERS = (
    ":euf.conflict-ack.v2.candidate-visits",
    ":euf.conflict-ack.v2.admitted-pairs",
    ":euf.conflict-ack.v2.negative-evictions",
    ":euf.conflict-ack.demand.requests",
    ":euf.conflict-ack.demand.installed-instances",
    ":euf.conflict-ack.demand.fresh-atoms",
    ":euf.conflict-ack.demand.peak-fresh-atoms",
    ":euf.conflict-ack.demand.sat-resumes",
    ":euf.conflict-ack.demand.budget-skips",
)


def solved(row):
    return row["status"] == "ok" and row["answer"] in ("sat", "unsat")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_rows(configs, directory, audited):
    records = []
    receipts = {(r["group"], r["rep"], r["slot"]): r for r in audited}
    assert len(receipts) == 2451
    for group in ("pilot", "controls"):
        master = json.loads((configs / f"{group}-campaign.json").read_text())
        for rep in range(3):
            for i, case in enumerate(master["selection"]):
                cohort = f"case-{i}" if group == "pilot" else "controls"
                slot = master["mapping"][i]["slot"]
                path = directory / group / f"rep-{rep}" / cohort / f"slot-{slot}"
                assert (path / "timing-complete").exists(), path
                receipt = receipts[group, rep, i]
                assert digest(path / "rows.jsonl") == receipt["rows_sha256"]
                assert digest(path / "provenance.json") == receipt["provenance_sha256"]
                assert digest(path / "coverage.json") == receipt["coverage_sha256"]
                for label, expected in receipt["model_metadata_sha256"].items():
                    assert digest(path / "models" / label / "metadata.json") == expected
                provenance = json.loads((path / "provenance.json").read_text())
                assert provenance["case"] == dict(case, repetition=rep)
                assert provenance["profiles"] == master["profiles"]
                for line in (path / "rows.jsonl").read_text().splitlines():
                    row = json.loads(line)
                    row["seconds"] = float(row["seconds"])
                    source = master["profiles"][row["profile"]]
                    assert row["file"] == case["file"]
                    records.append(
                        dict(
                            row,
                            group=group,
                            case=i,
                            repetition=rep,
                            input_sha256=case["sha256"],
                            source=source["git"],
                            binary_sha256=source["sha256"],
                        )
                    )
    assert len(records) == 5862
    return records


def rollup(records):
    grouped = collections.defaultdict(list)
    for row in records:
        grouped[row["group"], row["logic"], row["profile"]].append(row)
    result = []

    def order(item):
        return item[0][0] != "pilot", item[0][1], LABELS.index(item[0][2])

    for (group, logic, profile), rows in sorted(grouped.items(), key=order):
        good = [r for r in rows if solved(r)]
        counts = [
            sum(solved(r) for r in rows if r["repetition"] == rep) for rep in range(3)
        ]
        counters = {}
        for key in COUNTERS:
            values = [int(r["stats"][key]) for r in good if key in r["stats"]]
            if values:
                counters[key] = {
                    "reporting_successful_rows": len(values),
                    "value": max(values) if ".peak-" in key else sum(values),
                }
        result.append(
            {
                "group": group,
                "logic": logic,
                "profile": profile,
                "cases": len(rows) // 3,
                "solved_by_repetition": counts,
                "mean_PAR2_seconds": st.mean(
                    r["seconds"] if solved(r) else 60 for r in rows
                ),
                "median_RSS_KiB": st.median(r["peak_rss_kib"] for r in rows),
                "max_RSS_KiB": max(r["peak_rss_kib"] for r in rows),
                "successful_printed_mechanisms": counters,
            }
        )
    return result


def plot(records, destination):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update(
        {
            "svg.fonttype": "none",
            "svg.hashsalt": "primo-euf-20261118",
            "font.size": 10,
            "axes.spines.top": False,
            "axes.spines.right": False,
        }
    )
    colors = dict(
        zip(LABELS, ("#666666", "#0072b2", "#009e73", "#9573b5", "#d55e00", "#b4426f"))
    )
    destination.mkdir(parents=True, exist_ok=True)
    for group, logics in (
        ("pilot", ("QF_UF", "QF_UFLIA")),
        ("controls", ("QF_UF", "QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA")),
    ):
        columns = 2 if group == "pilot" else 3
        fig, axes = plt.subplots(
            math.ceil(len(logics) / columns),
            columns,
            figsize=(12, 4.1 if group == "pilot" else 7.5),
            squeeze=False,
        )
        for ax, logic in zip(axes.flat, logics):
            profiles = LABELS if group == "pilot" else ("BASE", "OFF")
            for label in profiles:
                cases = collections.defaultdict(list)
                for r in records:
                    if (r["group"], r["logic"], r["profile"]) == (group, logic, label):
                        cases[r["case"]].append(r)
                times = []
                for rows in cases.values():
                    assert len(rows) == 3
                    if sum(map(solved, rows)) >= 2:
                        times.append(
                            st.median(
                                r["seconds"] if solved(r) else math.inf for r in rows
                            )
                        )
                times.sort()
                ax.plot(
                    range(1, len(times) + 1),
                    times,
                    label=label,
                    color=colors[label],
                    linestyle="--" if label in ("BASE", "MECH", "OLDON") else "-",
                    linewidth=1.6,
                )
            ax.set(
                title=logic,
                xlabel="Cases solved in at least 2/3 repetitions",
                ylabel="Median native runtime (s)",
                yscale="log",
            )
            ax.set_ylim(top=32)
            ax.grid(True, which="major", alpha=0.18)
        for ax in list(axes.flat)[len(logics) :]:
            ax.set_visible(False)
        handles, names = axes.flat[0].get_legend_handles_labels()
        fig.legend(handles, names, loc="upper center", ncol=len(names), frameon=False)
        fig.tight_layout(rect=(0, 0, 1, 0.92))
        fig.savefig(destination / f"{group}-cactus.svg", metadata={"Date": None})
        plt.close(fig)
    return matplotlib.__version__


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("configs", "rows", "analysis", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--figures", type=Path)
    a = p.parse_args()
    receipt_path = a.analysis / "receipts.json"
    receipts = json.loads(receipt_path.read_text())
    assert receipts["native_rows"] == 5862 and len(receipts["receipts"]) == 2451
    records = read_rows(a.configs, a.rows, receipts["receipts"])
    assert receipts["SAT_Dolmen_pass"] == sum(
        solved(r) and r["answer"] == "sat" for r in records
    )
    a.out.mkdir(parents=True, exist_ok=False)
    fields = (
        "group",
        "logic",
        "case",
        "repetition",
        "profile",
        "file",
        "input_sha256",
        "source",
        "binary_sha256",
        "seconds",
        "status",
        "answer",
        "exit_code",
        "peak_rss_kib",
    )
    with (a.out / "native-rows.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[*fields, "args_json", *COUNTERS])
        writer.writeheader()
        for r in records:
            writer.writerow(
                {
                    **{k: r[k] for k in fields},
                    "args_json": json.dumps(r["args"]),
                    **{k: r["stats"].get(k, "") for k in COUNTERS},
                }
            )
    summary = rollup(records)
    (a.out / "rollup.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [
        "| Cohort | Logic | Profile | Solved (three repetitions) | Mean PAR2 (s) | Median / max RSS (MiB) |",
        "| --- | --- | --- | --- | ---: | ---: |",
    ]
    for r in summary:
        counts = "/".join(map(str, r["solved_by_repetition"]))
        lines.append(
            f"| {r['group']} | {r['logic']} | {r['profile']} | {counts} / {r['cases']} | "
            f"{r['mean_PAR2_seconds']:.3f} | {r['median_RSS_KiB'] / 1024:.1f} / {r['max_RSS_KiB'] / 1024:.1f} |"
        )
    (a.out / "tables.md").write_text("\n".join(lines) + "\n")
    metadata = {
        "native_rows": 5862,
        "SAT_Dolmen_pass": receipts["SAT_Dolmen_pass"],
        "audit_receipts_sha256": digest(receipt_path),
        "analysis_sha256": digest(a.analysis / "summary.json"),
        "csv_sha256": digest(a.out / "native-rows.csv"),
        "script_sha256": digest(Path(__file__)),
        "cactus": "Median of three runtimes with unsolved repetitions censored at infinity; requires at least two successful repetitions. Every successful SAT row separately validated.",
        "counter_scope": "Only explicitly printed successful-row values; missing is unavailable. Peaks use maximum. Failed/interrupted snapshots remain in the full audited analysis.",
    }
    if a.figures:
        metadata["matplotlib_version"] = plot(records, a.figures)
    (a.out / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
