#!/usr/bin/env python3
"""Audit retained correctness summaries; never launch solvers or reuse timing rows."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def counts(rows: list[dict[str, str]], key: str) -> dict[str, int]:
    return dict(sorted(Counter(row.get(key, "") for row in rows).items()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Collected results/model-repair")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    series = []
    failures = []
    identities = {}
    for logic in ("uf", "uflia", "uflra", "lra", "lia"):
        for mode in ("strong", "auto"):
            root = args.root / "fuzz" / f"{logic}-{mode}"
            primary = []
            models = []
            selected = set()
            passed = set()
            for shard in range(4):
                directory = root / f"shard-{shard}"
                summaries = sorted(directory.rglob("summary.csv"))
                answer_files = [p for p in summaries if "model-validation" not in p.parts and "models" not in p.parts]
                model_files = [p for p in summaries if p not in answer_files]
                if len(answer_files) != 1:
                    failures.append(f"{logic}/{mode}/{shard}: expected one primary summary, found {len(answer_files)}")
                answers = [r for p in answer_files for r in load_csv(p)]
                primary.extend(answers)
                for row in answers:
                    if row.get("ours", row.get("answer")) == "sat":
                        selected.add((shard, Path(row.get("case", row.get("file", ""))).name))
                if logic == "lra":
                    for row in answers:
                        if row.get("answer") == "sat" and row.get("dolmen") == "PASS":
                            passed.add((shard, Path(row["case"]).name))
                for path in model_files:
                    rows = load_csv(path)
                    models.extend(rows)
                    for row in rows:
                        if row["outcome"] == "pass" and row["validator"] == "dolmen" and row["solver_exit"] == "0" and row["dolmen_exit"] == "0":
                            passed.add((shard, Path(row["file"]).name))
                for path in summaries:
                    identities[str(path.relative_to(args.root))] = hashlib.sha256(path.read_bytes()).hexdigest()
            outcomes = counts(primary, "outcome")
            # These resource outcomes stay separate from passing comparisons.
            allowed = {"PASS", "UNKNOWN_INVOLVED", "TIMEOUT", "UNKNOWN"}
            unexpected = set(outcomes) - allowed
            if unexpected:
                failures.append(f"{logic}/{mode}: blocking or unclassified outcomes {sorted(unexpected)}")
            if len(primary) != 1000:
                failures.append(f"{logic}/{mode}: {len(primary)} primary rows; expected1000")
            missing = sorted(selected - passed)
            if missing:
                failures.append(f"{logic}/{mode}: {len(missing)} selected SAT rows lack direct Dolmen acceptance")
            model_outcomes = counts(models, "outcome")
            unexpected_models = set(model_outcomes) - {"pass", "not-sat", "solver-timeout", "solver-unknown"}
            if unexpected_models:
                failures.append(f"{logic}/{mode}: blocking model outcomes {sorted(unexpected_models)}")
            series.append({"logic": "QF_" + logic.upper(), "profile": mode, "primary_rows": len(primary),
                           "answers": counts(primary, "ours" if logic != "lra" else "answer"),
                           "comparison_outcomes": outcomes, "model_replay_outcomes": model_outcomes,
                           "selected_sat_rows": len(selected), "selected_sat_direct_dolmen_pass": len(selected & passed),
                           "all_direct_dolmen_pass": len(passed), "missing_selected_sat": missing})
    data = {"schema_version": 1, "scope": "Fresh generated answer/model correctness checks; not corpus timing validation",
            "series": series, "primary_rows": sum(s["primary_rows"] for s in series),
            "selected_sat_rows": sum(s["selected_sat_rows"] for s in series),
            "selected_sat_direct_dolmen_pass": sum(s["selected_sat_direct_dolmen_pass"] for s in series),
            "summary_sha256": identities, "failures": failures, "status": "complete" if not failures else "incomplete-or-failed"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=2) + "\n")
    for series_row in series:
        print(series_row["logic"], series_row["profile"], series_row["primary_rows"], series_row["comparison_outcomes"],
              "SAT models", series_row["selected_sat_direct_dolmen_pass"], "/", series_row["selected_sat_rows"])
    for failure in failures:
        print(failure)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
