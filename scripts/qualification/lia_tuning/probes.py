#!/usr/bin/env python3
"""Prepare and audit finite-atom correctness probes using frozen native runners."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.bound_propagation.analyze import (  # noqa: E402
    audit_models,
    audit_row,
    solved,
)
from scripts.qualification.lia_hybrid.run import measurement_order  # noqa: E402
from scripts.qualification.lia_tuning.prepare import flags, sha  # noqa: E402


def configure(a):
    parent = json.loads(a.parent_campaign.read_text())
    protocol = json.loads(a.protocol.read_text())
    selection = json.loads(a.parent_selection.read_text())["cases"]
    assert (
        parent["git"] == protocol["native_source"]
        and parent["sha256"] == protocol["native_sha256"]
    )
    a.out.mkdir(parents=True, exist_ok=False)
    mapping = []
    manifest = []
    for row_key, slots in protocol["resource_probes"]["parent_slots"].items():
        row = int(row_key[1:])
        for slot in slots:
            index = len(mapping)
            cohort = f"probe-{index}"
            case = dict(
                selection[slot], parent_slot=slot, row_context=row, repetition=0
            )
            mapping.append({"cohort": cohort, "slot": 0})
            profiles = {}
            for cap in protocol["resource_probes"]["atom_caps"]:
                args = [
                    x
                    if x != "--lia-split-atom-limit=0"
                    else f"--lia-split-atom-limit={cap}"
                    for x in flags("H1", row)
                ]
                profiles[f"A{cap}"] = {
                    "solver": parent["solver"],
                    "git": parent["git"],
                    "sha256": parent["sha256"],
                    "args": args,
                }
            config = {
                "cases": [case],
                "roots": parent["roots"],
                "out": f"{a.results_root}/rep-0/{cohort}",
                "timeout": 30,
                "address_space_bytes": 8589934592,
                "profiles": profiles,
            }
            path = a.out / f"{cohort}-0.json"
            path.write_text(json.dumps(config, indent=2) + "\n")
            manifest.append(
                {
                    "index": index,
                    "parent_slot": slot,
                    "row_context": row,
                    "logic": case["logic"],
                    "file": case["file"],
                    "input_sha256": case["sha256"],
                    "config_sha256": sha(path),
                }
            )
    campaign = {**parent, "configs": a.remote_configs, "mapping": mapping}
    (a.out / "campaign.json").write_text(json.dumps(campaign, indent=2) + "\n")
    (a.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"allocations": len(mapping), "native_rows": len(mapping) * 4}))


def analyze(a):
    manifest = json.loads((a.configs / "manifest.json").read_text())
    results = []
    models = 0
    for item in manifest:
        index = item["index"]
        path = a.configs / f"probe-{index}-0.json"
        assert sha(path) == item["config_sha256"]
        config = json.loads(path.read_text())
        case = config["cases"][0]
        d = a.rows / f"rep-0/probe-{index}/slot-0"
        assert (d / "timing-complete").is_file()
        prov = json.loads((d / "provenance.json").read_text())
        assert (
            prov["config_sha256"] == sha(path)
            and prov["case"] == case
            and prov["profiles"] == config["profiles"]
        )
        assert prov["runner_sha256"] == sha(
            ROOT / "scripts/qualification/lia_hybrid/run.py"
        )
        rows = [json.loads(s) for s in (d / "rows.jsonl").read_text().splitlines()]
        assert len(rows) == 4
        assert (
            [r["profile"] for r in rows]
            == prov["order"]
            == measurement_order(config, 0)
        )
        control = next(r for r in rows if r["profile"] == "A0")
        assert solved(control) and control["answer"] == case["expected"]
        control_atoms = int(
            control["stats"].get(":lia.branching.peak-generated-atoms", 0)
        )
        assert control_atoms > 1
        for r in rows:
            try:
                audit_row(r)
            except AssertionError:
                raise RuntimeError(
                    f"Probe failure index={index} profile={r['profile']} exit={r['exit_code']}"
                ) from None
            assert (
                r["args"] == config["profiles"][r["profile"]]["args"]
                and r["logic"] == case["logic"]
                and r["file"] == case["file"]
            )
            cap = int(r["profile"][1:])
            stderr = d / (r["profile"] + ".stderr")
            diagnostic = [
                x
                for x in stderr.read_text().splitlines()
                if x.strip() and not x.startswith(":")
            ]
            if cap and control_atoms > cap:
                assert (
                    r["status"] == "ok"
                    and r["exit_code"] == 0
                    and r["answer"] == "unknown"
                )
                assert diagnostic == ["integer split atom limit"]
                assert (
                    int(r["stats"].get(":lia.branching.split-budget-exhaustions", 0))
                    > 0
                )
                assert (
                    int(r["stats"].get(":lia.branching.peak-generated-atoms", 0)) <= cap
                )
            else:
                assert solved(r) and r["answer"] == control["answer"]
        selected = {r["profile"]: r for r in rows if solved(r) and r["answer"] == "sat"}
        h = audit_models(d, selected, config, case)
        assert h is not None
        models += len(selected)
        results.append(
            {
                **item,
                "rows_sha256": sha(d / "rows.jsonl"),
                "coverage_sha256": sha(d / "coverage.json"),
                "model_metadata_sha256": h,
                "control_atoms": control_atoms,
                "outcomes": {r["profile"]: r["answer"] for r in rows},
            }
        )
    a.out.write_text(
        json.dumps(
            {
                "status": "qualified resource probes only",
                "native_rows": 32,
                "SAT_Dolmen_pass": models,
                "cases": results,
                "scope": "Finite atom caps produce diagnosed unknown, never a logical refutation. These are correctness probes, excluded from tuning scores.",
            },
            indent=2,
        )
        + "\n"
    )
    print(json.dumps({"native_rows": 32, "SAT_Dolmen_pass": models}))


def unknowns(a):
    """Bind each mutation unknown to an exact-option, diagnosed SLURM replay."""
    gate = json.loads(a.gates.read_text())
    campaign = json.loads((a.configs / "campaign.json").read_text())
    candidates = gate["mutation_unknown_replays_needed"]
    assert len(candidates) == len(campaign["mapping"])
    results = []
    for i, (candidate, mapping) in enumerate(zip(candidates, campaign["mapping"])):
        config_path = a.configs / f"{mapping['cohort']}-0.json"
        config = json.loads(config_path.read_text())
        case = config["cases"][0]
        task = campaign["tasks"][candidate["task_slot"]]
        assert (
            case["file"] == candidate["file"]
            and case["task_slot"] == candidate["task_slot"]
        )
        assert case["logic"] == task["logic"]
        profile = config["profiles"]["replay"]
        assert profile["args"] == task["args"] + ["--check-model"]
        assert (
            profile["git"] == gate["source"]
            and profile["sha256"] == gate["native_sha256"]
        )
        d = a.rows / f"rep-0/{mapping['cohort']}/slot-0"
        assert (d / "timing-complete").is_file()
        prov = json.loads((d / "provenance.json").read_text())
        assert prov["config_sha256"] == sha(config_path) and prov["case"] == case
        assert prov["profiles"] == config["profiles"] and prov["runner_sha256"] == sha(
            ROOT / "scripts/qualification/lia_hybrid/run.py"
        )
        rr = [json.loads(s) for s in (d / "rows.jsonl").read_text().splitlines()]
        assert len(rr) == 1
        r = rr[0]
        audit_row(r)
        assert r["args"] == profile["args"] and r["file"] == case["file"]
        assert r["status"] == "ok" and r["exit_code"] == 0 and r["answer"] == "unknown"
        diagnostic = [
            s
            for s in (d / "replay.stderr").read_text().splitlines()
            if s.strip() and not s.startswith(":")
        ]
        assert diagnostic == ["integer branch-and-bound node limit"]
        assert int(r["stats"].get(":lia.branching.budget-exits", 0)) > 0
        results.append(
            {
                "index": i,
                "task_slot": candidate["task_slot"],
                "input_sha256": case["sha256"],
                "args": r["args"],
                "diagnostic": diagnostic[0],
                "outcome": "resource-incomplete",
                "rows_sha256": sha(d / "rows.jsonl"),
                "provenance_sha256": sha(d / "provenance.json"),
            }
        )
    a.out.write_text(
        json.dumps({"diagnosed_unknowns": len(results), "replays": results}, indent=2)
        + "\n"
    )
    print(json.dumps({"diagnosed_unknowns": len(results)}))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("configure", "analyze", "unknowns"))
    p.add_argument("--out", type=Path, required=True)
    for x in ("parent-campaign", "parent-selection", "protocol", "configs", "rows"):
        p.add_argument("--" + x, type=Path)
    for x in ("remote-configs", "results-root"):
        p.add_argument("--" + x)
    p.add_argument("--gates", type=Path)
    a = p.parse_args()
    if a.phase == "configure":
        assert (
            a.parent_campaign
            and a.parent_selection
            and a.protocol
            and a.remote_configs
            and a.results_root
        )
        configure(a)
    else:
        assert a.configs and a.rows
        if a.phase == "unknowns":
            assert a.gates
            unknowns(a)
        else:
            analyze(a)


if __name__ == "__main__":
    main()
