#!/usr/bin/env python3
"""Export the qualified paired cactus figure (optional dependency: Matplotlib)."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import platform
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--measurements", type=Path, required=True)
    parser.add_argument("--comparison", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="Output filename stem")
    args = parser.parse_args()
    report = json.loads(args.comparison.read_text())
    protocol = json.loads(args.protocol.read_text())
    assert report["status"] == "qualified", "Complete model coverage is required"
    assert report["models"]["selected_sat"] == report["models"]["direct_dolmen_pass"]
    assert report["selection_sha256"] == protocol["selection_sha256"]
    rows = list(csv.DictReader(args.measurements.open()))
    assert len(rows) == protocol["native_measurements"] == 16728
    groups = collections.defaultdict(list)
    sat = 0
    for row in rows:
        successful = (
            row["status"] == "ok"
            and row["exit_code"] == "0"
            and row["answer"] in {"sat", "unsat"}
        )
        assert successful == (row["solved"] == "True")
        if successful:
            groups[row["profile"], int(row["repetition"])].append(float(row["seconds"]))
            sat += row["answer"] == "sat"
    assert sat == report["models"]["selected_sat"]
    import matplotlib

    matplotlib.use("Agg")
    from matplotlib import pyplot as plt
    from matplotlib.lines import Line2D

    matplotlib.rcParams.update(
        {"font.size": 10, "svg.hashsalt": "primo-bound-repairs-20261114"}
    )
    fig, axes = plt.subplots(2, 2, figsize=(11.5, 7), sharex=True, sharey=True)
    titles = {
        "default": "Default",
        "pre": "Preprocessing bounds",
        "rows": "Row propagation",
        "both": "Both",
    }
    styles = {"baseline": ("#42688e", "--"), "candidate": ("#147d69", "-")}
    minimum = min(min(values) for values in groups.values())
    for ax, (profile, title) in zip(axes.flat, titles.items(), strict=True):
        for variant, (color, linestyle) in styles.items():
            for repetition in range(3):
                values = sorted(groups[f"{variant}-{profile}", repetition])
                ax.plot(
                    range(1, len(values) + 1),
                    values,
                    color=color,
                    linestyle=linestyle,
                    linewidth=1,
                    alpha=0.65,
                )
        ax.set_title(title, loc="left", fontweight="bold")
        ax.set_yscale("log")
        ax.set_xlim(0, protocol["cases"])
        ax.set_ylim(minimum * 0.8, 125)
        ax.grid(True, which="major", color="#d9dfe5", linewidth=0.6)
        ax.spines[["top", "right"]].set_visible(False)
    for ax in axes[-1]:
        ax.set_xlabel("Solved instances")
    for ax in axes[:, 0]:
        ax.set_ylabel("Wall time (seconds, log scale)")
    fig.suptitle(
        "Arithmetic propagation: frozen baseline and repaired solver",
        fontsize=14,
        y=0.99,
    )
    handles = [
        Line2D([0], [0], color=color, linestyle=linestyle, label=label)
        for label, (color, linestyle) in zip(
            (
                f"Baseline {protocol['baseline_source']}",
                f"Candidate {protocol['candidate_source']}",
            ),
            styles.values(),
            strict=True,
        )
    ]
    fig.legend(
        handles=handles,
        loc="upper center",
        bbox_to_anchor=(0.5, 0.955),
        ncol=2,
        frameon=False,
    )
    fig.text(
        0.5,
        0.015,
        f"697 inputs across four arithmetic logics; 120 s limit; three lines per solver show the repetitions.\nSuccessful SAT/UNSAT only; all {sat:,} successful SAT rows independently validated with Dolmen.",
        ha="center",
        fontsize=9,
        color="#394653",
    )
    fig.tight_layout(rect=(0, 0.07, 1, 0.90))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out.with_suffix(".svg"), metadata={"Date": None})
    # Matplotlib emits trailing spaces in path attributes; line endings still
    # separate their coordinates after normalization for tracked artifacts.
    svg = args.out.with_suffix(".svg")
    svg.write_text(
        "\n".join(line.rstrip() for line in svg.read_text().splitlines()) + "\n"
    )
    fig.savefig(
        args.out.with_suffix(".pdf"), metadata={"CreationDate": None, "ModDate": None}
    )
    fig.savefig(args.out.with_suffix(".png"), dpi=180)
    plt.close(fig)
    provenance = {
        "measurements_sha256": sha(args.measurements),
        "comparison_sha256": sha(args.comparison),
        "protocol_sha256": sha(args.protocol),
        "generator_sha256": sha(Path(__file__)),
        "python": platform.python_version(),
        "matplotlib": matplotlib.__version__,
        "solved_counts": {
            f"{label}/{rep}": len(values)
            for (label, rep), values in sorted(groups.items())
        },
    }
    args.out.with_suffix(".json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    main()
