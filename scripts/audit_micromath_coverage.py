#!/usr/bin/env python3
"""Reconcile saved UF certificate coverage without running any solver/checker."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from collections import Counter
from pathlib import Path

from select_micromath_fast_cases import DECLARATION, TOKEN, read_history


def digest(path: Path) -> str:
    state = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            state.update(block)
    return state.hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def signatures(args: argparse.Namespace) -> dict:
    """Reproduce the selector's conservative lexical declaration screen."""
    if not os.environ.get("SLURM_JOB_ID"):
        raise ValueError("corpus signature audit requires SLURM")
    records = read_history(args.history, args.source_git, None)
    manifest = read_json(args.manifest)
    selected = {r["file"]: r["sha256"] for r in manifest["cases"]}
    require(len(selected) == len(manifest["cases"]), "duplicate manifest path")
    seen_hashes = set()
    categories: Counter[str] = Counter()
    rows = []
    for row in records:
        relative = Path(row["file"])
        require(
            not relative.is_absolute() and ".." not in relative.parts, "unsafe path"
        )
        path = args.corpus / relative
        require(digest(path) == row["sha256"], f"source hash mismatch: {relative}")
        require(
            row["sha256"] not in seen_hashes, f"duplicate source content: {relative}"
        )
        seen_hashes.add(row["sha256"])
        bool_arg = False
        bool_result = False
        witnesses = []
        for declaration in DECLARATION.finditer(path.read_text()):
            arguments = TOKEN.findall(declaration[2])
            arg = any(a.strip("|") == "Bool" for a in arguments)
            result = bool(arguments) and declaration[3].strip("|") == "Bool"
            bool_arg |= arg
            bool_result |= result
            if arg or result:
                witnesses.append(declaration[0])
        category = (
            "bool_argument_and_result"
            if bool_arg and bool_result
            else "bool_argument_only"
            if bool_arg
            else "bool_result_only"
            if bool_result
            else "selected"
        )
        require(
            (category == "selected") == (row["file"] in selected),
            f"selection mismatch: {relative}",
        )
        if category == "selected":
            require(selected[row["file"]] == row["sha256"], "manifest source mismatch")
        categories[category] += 1
        if category != "selected":
            rows.append(
                {
                    **row,
                    "signature_category": category,
                    "declaration_witnesses": witnesses,
                }
            )
    require(len(selected) == categories["selected"], "manifest has unknown entries")
    return {
        "kind": "recorded-UNSAT-signature-audit",
        "job_id": os.environ["SLURM_JOB_ID"],
        "host": os.uname().nodename,
        "historical_source_git": args.source_git,
        "manifest_sha256": digest(args.manifest),
        "script_sha256": digest(Path(__file__)),
        "selector_sha256": digest(
            Path(__file__).with_name("select_micromath_fast_cases.py")
        ),
        "known_unsat": len(records),
        "categories": dict(categories),
        "scope": "Same lexical declare-fun screen as the original selector; not a complete SMT-LIB compatibility check. All source hashes rechecked; no solver/checker run.",
        "excluded": sorted(rows, key=lambda r: r["file"]),
    }


def confirm_kernel(path: Path, stage: dict) -> None:
    require(
        stage["state"] == "ok" and stage["exit_code"] == 0, f"failed kernel: {path}"
    )
    lines = path.read_text().splitlines()
    require(
        "challenge proven" in lines and "Inconsistency proven!" not in lines,
        f"unconfirmed kernel: {path}",
    )


