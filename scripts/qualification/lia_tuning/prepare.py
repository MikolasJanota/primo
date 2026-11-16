#!/usr/bin/env python3
"""Freeze a targeted parameter pilot and configure existing frozen SLURM runners."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path

POLICIES = {
    "PVT": ("private", 1, 100000),
    "H0": ("hybrid", 0, 100000),
    "H1k": ("hybrid", 1, 1024),
    "H1": ("hybrid", 1, 100000),
    "H16k": ("hybrid", 16, 1024),
    "H16": ("hybrid", 16, 100000),
    "H4096k": ("hybrid", 4096, 1024),
    "H4096": ("hybrid", 4096, 100000),
}
LOGICS = ("QF_LIA", "QF_UFLIA")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def flags(policy, rows):
    mode, factor, cap = POLICIES[policy]
    return [
        "--quiet",
        "--stats",
        "--lra-propagation",
        *(
            ["--lra-row-propagation", "--lra-bidirectional-row-propagation"]
            if rows
            else []
        ),
        f"--lia-branching={mode}",
        f"--lia-private-branch-factor={factor}",
        "--lia-split-atom-limit=0",
        f"--lia-branch-limit={cap}",
    ]


def tasks():
    return [
        {
            "logic": logic,
            "label": f"r{row}-{p}",
            "args": flags(p, row),
            "parameters": {
                "rows": bool(row),
                "policy": p,
                "mode": mode,
                "factor": factor,
                "node_cap": cap,
            },
        }
        for logic in LOGICS
        for row in range(2)
        for p, (mode, factor, cap) in POLICIES.items()
    ]


def plan(args):
    parent = json.loads(args.selection.read_text())
    original = parent["cases"]
    mandatory = {
        "QF_LIA": [8, 24, 40, 61, 85, 95],
        "QF_UFLIA": [210, 213, 229, 253, 255],
    }
    excluded = {2, 17, 460}
    chosen = []
    for logic in LOGICS:
        selected = list(mandatory[logic])
        families = collections.defaultdict(list)
        for index, case in enumerate(original):
            if case["logic"] != logic or index in selected or index in excluded:
                continue
            family = "/".join(case["file"].split("/")[:2])
            families[family].append(index)
        for group in families.values():
            group.sort(
                key=lambda i: hashlib.sha256(
                    ("20261116:" + original[i]["sha256"]).encode()
                ).hexdigest()
            )
        while len(selected) < 20:
            old = len(selected)
            for family in sorted(families):
                if families[family] and len(selected) < 20:
                    selected.append(families[family].pop(0))
            assert len(selected) > old
        chosen.extend(dict(original[i], parent_slot=i) for i in selected)
    args.out.mkdir(parents=True, exist_ok=True)
    selection = {
        "parent_selection": str(args.selection),
        "parent_selection_sha256": sha(args.selection),
        "scope": "Target-enriched development pilot,20inputs/logic; not a representative ranking or global tuning set.",
        "mandatory_parent_slots": mandatory,
        "excluded_timing_canaries": {
            "2/17/460": "Previously slow but no private-node/split activity in completed controls; excluded only from this targeted timing pilot, not regression correctness gates."
        },
        "fill": "Deterministic family round-robin using first2pathcomponents; candidates ordered by SHA256(20261116:inputSHA).",
        "cases": chosen,
    }
    (args.out / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    protocol = {
        "scope": "Bounded exploratory existing-option pilot; no solver/default changes, no global optimum claim.",
        "selection_sha256": sha(args.out / "selection.json"),
        "inputs": 40,
        "row_contexts": 2,
        "policies": POLICIES,
        "tasks": tasks(),
        "repetitions": 3,
        "native_measurements": 1920,
        "seconds": 30,
        "address_space_bytes": 8589934592,
        "native_source": "4f02a8b7",
        "native_sha256": "08948789d787e2a81e76ad671738f29ac192dde34b4c9276789dff55b03a44ff",
        "build": "Reuse immutable Release binary prepared via --build-via-slurm,build401293/check401294. Preflight immediately before each new submission.",
        "generated_seed_base": 2026114000,
        "mutation_seed_base": 2026114100,
        "qualification": "1000fresh generated/ref cases and exact-option tracked regressions per32logic/context/policy tasks; real-seed mutations per task; all nativeSAT independently Dolmen checked. Resource-only incomplete outcomes separate; stop on wronganswer/crash/invalidmodel/unexplained disagreement.",
        "resource_probes": {
            "parent_slots": {"r0": [48, 198, 376, 332], "r1": [48, 198, 368, 453]},
            "atom_caps": [0, 1, 8, 64],
            "factor": 1,
            "node_cap": 100000,
            "seconds": 30,
            "rows": 32,
            "kind": "Correctness probes, not tuning scores; known unlimited prefixes require multiple splits; capped unknown must have exact resource diagnostic. All successfulSAT separately Dolmen.",
        },
        "schedule": "32generated+32seed+32mutation gate tasks; audit all before80case/context timing tasks in3sequential arrays,64concurrency; model arrays32each only after last timing. No own gates/models compete with timings.",
        "analysis": "Perlogic/context solved,meanPAR2(unsolved60s),perrepmedian summaries,memory,private work/splits/SATresumes; paired candidates vsH1 andPVT, gains/losses and cutoff sensitivity. Scores descriptive for enriched pilot only; model time excluded.",
        "defaults": "Unchanged; later independent global tuning required.",
    }
    (args.out / "protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")


def configure(args):
    protocol = json.loads(args.protocol.read_text())
    selection = json.loads(args.selection.read_text())
    assert sha(args.selection) == protocol["selection_sha256"]
    assert protocol["tasks"] == tasks()
    assert protocol["inputs"] == len(selection["cases"]) == 40
    assert protocol["seconds"] == 30 and protocol["address_space_bytes"] == 8589934592
    assert protocol["native_measurements"] == 1920 and protocol["repetitions"] == 3
    parent = json.loads(args.parent_campaign.read_text())
    assert (
        parent["git"] == protocol["native_source"]
        and parent["sha256"] == protocol["native_sha256"]
    )
    args.out.mkdir(parents=True, exist_ok=False)
    profiles = {t["label"]: t["args"] for t in tasks() if t["logic"] == LOGICS[0]}
    (args.out / "profile-args-integers.json").write_text(
        json.dumps(profiles, indent=2) + "\n"
    )
    mapping = []
    for index, case in enumerate(selection["cases"]):
        for row in range(2):
            cohort = f"case-{index}-r{row}"
            mapping.append({"cohort": cohort, "slot": 0})
            for rep in range(3):
                order = list(POLICIES)
                offset = (index + 3 * rep) % len(order)
                order = order[offset:] + order[:offset]
                if (index + rep) % 2:
                    order.reverse()
                config = {
                    "cases": [
                        dict(case, pilot_slot=index, row_context=row, repetition=rep)
                    ],
                    "roots": parent["roots"],
                    "out": f"{args.results_root}/timings/rep-{rep}/{cohort}",
                    "timeout": 30,
                    "address_space_bytes": 8589934592,
                    "profiles": {
                        p: {
                            "solver": parent["solver"],
                            "git": parent["git"],
                            "sha256": parent["sha256"],
                            "args": flags(p, row),
                        }
                        for p in order
                    },
                }
                (args.out / f"{cohort}-{rep}.json").write_text(
                    json.dumps(config, indent=2) + "\n"
                )
    campaign = {
        **parent,
        "configs": args.remote_configs,
        "results": args.results_root + "/qualification",
        "mapping": mapping,
        "tasks": tasks(),
        "protocol": protocol,
    }
    (args.out / "campaign.json").write_text(json.dumps(campaign, indent=2) + "\n")
    print(
        json.dumps(
            {
                "tasks": len(tasks()),
                "timing_allocations_per_rep": len(mapping),
                "timing_rows": 1920,
            }
        )
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("plan", "configure"))
    p.add_argument("--selection", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--protocol", type=Path)
    p.add_argument("--parent-campaign", type=Path)
    p.add_argument("--remote-configs")
    p.add_argument("--results-root")
    a = p.parse_args()
    if a.phase == "plan":
        plan(a)
    else:
        assert a.protocol and a.parent_campaign and a.remote_configs and a.results_root
        configure(a)


if __name__ == "__main__":
    main()
