#!/usr/bin/env python3
"""Plot audited 120s Primo repeats with archived single-run reference context.

Matplotlib is optional and should be installed in a separate plotting environment.
"""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native", "references", "summary", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.summary.read_text())
    assert report["phase"] == "final" and report["SAT_Dolmen_pass"] is not None
    native = list(csv.DictReader(args.native.open()))
    refs = list(csv.DictReader(args.references.open()))
    assert len(native) == report["native_rows"] == 8424 and len(refs) == 5616
    cases = collections.defaultdict(list)
    for row in native:
        cases[row["logic"], row["file"], row["profile"]].append(row)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update(
        {
            "svg.fonttype": "none",
            "svg.hashsalt": "primo-euf-smtcomp",
            "font.size": 10,
            "axes.spines.top": False,
            "axes.spines.right": False,
        }
    )
    colors = {
        "BASE": "#666666",
        "OFF": "#0072b2",
        "z3": "#009e73",
        "cvc5": "#e69f00",
        "yices": "#d55e00",
        "opensmt": "#9573b5",
    }
    names = {
        "BASE": "Primo baseline (3 runs)",
        "OFF": "Primo current (3 runs)",
        **{p: p + " (archived)" for p in ("z3", "cvc5", "yices", "opensmt")},
    }
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.7))
    for ax, logic in zip(axes, ("QF_UF", "QF_UFLIA")):
        all_times = []
        for profile, color in colors.items():
            times = []
            if profile in ("BASE", "OFF"):
                for (current_logic, _, current_profile), rows in cases.items():
                    if (current_logic, current_profile) != (logic, profile):
                        continue
                    assert len(rows) == 3 and {r["repetition"] for r in rows} == {
                        "0",
                        "1",
                        "2",
                    }
                    values = [
                        float(r["seconds"])
                        if r["status"] == "ok"
                        and r["exit_code"] == "0"
                        and r["answer"] in ("sat", "unsat")
                        else math.inf
                        for r in rows
                    ]
                    median = statistics.median(values)
                    if math.isfinite(median):
                        times.append(median)
            else:
                times = [
                    float(r["seconds"])
                    for r in refs
                    if (r["logic"], r["profile"]) == (logic, profile)
                    and r["status"] == "ok"
                    and r["exit_code"] == "0"
                    and r["answer"] in ("sat", "unsat")
                ]
            times.sort()
            assert all(0 < t < math.inf for t in times)
            all_times.extend(times)
            ax.plot(
                range(1, len(times) + 1),
                times,
                color=color,
                label=names[profile],
                linewidth=1.8 if profile in ("BASE", "OFF") else 1.2,
                linestyle="-" if profile == "OFF" else "--",
            )
        ax.set(
            title=logic,
            xlabel="Instances solved",
            ylabel="Runtime (s)",
            yscale="log",
            ylim=(min(all_times) * 0.8, max(130, max(all_times) * 1.05)),
        )
        ax.grid(True, which="major", alpha=0.18)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=3, frameon=False)
    fig.text(
        0.5,
        0.015,
        "Primo: median of 3 runs, unsolved = infinity (at least 2 successes). References: reused single runs censored at 120s.",
        ha="center",
        fontsize=8,
    )
    fig.tight_layout(rect=(0, 0.04, 1, 0.86))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out.with_suffix(".svg"), metadata={"Date": None})
    fig.savefig(args.out.with_suffix(".png"), dpi=180)
    plt.close(fig)
    metadata = {
        "matplotlib": matplotlib.__version__,
        "figure": str(args.out),
        "inputs_sha256": {
            str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (args.native, args.references, args.summary)
        },
        "native_semantics": "Median of three; unsolved infinity; at least two successes.",
        "reference_semantics": "Archived single runs censored at120s; context only.",
    }
    args.out.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata))


if __name__ == "__main__":
    main()
