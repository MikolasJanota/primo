#!/usr/bin/env python3
"""Draw a cactus plot from the benchmark-comparison CSVs.

A cactus plot answers "how many instances can each solver finish within a
per-instance time budget": for each solver, sort its solved instances by
runtime and plot the running count against that runtime. Further right and
lower is better -- a solver whose curve stays flat and then rises steeply is
solving most instances quickly.

Two things this script insists on, because a cactus plot is easy to draw
dishonestly:

* **Only rows that actually answered `sat` or `unsat` count as solved.** A
  timeout is not a data point on the curve; neither is a crash or an
  unsupported input -- and neither is a clean `unknown`. That last one is not
  hypothetical: primo exits with status `ok` and answer `unknown` when QF_LIA
  branch-and-bound hits its node limit, 750 times per arm on the 13,306-file
  division, while cvc5 and z3 never do. Scoring `status == ok` would have
  drawn primo's curve 750 instances above the truth and inverted a standing it
  is actually behind on (2026-08-26).
* **Non-standard failures are reported separately and loudly.** A benchmark we
  could not parse is a frontend gap, not a performance result, and it must not
  disappear into the gap between a curve and the instance count. See
  docs/experiments.md.

Solvers may come from different result directories -- a reference solver that
has not changed should not be re-run (see docs/experiments.md) -- so the
provenance of each series is recorded in the caption rather than implied.

``--drop-solved-by-all-under S`` removes the instances every series solved in
under S seconds. This is a presentation change, not a scoring one: an instance
every solver finishes quickly contributes exactly one point to every curve, so
dropping it subtracts the same constant from all of them. Every gap between
curves survives untouched while the vertical range stops being dominated by
the shared easy majority. What it does change is PAR2 and the solved counts,
which are then computed over the retained instances only -- so the printed
table and the legend are counts *of the hard subset*, and the caption says so.
Numbers from a filtered plot are not comparable with numbers from a full one.

Example:
    python3 scripts/plot_cactus.py \\
        --series primo=results/div-118904 --series yices=results/div-118904 \\
        --series z3=results/div-118382 --series cvc5=results/div-118382 \\
        --series opensmt=results/div-119045 \\
        --timeout 300 --out docs/_static/qf-lra-cactus.svg
"""

from __future__ import annotations

import argparse
import collections
import csv
import html
import pathlib


def load_series(directory: pathlib.Path, solver: str) -> list[dict[str, str]]:
    """Merge a solver's named or directory-scoped shard CSVs."""
    paths = []
    for path in sorted(directory.glob(f"{solver}*.csv")):
        if path.stem.endswith("-summary"):
            continue
        # `primo.shard-3.csv` and `primo.csv` both belong to `primo`, but
        # `primo-foo.csv` does not.
        if path.stem.split(".shard-")[0] == solver:
            paths.append(path)

    # Newer benchmark campaigns put one solver in each directory, so the
    # directory owns generic `shard-N.csv` files. Prefer named files when both
    # layouts are present; mixing layouts could silently count a row twice.
    if not paths:
        paths = [
            path
            for path in sorted(directory.glob("shard-*.csv"))
            if not path.stem.endswith("-summary")
        ]
    # Model-replay campaigns retain each shard's answers beside its provenance.
    if not paths:
        paths = sorted(directory.glob("shard-*/answers.csv"))

    rows: dict[str, dict[str, str]] = {}
    for path in paths:
        with path.open() as handle:
            for row in csv.DictReader(handle):
                rows[row["file"]] = row
    return list(rows.values())


def answered(row: dict[str, str]) -> bool:
    """Whether this row is a decision, not merely a clean exit.

    A solver that stops early on its own budget -- primo's QF_LIA node limit
    is the case that prompted this -- exits with status `ok` and answer
    `unknown`. That is not a solve, and it must not be plotted as one.
    """
    return row["status"] == "ok" and row.get("answer") in {"sat", "unsat"}


