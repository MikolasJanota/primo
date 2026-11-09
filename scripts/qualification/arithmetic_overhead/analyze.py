#!/usr/bin/env python3
"""Audit paired timings, complete direct SAT-model coverage and logical counters."""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.analyze_lia_cubes import cost, paired  # noqa: E402

NEW_COUNTERS = {
    ":lia.patch-interval-requests",
    ":lia.patch-interval-computations",
    ":lia.patch-interval-cache-hits",
    ":preprocessing.cite.uniformity-checks",
    ":preprocessing.cite.leaf-comparisons",
    ":preprocessing.cite.leaf-scan-equivalent",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open() as stream:
        return list(csv.DictReader(stream))


def complete_answer(row: dict[str, str]) -> bool:
    return (
        row["status"] == "ok" and row["exit_code"] == "0" and row["timeout"] == "false"
    )


def logical_counter(key: str) -> bool:
    # Arithmetic work is supposed to shrink. These counters retain their old
    # meanings, but not their old values; microsecond fields are durations.
    return (
        key.startswith(":")
        and key not in NEW_COUNTERS
        and not key.startswith((":time.", ":rational."))
        and not key.endswith("microseconds")
    )


def counter_differences(left: dict[str, str], right: dict[str, str]) -> dict:
    return {
        key: [left.get(key) or "0", right.get(key) or "0"]
        for key in sorted(left.keys() | right.keys())
        if logical_counter(key) and (left.get(key) or "0") != (right.get(key) or "0")
    }


def audit_shard(
    path: Path,
    profile: dict,
    inputs: dict[str, dict],
    timeout: float,
    require_models: bool,
) -> tuple[list[dict[str, str]], dict]:
    identity = json.loads((path / "provenance.json").read_text())
    expected = {name: item["sha256"] for name, item in inputs.items()}
    require(
        identity["status"] == "complete" and identity["inputs_unchanged"],
        f"incomplete timing provenance: {path}",
    )
    require(identity["inputs"] == expected, f"input identity mismatch: {path}")
    require(
        identity["solver"]["sha256"] == profile["binary_sha256"],
        f"binary identity mismatch: {path}",
    )
    require(
        identity["solver_args"] == profile["command"][1:]
        and float(identity["timeout"]) == timeout,
        f"profile/limit mismatch: {path}",
    )
    rows = read_rows(path / "answers.csv")
    require(
        len(rows) == len(inputs) and {r["file"] for r in rows} == inputs.keys(),
        f"missing, duplicated or unexpected timing rows: {path}",
    )
    for row in rows:
        require(
            row["wrong"] == "false" and row["status"] in {"ok", "timeout"},
            f"blocking solver outcome: {path}: {row['file']}",
        )
        require(
            row["status"] != "ok" or complete_answer(row),
            f"post-answer process error: {path}: {row['file']}",
        )
        require(not int(row.get(":lia.cube-rejected") or 0), "rejected model candidate")
        requests = int(row.get(":lia.patch-interval-requests") or 0)
        computations = int(row.get(":lia.patch-interval-computations") or 0)
        hits = int(row.get(":lia.patch-interval-cache-hits") or 0)
        require(requests == computations + hits, "inconsistent interval-work counters")
    successful_sat = {
        str(Path(identity["root"]) / row["file"])
        for row in rows
        if complete_answer(row) and row["answer"] == "sat"
    }
    coverage = {
        "expected_sat": len(successful_sat),
        "dolmen_pass": 0,
        "status": "pending",
    }
    if profile["binary"] != "z3":
        target = path / "model-validation"
        if (target / "coverage.json").exists():
            coverage = json.loads((target / "coverage.json").read_text())
            proof = coverage["provenance"]
            require(
                proof["parent_answers_sha256"] == digest(path / "answers.csv")
                and proof["parent_provenance_sha256"]
                == digest(path / "provenance.json"),
                f"model replay belongs to different timing data: {path}",
            )
            require(
                coverage["expected_sat"] == len(successful_sat)
                and coverage["inputs_and_binary_unchanged"],
                f"model identity mismatch: {path}",
            )
            manifest = target / "sat-manifest.txt"
            require(
                digest(manifest) == proof["manifest_sha256"],
                "SAT replay manifest changed",
            )
            replay_inputs = manifest.read_text().splitlines()
            require(
                len(replay_inputs) == len(successful_sat)
                and set(replay_inputs) == successful_sat,
                "SAT replay manifest coverage mismatch",
            )
            if coverage["status"] == "complete":
                require(
                    coverage["dolmen_pass"] == len(successful_sat),
                    "SAT coverage mismatch",
                )
                if successful_sat:
                    models = read_rows(target / "models/summary.csv")
                    require(
                        digest(target / "models/summary.csv")
                        == coverage["artifacts"]["models/summary.csv"],
                        "model summary changed",
                    )
                    require(
                        len(models) == len(successful_sat)
                        and {r["file"] for r in models} == successful_sat,
                        f"missing, duplicate or unexpected replay rows: {path}",
                    )
                    require(
                        all(
                            r["outcome"] == "pass"
                            and r["validator"] == "dolmen"
                            and r["answer"] == "sat"
                            and r["solver_exit"] == "0"
                            and r["dolmen_exit"] == "0"
                            for r in models
                        ),
                        f"invalid direct model replay: {path}",
                    )
        if require_models:
            require(
                coverage["status"] == "complete",
                f"incomplete direct SAT coverage: {path}",
            )
    else:
        coverage = {"status": "reference", "expected_sat": 0, "dolmen_pass": 0}
    return rows, coverage


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--suites", type=Path, required=True)
    parser.add_argument("--configs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--allow-pending-models", action="store_true")
    args = parser.parse_args()
    suites = json.loads(args.suites.read_text())
    report = {
        "status": "pending-models" if args.allow_pending_models else "complete",
        "suites": {},
        "suspected_runtime_regressions": [],
    }
    flat = []
    for suite_name, suite in suites.items():
        config = json.loads((args.configs / (suite_name + "-private.json")).read_text())
        entries = {entry["file"]: entry for entry in config["inputs"]}
        require(len(entries) == suite["files"], "duplicate/incomplete input selection")
        repetitions = {}
        for repetition in range(config["repetitions"]):
            root = args.root / suite_name / f"repetition-{repetition}"
            profiles = {}
            covers = []
            resources = []
            for task in range(suite["shards"]):
                meta = json.loads((root / f"shard-{task}/provenance.json").read_text())
                expected = config["inputs"][task :: suite["shards"]]
                require(
                    meta["inputs"] == expected and meta["task"] == task,
                    "shard input/resource provenance mismatch",
                )
                derived_path = root / "configs" / f"shard-{task}.json"
                derived = json.loads(derived_path.read_text())
                require(
                    meta["config_sha256"] == digest(derived_path)
                    and derived["parent_config_sha256"]
                    == digest(args.configs / (suite_name + "-private.json")),
                    "changed parent/derived timing configuration",
                )
                offset = (repetition + task) % len(config["profiles"])
                order = config["profiles"][offset:] + config["profiles"][:offset]
                require(
                    meta["profiles"] == order and int(meta["cpus_requested"]) == 1,
                    "unexpected timing rotation or CPU allocation",
                )
                resources.append(
                    {
                        k: meta[k]
                        for k in (
                            "job",
                            "task",
                            "node",
                            "cpu_model",
                            "cpus_requested",
                            "memory_requested_mb",
                            "config_sha256",
                        )
                    }
                )
            for profile in config["profiles"]:
                rows = {}
                for task in range(suite["shards"]):
                    selected = {
                        e["file"]: e for e in config["inputs"][task :: suite["shards"]]
                    }
                    data, cover = audit_shard(
                        root / profile["name"] / f"shard-{task}",
                        profile,
                        selected,
                        config["timeout"],
                        not args.allow_pending_models,
                    )
                    covers.append(cover)
                    for row in data:
                        require(row["file"] not in rows, "duplicated timing input")
                        rows[row["file"]] = row
                        flat.append(
                            dict(
                                row,
                                suite=suite_name,
                                repetition=repetition,
                                profile=profile["name"],
                                file=entries[row["file"]]["logical_file"],
                            )
                        )
                require(rows.keys() == entries.keys(), "incomplete profile input set")
                profiles[profile["name"]] = rows
            # Check all concrete decisions, including references, independently
            # of which native profiles are selected for performance comparison.
            for name in entries:
                concrete = {
                    p[name]["answer"]
                    for p in profiles.values()
                    if complete_answer(p[name])
                    and p[name]["answer"] in {"sat", "unsat"}
                }
                require(
                    len(concrete) <= 1, f"contradictory successful decisions: {name}"
                )
            result = {
                "profiles": {},
                "paired": {},
                "preservation": {},
                "resources": resources,
                "SAT_models": {
                    "expected": sum(c["expected_sat"] for c in covers),
                    "direct_Dolmen_pass": sum(c["dolmen_pass"] for c in covers),
                    "shard_statuses": dict(Counter(c["status"] for c in covers)),
                },
            }
            for label, rows in profiles.items():
                families = defaultdict(list)
                for name, row in rows.items():
                    families[
                        entries[name]["logic"] + "/" + entries[name]["family"]
                    ].append(row)
                counters = sorted(
                    {
                        key
                        for r in rows.values()
                        for key in r
                        if key.startswith(":")
                        and not key.startswith(":time.")
                        and not key.endswith("microseconds")
                    }
                )
                result["profiles"][label] = dict(
                    cost(list(rows.values()), config["timeout"]),
                    families={
                        k: cost(v, config["timeout"])
                        for k, v in sorted(families.items())
                    },
                    counters_lower_bounds={
                        key: sum(int(r.get(key) or 0) for r in rows.values())
                        for key in counters
                    },
                )
            for profile in config["profiles"]:
                identity = json.loads(
                    (root / profile["name"] / "shard-0/provenance.json").read_text()
                )
                result["profiles"][profile["name"]]["binary"] = {
                    "arm": profile["binary"],
                    "sha256": profile["binary_sha256"],
                    "version": identity["solver"]["version"],
                    "arguments": profile["command"][1:],
                }
            settings = []
            for cover in covers:
                if "provenance" not in cover:
                    continue
                proof = cover["provenance"]
                item = {
                    key: proof[key]
                    for key in (
                        "validator_script_sha256",
                        "solver_timeout",
                        "dolmen_timeout",
                        "dolmen_args",
                        "dolmen_size",
                    )
                }
                item["dolmen"] = {
                    key: proof["dolmen"][key] for key in ("version", "sha256")
                }
                if item not in settings:
                    settings.append(item)
            result["SAT_models"]["configurations"] = settings
            for label, right in profiles.items():
                if not label.startswith("candidate-"):
                    continue
                for arm in ("baseline", "control"):
                    left_label = arm + label.removeprefix("candidate")
                    if left_label not in profiles:
                        continue
                    left = profiles[left_label]
                    key = left_label + "_vs_" + label
                    result["paired"][key] = paired(left, right, config["timeout"])
                    for outcome in ("gains", "losses"):
                        result["paired"][key][outcome] = [
                            entries[name]["logical_file"]
                            for name in result["paired"][key][outcome]
                        ]
                    checked = 0
                    for name in entries:
                        a, b = left[name], right[name]
                        if (
                            complete_answer(a)
                            and complete_answer(b)
                            and a["answer"] == b["answer"]
                        ):
                            checked += 1
                            require(
                                not counter_differences(a, b),
                                f"logical counter change: {key}: {name}: {counter_differences(a, b)}",
                            )
                            before, after = float(a["seconds"]), float(b["seconds"])
                            if (
                                before >= 0.05
                                and after > before * 1.15
                                and after - before > 0.1
                            ):
                                report["suspected_runtime_regressions"].append(
                                    {
                                        "suite": suite_name,
                                        "repetition": repetition,
                                        "comparison": key,
                                        "file": entries[name]["logical_file"],
                                        "baseline_seconds": before,
                                        "candidate_seconds": after,
                                    }
                                )
                    result["preservation"][key] = {
                        "matching_completed_answers": checked,
                        "logical_counter_differences": 0,
                    }
            repetitions[str(repetition)] = result
        report["suites"][suite_name] = {
            "files": len(entries),
            "timeout": config["timeout"],
            "repetitions": repetitions,
        }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    stream = io.StringIO(newline="")
    columns = [
        "suite",
        "repetition",
        "profile",
        "file",
        "status",
        "answer",
        "expected",
        "wrong",
        "seconds",
        "timeout",
        "exit_code",
    ]
    columns += sorted({key for row in flat for key in row if key.startswith(":")})
    writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(flat)
    (args.out / "rows.csv.gz").write_bytes(
        gzip.compress(stream.getvalue().encode(), mtime=0)
    )
    print(
        json.dumps(
            {
                "rows": len(flat),
                "suites": len(suites),
                "suspected_regressions": len(report["suspected_runtime_regressions"]),
            }
        )
    )


if __name__ == "__main__":
    main()
