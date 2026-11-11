#!/usr/bin/env python3
"""Audit generated and mutation correctness summaries without running solvers."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path

from profiles import FUZZ_PROFILES


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = []
    identities = {}
    failures = []
    for index, profile in enumerate(FUZZ_PROFILES):
        answers = []
        model_rows = []
        selected_count = 0
        validated_count = 0
        for shard in range(4):
            directory = args.root / "fuzz" / f"slot-{4 * index + shard}"
            summary = directory / "answers/summary.csv"
            if not summary.exists():
                failures.append(f"missing {profile}/{shard}")
                continue
            batch = rows(summary)
            answers.extend(batch)
            assert len(batch) == 250, summary
            selected = {Path(r["case"]).name for r in batch if r["ours"] == "sat" and r["ours_code"] == "0"}
            model_summary = directory / "models/summary.csv"
            models = rows(model_summary) if model_summary.exists() else []
            model_rows.extend(models)
            passed = {Path(r["file"]).name for r in models if r["outcome"] == "pass" and r["validator"] == "dolmen" and r["solver_exit"] == "0" and r["dolmen_exit"] == "0"}
            selected_count += len(selected)
            validated_count += len(selected & passed)
            if selected - passed:
                failures.append(f"missing selected SAT models {profile}/{shard}: {sorted(selected - passed)}")
            for path in (summary, model_summary):
                if path.exists():
                    identities[str(path.relative_to(args.root))] = hashlib.sha256(path.read_bytes()).hexdigest()
        outcomes = dict(Counter(r["outcome"] for r in answers))
        model_outcomes = dict(Counter(r["outcome"] for r in model_rows))
        if set(outcomes) - {"PASS", "UNKNOWN_INVOLVED", "TIMEOUT"}:
            failures.append(f"blocking/unclassified outcomes {profile}: {outcomes}")
        if set(model_outcomes) - {"pass"}:
            failures.append(f"blocking/incomplete selected models {profile}: {model_outcomes}")
        if len(answers) != 1000:
            failures.append(f"{profile}: only {len(answers)} primary rows")
        data.append({"profile": profile, "inputs": len(answers), "answers": dict(Counter(r["ours"] for r in answers)),
                     "outcomes": outcomes, "selected_sat": selected_count, "selected_sat_direct_dolmen": validated_count,
                     "model_outcomes": model_outcomes})
    mutations = {}
    for policy in ("all", "fixed"):
        path = args.root / "mutations" / f"coverage-{policy}.json"
        if not path.exists():
            failures.append(f"missing mutation coverage {policy}")
        else:
            mutations[policy] = json.loads(path.read_text())
    result = {"status": "complete" if not failures else "incomplete-or-failed", "generated": data,
              "inputs": sum(r["inputs"] for r in data), "selected_sat": sum(r["selected_sat"] for r in data),
              "selected_sat_direct_dolmen": sum(r["selected_sat_direct_dolmen"] for r in data),
              "mutations": mutations, "summary_sha256": identities, "failures": failures}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "inputs": result["inputs"], "selected_sat": result["selected_sat"], "models": result["selected_sat_direct_dolmen"], "failures": failures}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