def summarize(rows: list[dict[str, str]], timeout: float) -> dict[str, object]:
    # `status == ok` means the process exited cleanly, NOT that it decided the
    # instance -- see the module docstring. Answering is the requirement.
    solved = sorted(
        float(r["seconds"]) for r in rows if answered(r)
    )
    # Clean non-answers, counted and shown separately: they are neither solves
    # nor failures, and silently folding them into either is how a cactus plot
    # lies.
    clean_unknown = sum(1 for r in rows if r["status"] == "ok" and not answered(r))
    # Kept alongside the total because the two halves move independently: a
    # change that only helps sat instances (patching, in QF_LIA) is invisible
    # in a solved count that pools them.
    sat = sum(1 for r in rows if answered(r) and r["answer"] == "sat")
    unsat = sum(1 for r in rows if answered(r) and r["answer"] == "unsat")
    wrong = sum(1 for r in rows if r.get("wrong") == "true")
    nonstandard: collections.Counter[str] = collections.Counter()
    for r in rows:
        if r["status"] in {"ok", "timeout"} or r.get("answer") == "unknown":
            continue
        head = (r.get("stderr_head") or "").strip()
        if ".smt2: " in head:
            head = head.split(".smt2: ", 1)[1]
        nonstandard[head[:70] or f"status={r['status']}"] += 1
    par2 = sum(solved) + 2 * timeout * (len(rows) - len(solved))
    return {
        "total": len(rows),
        "solved": solved,
        "clean_unknown": clean_unknown,
        "sat": sat,
        "unsat": unsat,
        "wrong": wrong,
        "nonstandard": nonstandard,
        "par2": par2,
    }


def order_series_by_solved(
    series: list[tuple[str, list[float]]],
) -> list[tuple[str, list[float]]]:
    """Order legend and curves by descending solved count, then name."""
    return sorted(series, key=lambda item: (-len(item[1]), item[0].casefold()))


