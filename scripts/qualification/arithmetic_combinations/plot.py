#!/usr/bin/env python3
"""Render qualified arithmetic combination curves (optional: Matplotlib)."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import statistics
from pathlib import Path

COLORS = {"B": "#526271", "P": "#3c7eab", "R": "#b37436", "PR": "#487e68"}
LOGICS = ("QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA")
LABELS = ("B", "P", "R", "PR", "H", "PH", "RH", "PRH")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import ticker

    report = json.loads((args.evidence / "analysis.json").read_text())
    assert report["status"] == "qualified", "Only plot fully validated timing evidence"
    groups = collections.defaultdict(list)
    for row in csv.DictReader((args.evidence / "measurements.csv").open()):
        groups[row["logic"], row["profile"], row["slot"]].append(row)
    curves = collections.defaultdict(list)
    for (logic, profile, _), rows in groups.items():
        assert len(rows) == 3 and {r["repetition"] for r in rows} == {"0", "1", "2"}
        if all(r["solved"] == "True" for r in rows):
            curves[logic, profile].append(
                statistics.median(float(r["seconds"]) for r in rows)
            )
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 10,
            "svg.hashsalt": "primo-arithmetic-combinations",
        }
    )
    fig, axes = plt.subplots(2, 2, figsize=(11, 8), constrained_layout=True)
    for ax, logic in zip(axes.flat, LOGICS, strict=True):
        for profile in LABELS:
            if logic in LOGICS[2:] and "H" in profile:
                continue
            times = sorted(curves[logic, profile])
            base = profile.replace("H", "") or "B"
            ax.plot(
                range(1, len(times) + 1),
                times,
                "--" if "H" in profile else "-",
                color=COLORS[base],
                linewidth=1.6,
                label=f"{profile} (n={len(times)})",
            )
        ax.set_title(logic)
        ax.set_yscale("log")
        ax.set_ylim(0.001, 130)
        ax.set_xlabel("Cases solved in all three repetitions")
        ax.set_ylabel("Median wall time (seconds)")
        ax.grid(True, which="major", alpha=0.2)
        ax.legend(fontsize=7, loc="upper left", ncol=2)
    fig.suptitle(
        "Arithmetic combinations · 120-second limit · separately validated SAT models",
        fontsize=12,
    )
    for suffix in ("png", "svg"):
        fig.savefig(args.evidence / ("cactus." + suffix), dpi=170)
    plt.close(fig)

    fig, axes = plt.subplots(2, 2, figsize=(11, 7), constrained_layout=True)
    ratios = [
        item["geomean_case_median_ratio"]
        for logic in LOGICS
        for item in report["summaries"][logic]["interactions"].values()
        if item["geomean_case_median_ratio"] is not None
    ]
    limits = (
        min(0.75, min(ratios, default=1) * 0.9),
        max(1.5, max(ratios, default=1) * 1.1),
    )
    for ax, logic in zip(axes.flat, LOGICS, strict=True):
        items = report["summaries"][logic]["interactions"]
        for y, (name, value) in enumerate(items.items()):
            ratio = value["geomean_case_median_ratio"]
            if ratio is not None:
                ax.plot(ratio, y, "o", color="#3c7eab", markersize=7)
                ax.annotate(
                    f"{ratio:.3f} · n={value['eligible_cases']}",
                    (ratio, y),
                    xytext=(-7 if ratio >= 1 else 7, 7),
                    ha="right" if ratio >= 1 else "left",
                    textcoords="offset points",
                    fontsize=8,
                )
        ax.set_yticks(range(len(items)), [name.replace("_", " | ") for name in items])
        ax.set_title(logic)
        ax.axvline(1, color="#7e858b", linewidth=1)
        ax.set_xscale("log")
        ax.set_xlim(*limits)
        ax.xaxis.set_major_locator(ticker.MaxNLocator(nbins=4))
        ax.xaxis.set_major_formatter(ticker.StrMethodFormatter("{x:.2f}"))
        ax.xaxis.set_minor_locator(ticker.NullLocator())
        ax.set_ylim(-0.7, len(items) - 0.2)
        ax.set_xlabel("Conditional interaction ratio (1 = no relative interaction)")
        ax.grid(True, axis="x", which="major", alpha=0.2)
    fig.suptitle(
        "Median paired ratios per case, then geometric mean · descriptive, no significance claim",
        fontsize=11,
    )
    for suffix in ("png", "svg"):
        fig.savefig(args.evidence / ("interactions." + suffix), dpi=170)
    plt.close(fig)


if __name__ == "__main__":
    main()
