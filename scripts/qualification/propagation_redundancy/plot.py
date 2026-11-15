#!/usr/bin/env python3
"""Plot qualified stable-case cactus curves (optional dependency: Matplotlib)."""

from __future__ import annotations

import argparse
import collections
import csv
import statistics
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--measurements", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    groups = collections.defaultdict(list)
    for row in csv.DictReader(args.measurements.open()):
        groups[row["logic"], row["mode"], row["variant"], row["slot"]].append(row)
    curves = collections.defaultdict(list)
    for (logic, mode, variant, _), rows in groups.items():
        assert len(rows) == 3 and {r["repetition"] for r in rows} == {"0", "1", "2"}
        if all(r["solved"] == "True" for r in rows):
            curves[logic, mode, variant].append(
                statistics.median(float(r["seconds"]) for r in rows)
            )
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10})
    fig, axes = plt.subplots(2, 2, figsize=(10, 7.5), constrained_layout=True)
    for ax, logic in zip(
        axes.flat, ("QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA"), strict=True
    ):
        for mode, color in [("rows", "#286b9f"), ("both", "#925a92")]:
            for variant, style in [("baseline", "-"), ("candidate", "--")]:
                times = sorted(curves[logic, mode, variant])
                ax.plot(
                    range(1, len(times) + 1),
                    times,
                    style,
                    color=color,
                    linewidth=1.5,
                    label=f"{mode}, {variant} (n={len(times)})",
                )
        ax.set_title(logic)
        ax.set_yscale("log")
        ax.set_ylim(0.001, 130)
        ax.set_xlabel("Cases solved in all three repetitions")
        ax.set_ylabel("Median wall time (seconds)")
        ax.grid(True, which="major", alpha=0.2)
        ax.legend(fontsize=7, loc="upper left", framealpha=0.9)
    fig.suptitle(
        "Propagation filters versus pre-audit baseline: 120-second limit", fontsize=13
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=170)
    plt.close(fig)


if __name__ == "__main__":
    main()