def svg(
    series: list[tuple[str, list[float]]],
    timeout: float,
    total: int,
    title: str,
    caption: list[str],
    highlighted: str | None = None,
    breakdown: dict[str, tuple[int, int]] | None = None,
) -> str:
    # The caption grows with the number of provenance notes, so the canvas and
    # the bottom margin grow with it rather than the plot area shrinking --
    # otherwise a fourth line simply falls off the bottom edge, which is how
    # the --drop-solved-by-all-under note first went missing.
    caption_leading = 14
    extra = caption_leading * max(0, len(caption) - 3)
    width, height = 900, 560 + extra
    left, right, top, bottom = 78, 250, 56, 96 + extra
    plot_w, plot_h = width - left - right, height - top - bottom
    max_solved = max((len(s) for _, s in series), default=1) or 1
    # Log time axis: solved counts bunch up at the low end otherwise, and the
    # interesting separation between solvers is in the last decade.
    import math
    lo, hi = 0.01, timeout
    def x_of(t: float) -> float:
        t = min(max(t, lo), hi)
        return left + plot_w * (math.log10(t) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))
    def y_of(n: int) -> float:
        return top + plot_h * (1 - n / max_solved)

    # Deliberately no red: series are solvers, not severities, and a red curve
    # reads as a verdict on whichever solver happens to be second. When one
    # series is highlighted, reserve the blue for it. The six hues, in this
    # fixed order, pass a lightness-band, chroma, colour-vision-deficiency and
    # normal-vision separation check on white; three are below 3:1 contrast, so
    # every curve is also named in the legend with its solved count. The
    # previous palette failed that check (its gray had almost no chroma).
    palette = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300"]
    comparison_palette = palette[1:]
    out = [(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" font-family="system-ui,-apple-system,sans-serif">'),
           '<rect width="100%" height="100%" fill="#ffffff"/>',
           (f'<text x="{left}" y="30" font-size="17" font-weight="600">'
            f'{html.escape(title)}</text>')]

    for decade in range(-2, 4):
        t = 10.0 ** decade
        if not lo <= t <= hi:
            continue
        x = x_of(t)
        out.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top+plot_h}" '
                   f'stroke="#e6e6e6" stroke-width="1"/>')
        label = f"{t:g}s"
        out.append(f'<text x="{x:.1f}" y="{top+plot_h+20}" font-size="12" '
                   f'text-anchor="middle" fill="#444">{label}</text>')
    for frac in (0, 0.25, 0.5, 0.75, 1.0):
        n = int(max_solved * frac)
        y = y_of(n)
        out.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left+plot_w}" y2="{y:.1f}" '
                   f'stroke="#e6e6e6" stroke-width="1"/>')
        out.append(f'<text x="{left-10}" y="{y+4:.1f}" font-size="12" '
                   f'text-anchor="end" fill="#444">{n}</text>')

    # Anchored to the plot area, not the canvas: with a caption-driven height
    # a canvas-relative offset walks down into the caption block.
    out.append(f'<text x="{left+plot_w/2:.0f}" y="{top+plot_h+38}" font-size="13" '
               f'text-anchor="middle" fill="#222">per-instance time budget (log scale)</text>')
    out.append(f'<text x="18" y="{top+plot_h/2:.0f}" font-size="13" fill="#222" '
               f'transform="rotate(-90 18 {top+plot_h/2:.0f})" text-anchor="middle">'
               f'instances solved (of {total})</text>')

    # Two-line legend entries need the extra room; a single-line legend keeps
    # the tighter spacing it always had.
    legend_step = 30 if breakdown else 22
    comparison_index = 0
    for index, (name, solved) in enumerate(series):
        is_highlighted = name == highlighted
        if highlighted is None:
            colour = palette[index % len(palette)]
        elif is_highlighted:
            colour = "#0057b8"
        else:
            colour = comparison_palette[comparison_index % len(comparison_palette)]
            comparison_index += 1
        curve_width = 3.4 if is_highlighted else 2.2
        legend_width = 4.0 if is_highlighted else 2.6
        label_weight = ' font-weight="600"' if is_highlighted else ""
        points = " ".join(f"{x_of(t):.1f},{y_of(i+1):.1f}" for i, t in enumerate(solved))
        if points:
            out.append(f'<polyline points="{points}" fill="none" stroke="{colour}" '
                       f'stroke-width="{curve_width}" stroke-linejoin="round">'
                       f'<title>{html.escape(name)}: {len(solved)} solved</title></polyline>')
        ly = top + 8 + index * legend_step
        out.append(f'<line x1="{left+plot_w+18}" y1="{ly}" x2="{left+plot_w+44}" y2="{ly}" '
                   f'stroke="{colour}" stroke-width="{legend_width}"/>')
        out.append(f'<text x="{left+plot_w+52}" y="{ly+4}" font-size="13" fill="#222"'
                   f'{label_weight}>'
                   f'{html.escape(name)} &#8212; {len(solved)}</text>')
        split = (breakdown or {}).get(name)
        if split is not None:
            sat, unsat = split
            out.append(f'<text x="{left+plot_w+52}" y="{ly+19}" font-size="11" '
                       f'fill="#666">{sat} sat &#183; {unsat} unsat</text>')

    caption_top = height - 12 - caption_leading * (len(caption) - 1)
    for i, line in enumerate(caption):
        out.append(f'<text x="{left}" y="{caption_top+i*caption_leading}" '
                   f'font-size="11" fill="#666">{html.escape(line)}</text>')
    out.append("</svg>")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--series", action="append", required=True,
                        metavar="NAME=DIR", help="solver name and its result directory")
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--title", default="QF_LRA")
    parser.add_argument("--note", action="append", default=[],
                        help="extra caption line, e.g. run provenance")
    parser.add_argument("--drop-solved-by-all-under", type=float, metavar="SECONDS",
                        help="omit instances every series solved faster than this; "
                             "counts and PAR2 are then over the retained instances")
    parser.add_argument("--sort-by-solved", action="store_true",
                        help="order curves and legend by descending solved count")
    parser.add_argument("--highlight-series", metavar="NAME",
                        help="draw one named series in a prominent blue with a heavier line")
    parser.add_argument("--public", action="store_true",
                        help="omit every --note line (job IDs and other run "
                             "provenance) for external/public presentation, while "
                             "keeping the instance/timeout summary and the "
                             "drop-cutoff explanation, since dropping instances "
                             "silently would misrepresent the plot")
    args = parser.parse_args()

    loaded: list[tuple[str, list[dict[str, str]]]] = []
    for spec in args.series:
        if "=" not in spec:
            raise SystemExit(f"--series expects NAME=DIR, got {spec!r}")
        name, _, directory = spec.partition("=")
        rows = load_series(pathlib.Path(directory), name)
        if not rows:
            raise SystemExit(f"no rows for {name} under {directory}")
        loaded.append((name, rows))

    dropped = 0
    if args.drop_solved_by_all_under is not None:
        cutoff = args.drop_solved_by_all_under
        # A file missing from some series cannot have been solved by all of
        # them, so it is kept -- `all()` over the series it does appear in
        # would silently drop it on the strength of a partial set.
        seen: collections.Counter[str] = collections.Counter()
        fast: collections.Counter[str] = collections.Counter()
        for _, rows in loaded:
            for row in rows:
                seen[row["file"]] += 1
                if answered(row) and float(row["seconds"]) < cutoff:
                    fast[row["file"]] += 1
        easy = {f for f, count in fast.items() if count == len(loaded) == seen[f]}
        dropped = len(easy)
        loaded = [(name, [r for r in rows if r["file"] not in easy])
                  for name, rows in loaded]
        print(f"dropping {dropped} instances every series solved in under "
              f"{cutoff:g}s; counts and PAR2 below are over what remains")

    plotted: list[tuple[str, list[float]]] = []
    breakdown: dict[str, tuple[int, int]] = {}
    totals: set[int] = set()
    problems = 0
    print(f"{'solver':16s} {'files':>6} {'solved':>7} {'sat':>6} {'unsat':>6} "
          f"{'wrong':>6} {'PAR2':>10}")
    for name, rows in loaded:
        stats = summarize(rows, args.timeout)
        totals.add(int(stats["total"]))
        breakdown[name] = (int(stats["sat"]), int(stats["unsat"]))
        print(f"{name:16s} {stats['total']:6d} {len(stats['solved']):7d} "
              f"{stats['sat']:6d} {stats['unsat']:6d} "
              f"{stats['wrong']:6d} {stats['par2']:10.0f}")
        if stats["wrong"]:
            print(f"  WRONG ANSWERS from {name}: {stats['wrong']} -- do not publish this plot")
            problems += 1
        if stats["clean_unknown"]:
            print(f"  clean `unknown` exits ({name}): {stats['clean_unknown']:5d}  "
                  f"(exited 0 without deciding; not plotted as solved)")
        for cause, count in stats["nonstandard"].most_common():
            print(f"  non-standard unsolved ({name}): {count:5d}  {cause}")
            problems += 1
        plotted.append((name, list(stats["solved"])))

    if args.highlight_series is not None and all(
        name != args.highlight_series for name, _ in plotted
    ):
        raise SystemExit(f"no plotted series named {args.highlight_series!r}")
    if args.sort_by_solved:
        plotted = order_series_by_solved(plotted)

    if len(totals) > 1:
        print(f"  NOTE: solvers cover different file counts {sorted(totals)} -- "
              "the curves are not directly comparable")
        problems += 1

    total = max(totals)
    caption = [(f"{total} instances, {args.timeout:g}s per instance. "
               "Only solved instances contribute a point; timeouts do not.")]
    if dropped:
        caption.append(
            f"Excludes {dropped} further instances every solver finished in under "
            f"{args.drop_solved_by_all_under:g}s, so every count here is of the "
            f"remaining {total}.")
    if not args.public:
        caption += args.note
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        svg(plotted, args.timeout, total, args.title, caption, args.highlight_series,
            breakdown)
    )
    print(f"\nwrote {args.out}")
    if problems:
        print("Review the flagged items above before citing this plot.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
