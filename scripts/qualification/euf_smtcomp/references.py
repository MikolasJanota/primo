#!/usr/bin/env python3
"""Select hash-matched archived reference rows without executing any solver."""

from __future__ import annotations

import argparse
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def export(args):
    selection = json.loads(args.selection.read_text())
    selected = {(c["logic"], c["file"]): c for c in selection["cases"]}
    experiment = json.loads(args.experiment.read_text())
    assert experiment["config"]["timeout_seconds"] == 120
    assert experiment["config"]["memory_mib"] == 8192
    provenance = json.loads(args.provenance.read_text())
    names = {"z3", "cvc5", "yices", "opensmt"}
    rows, seen = [], set()
    answers = collections.defaultdict(set)
    with gzip.open(args.archive, "rt", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames
        for row in reader:
            key = row["logic"], row["file"]
            if key not in selected or row["profile"] not in names:
                continue
            identity = (*key, row["profile"])
            assert identity not in seen
            seen.add(identity)
            assert row["sha256"] == selected[key]["sha256"]
            assert row["wrong"] == "false"
            for prefix in ("", "original_"):
                answer = row.get(prefix + "answer")
                if row.get(prefix + "status") == "ok" and answer in ("sat", "unsat"):
                    assert row[prefix + "exit_code"] == "0"
                    assert row[prefix + "timeout"] == "false"
                    if not prefix:
                        assert float(row["seconds"]) <= 120
                    if selected[key]["expected"] in ("sat", "unsat"):
                        assert answer == selected[key]["expected"]
                    answers[key].add(answer)
            rows.append(row)
    assert all(len(values) <= 1 for values in answers.values())
    counts = dict(collections.Counter(r["profile"] for r in rows))
    assert counts == dict.fromkeys(names, len(selected))
    args.out.mkdir(parents=True, exist_ok=True)
    destination = args.out / "archived-references.csv"
    assert not destination.exists()
    with destination.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    metadata = {
        "status": "Complete input-hash-matched reuse; no new reference executions",
        "rows": len(rows),
        "counts": counts,
        "selection_sha256": sha(args.selection),
        "archive_sha256": sha(args.archive),
        "experiment_sha256": sha(args.experiment),
        "provenance_sha256": sha(args.provenance),
        "reference_reuse_sha256": provenance["reference_reuse_sha256"],
        "tools": {n: provenance["tools"][n] for n in sorted(names)},
        "export_sha256": sha(destination),
        "script_sha256": sha(Path(__file__)),
        "limits": "Archived 1200-second source timings censored to120seconds by the audited uniform120 report;8GiB enforced address-space cap. Single archived reference run, separate from new three-repetition paired Primo measurements. Context only; no fresh paired-reference timing claim.",
    }
    (args.out / "reference-provenance.json").write_text(
        json.dumps(metadata, indent=2) + "\n"
    )
    print(json.dumps({"rows": len(rows), "counts": counts}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("selection", "archive", "experiment", "provenance", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    export(parser.parse_args())


if __name__ == "__main__":
    main()