def results(args: argparse.Namespace) -> dict:
    records = read_history(args.history, args.source_git, None)
    known = {row["file"]: row for row in records}
    manifest = read_json(args.manifest)
    cases = manifest["cases"]
    require(len({c["file"] for c in cases}) == len(cases), "duplicate manifest path")
    signature = read_json(args.signatures)
    require(
        signature["manifest_sha256"] == digest(args.manifest),
        "signature manifest mismatch",
    )
    require(
        signature["historical_source_git"] == args.source_git,
        "signature source revision mismatch",
    )
    excluded = signature["excluded"]
    require(
        {r["file"] for r in excluded}.isdisjoint(c["file"] for c in cases),
        "excluded/selected overlap",
    )
    require(
        {r["file"] for r in excluded} | {c["file"] for c in cases} == set(known),
        "history not fully accounted for",
    )
    states = {}
    originals = {}
    sources = {}
    journals = []
    for journal in sorted(args.sweep.glob("shard-*/rows.jsonl")):
        shard = journal.parent
        journals.append(
            {"file": str(journal.relative_to(args.sweep)), "sha256": digest(journal)}
        )
        for line in journal.read_text().splitlines():
            row = json.loads(line)
            index = row["index"]
            require(
                index not in states and 0 <= index < len(cases),
                "duplicate or invalid index",
            )
            require(row["case"] == cases[index], "sweep case differs from manifest")
            require(
                known[row["case"]["file"]]["sha256"] == row["case"]["sha256"],
                "history source differs",
            )
            require(
                len(row["profiles"]) == 1 and row["profiles"][0]["mode"] == "core",
                "not CORE-only",
            )
            profile = row["profiles"][0]
            originals[index] = profile
            states[index] = profile["status"]
            sources[index] = "347445"
            if profile["status"] == "kernel_proven":
                confirm_kernel(
                    shard / "cases" / f"{index:03d}" / "core/kernel.out",
                    profile["stages"]["kernel"],
                )
    require(set(states) == set(range(len(cases))), "missing sweep results")
    raw_indices = set()
    for path in sorted(args.raw.glob("shard-*/*/summary.json")):
        summary = read_json(path)
        require(
            summary["completed"] == summary["expected"] == len(summary["rows"]),
            "incomplete raw replay",
        )
        for row in summary["rows"]:
            index = int(row["case"])
            require(
                index not in raw_indices and index in states,
                "duplicate or unknown replay index",
            )
            require(
                states[index] in ("proof_error", "proof_timeout", "challenge_timeout"),
                "unexpected raw replay selection",
            )
            require(
                row["input_sha256"]
                == originals[index]["files"]["input.primo"]["sha256"],
                "raw certificate mismatch",
            )
            raw_indices.add(index)
            states[index] = row["status"]
            sources[index] = "347770"
            if row["status"] == "kernel_proven":
                confirm_kernel(
                    path.parent / "cases" / row["case"] / "kernel.out",
                    row["stages"]["kernel"],
                )
    require(len(raw_indices) == 805, "missing raw replay cases")
    pipe = read_json(args.pipe / "summary.json")
    require(
        pipe["completed"] == pipe["expected"] == len(pipe["rows"]) == 4,
        "incomplete pipe replay",
    )
    pipe_indices = set()
    for row in pipe["rows"]:
        index = int(row["case"])
        require(
            index not in pipe_indices and index in states,
            "duplicate or unknown pipe index",
        )
        require(states[index] == "proof_error", "unexpected pipe replay selection")
        require(
            row["input_sha256"] == originals[index]["files"]["input.primo"]["sha256"],
            "pipe certificate mismatch",
        )
        pipe_indices.add(index)
        stage = row["stages"]["proof_pipe"]
        require(stage["process_exit_codes"] == [0, 0], "failed pipe child")
        confirm_kernel(args.pipe / "cases" / row["case"] / "pipe/kernel.out", stage)
        states[index] = row["status"]
        sources[index] = "348181"
    families: dict[str, Counter] = {}
    uncovered = []
    for index, state in states.items():
        case = cases[index]
        families.setdefault(case["family"], Counter())[state] += 1
        if state != "kernel_proven":
            uncovered.append(
                {
                    "index": index,
                    **case,
                    "latest_status": state,
                    "latest_job": sources[index],
                    "original_main_unsat": originals[index]["statistics"].get(
                        ":export.main-unsat"
                    )
                    == "1",
                }
            )
    historical = Counter()
    for path in args.history.glob("shard-*/answers.csv"):
        for row in csv.DictReader(path.open(newline="")):
            historical[f"{row['status']}:{row['answer']}"] += 1
    return {
        "kind": "saved-UF-certificate-coverage-audit",
        "historical_source_git": args.source_git,
        "script_sha256": digest(Path(__file__)),
        "manifest_sha256": digest(args.manifest),
        "signature_audit_sha256": digest(args.signatures),
        "signature_audit_job": signature["job_id"],
        "history_shards": len(list(args.history.glob("shard-*/answers.csv"))),
        "historical_outcomes": dict(historical),
        "known_unsat": len(records),
        "signature_categories": signature["categories"],
        "selected": len(cases),
        "attempted": len(originals),
        "latest_counts": dict(Counter(states.values())),
        "families": {k: dict(v) for k, v in sorted(families.items())},
        "uncovered": sorted(uncovered, key=lambda r: r["index"]),
        "excluded": excluded,
        "audit_inputs": journals,
        "scope": "Reconciliation of recorded historical UNSAT and saved proof logs; no new solver/checker run. SAT answers are outside UNSAT certification. History is the selected run, not a union of all revisions/configurations.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    for mode in ("signatures", "results"):
        sub = modes.add_parser(mode)
        sub.add_argument("--history", type=Path, required=True)
        sub.add_argument("--source-git", required=True)
        sub.add_argument("--manifest", type=Path, required=True)
        sub.add_argument("--out", type=Path, required=True)
        if mode == "signatures":
            sub.add_argument("--corpus", type=Path, required=True)
        else:
            for flag in ("signatures", "sweep", "raw", "pipe"):
                sub.add_argument(f"--{flag}", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = signatures(args) if args.mode == "signatures" else results(args)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"audit failed: {error}\n")
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k not in ("excluded", "uncovered", "audit_inputs")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
