#!/usr/bin/env python3
"""Audit full-query review samples; stack observations are not CPU or timing scores."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path

GROUPS = {
    "explanation": (
        "FlatCongruenceClosure::explain",
        "FlatCongruenceClosure::append_edge_reason",
    ),
    "closure_update": (
        "FlatCongruenceClosure::propagate",
        "FlatCongruenceClosure::merge_classes",
    ),
    "selector": ("ConflictAckermannSelector::", "conflict_application_pairs"),
    "statistics": ("Statistics::increment", "ConflictAckermannSelector::count"),
    "preprocessing": ("prepare_and_execute_preprocessing", "UfSymmetryBreaker::"),
    "sat": ("CaDiCaL::",),
}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def audit(a):
    config = json.loads(a.config.read_text())
    selection = json.loads(a.selection.read_text())["cases"]
    protocol = json.loads((a.selection.parent / "protocol.json").read_text())
    assert sha(a.selection) == protocol["selection_sha256"]
    assert config["git"] == protocol["candidate_source"]
    assert [c["file"] for c in config["cases"]] == [
        "PEQ/PEQ011_size8.smt2",
        "SEQ/SEQ026_size7.smt2",
    ]
    assert set(config["profiles"]) == {"OFF", "ON"}
    for label, profile in config["profiles"].items():
        assert profile["args"] == protocol["profiles"][label]
        assert profile["git"] == config["git"] and profile["sha256"] == config["sha256"]
    records = []
    for slot, case in enumerate(config["cases"]):
        assert case in selection and case["expected"] == "unsat"
        directory = a.rows / f"case-{slot}"
        meta = json.loads((directory / "metadata.json").read_text())
        assert meta["case"] == case and meta["source"] == config["git"]
        assert meta["native_sha256"] == config["sha256"]
        assert meta["config_sha256"] == sha(a.config)
        assert meta["sampler_sha256"] == sha(Path("scripts/profile_gdb_samples.py"))
        assert meta["workload_sha256"] == sha(
            Path("scripts/qualification/conflict_ackermann/review_profile.sbatch")
        )
        assert [(r["repetition"], r["profile"]) for r in meta["records"]] == [
            (0, "OFF"),
            (0, "ON"),
            (1, "ON"),
            (1, "OFF"),
        ]
        for entry in meta["records"]:
            rep, label = entry["repetition"], entry["profile"]
            d = directory / f"rep-{rep}-{label}"
            summary = json.loads((d / "summary.json").read_text())
            assert summary == entry["summary"] and summary["status"] == "complete"
            assert summary["executable_sha256"] == config["sha256"]
            assert summary["command"] == [
                config["profiles"][label]["solver"],
                *config["profiles"][label]["args"],
                str(Path(config["roots"][case["logic"]]) / case["file"]),
            ]
            assert (
                summary["seed"] == 2026111710 + slot * 2 + rep
                and summary["interval"] == 0.05
            )
            log = (d / "gdb-mi.log").read_text()
            assert "unsat" in log.splitlines() or '@"unsat\\n"' in log
            samples = [
                json.loads(line)
                for line in (d / "samples.jsonl").read_text().splitlines()
            ]
            assert len(samples) == summary["samples"] and samples
            counts = collections.Counter()
            top = collections.Counter()
            for sample in samples:
                frames = sample["frames"]
                top.update(frames[:1])
                for name, patterns in GROUPS.items():
                    counts[name] += any(
                        pattern in frame for frame in frames for pattern in patterns
                    )
            records.append(
                {
                    "case": case,
                    "profile": label,
                    "repetition": rep,
                    "samples": len(samples),
                    "seed": summary["seed"],
                    "group_samples": dict(counts),
                    "top_frames": top.most_common(8),
                    "samples_sha256": sha(d / "samples.jsonl"),
                    "summary_sha256": sha(d / "summary.json"),
                    "log_sha256": sha(d / "gdb-mi.log"),
                }
            )
    assert len(records) == 8
    report = {
        "native_source": config["git"],
        "native_sha256": config["sha256"],
        "sampler_sha256": sha(Path("scripts/profile_gdb_samples.py")),
        "workload_sha256": sha(
            Path("scripts/qualification/conflict_ackermann/review_profile.sbatch")
        ),
        "scope": "Two completed UNSAT QF_UF queries,OFF/ON,two rotated repetitions;GDB wall-stack observations,0.05s jitter/90s deadline,10GiB allocation,no native address-space cap. Inclusive groups overlap;top41frames may truncate deep stacks;inlined functions may be absent. No CPU percentages or benchmark timing inference.",
        "groups": GROUPS,
        "records": records,
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {"records": len(records), "samples": sum(r["samples"] for r in records)}
        )
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("config", "rows", "selection", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    audit(p.parse_args())


if __name__ == "__main__":
    main()
