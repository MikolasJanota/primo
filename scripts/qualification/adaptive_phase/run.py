#!/usr/bin/env python3
"""Run bounded phase-probe arms serially within one SLURM allocation."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = next(parent for parent in Path(__file__).resolve().parents
                 if (parent / "scripts/bench_qf_uf_full.py").is_file())
sys.path.insert(0, str(REPO_ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402

M124 = ["--lia-round-integer-bounds", "--lia-primitive-int-rows",
        "--no-lia-integer-slacks", "--lia-patch-integral-moves"]
NATIVE = ["--no-lia-round-integer-bounds", "--no-lia-primitive-int-rows",
          "--no-lia-integer-slacks", "--no-lia-patch-integral-moves"]
ENV_KEYS = ["PRIMO_PHASE_PROBE", "PRIMO_PHASE_PROBE_K", "PRIMO_PHASE_PROBE_WINDOW",
            "PRIMO_PHASE_PROBE_TRACE"]


def profile(name: str) -> tuple[list[str], dict[str, str]]:
    flags = ["--quiet", "--stats"]
    env: dict[str, str] = {}
    if name == "native":
        return flags + NATIVE + ["--arithmetic-model-phase=off"], env
    if name == "m124":
        return flags + M124 + ["--arithmetic-model-phase=off"], env
    if name in {"force-positive", "force-negative"}:
        value = "1" if name == "force-positive" else "0"
        return flags + M124 + ["--arithmetic-model-phase=off",
                               "--cadical-option=forcephase=1",
                               f"--cadical-option=phase={value}"], env
    flags += M124 + ["--arithmetic-model-phase=on"]
    if name == "beta-uninstrumented":
        return flags, env
    if name == "beta":
        env["PRIMO_PHASE_PROBE"] = "observe"
    elif name.startswith(("release-", "invert-")):
        mode, count = name.split("-")
        threshold = int(count)
        if threshold < 0:
            raise ValueError(name)
        env.update(PRIMO_PHASE_PROBE=mode, PRIMO_PHASE_PROBE_K=str(threshold),
                   PRIMO_PHASE_PROBE_WINDOW="64")
    else:
        raise ValueError(name)
    return flags, env


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--profiles", nargs="+", required=True)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--trace", type=int, default=0)
    args = parser.parse_args()
    if args.out.exists() or args.timeout <= 0 or not 0 <= args.shard < args.shards:
        parser.error("fresh output, positive timeout and valid shard required")
    names = [s.strip() for s in args.manifest.read_text().splitlines()
             if s.strip() and not s.startswith("#")]
    inputs = [(i, args.root / name) for i, name in enumerate(names)
              if i % args.shards == args.shard]
    args.out.mkdir(parents=True)
    solver = args.solver.resolve()
    identity = {"runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "solver": str(solver), "solver_sha256": hashlib.sha256(solver.read_bytes()).hexdigest(),
                "version": subprocess.check_output([str(solver), "--version"], text=True),
                "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                "timeout": args.timeout, "profiles": args.profiles, "rounds": args.rounds,
                "shards": args.shards, "shard": args.shard,
                "inputs": {str(p.relative_to(args.root)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for _, p in inputs}}
    (args.out / "provenance.json").write_text(json.dumps(identity, indent=2) + "\n")
    for key in ENV_KEYS:
        os.environ.pop(key, None)
    with (args.out / "rows.jsonl").open("w") as rows:
        for round_index in range(args.rounds):
            for index, path in inputs:
                offset = (index + round_index) % len(args.profiles)
                order = args.profiles[offset:] + args.profiles[:offset]
                for name in order:
                    flags, env = profile(name)
                    for key in ENV_KEYS:
                        os.environ.pop(key, None)
                    os.environ.update(env)
                    if args.trace and env:
                        os.environ["PRIMO_PHASE_PROBE_TRACE"] = str(args.trace)
                        env["PRIMO_PHASE_PROBE_TRACE"] = str(args.trace)
                    result = run_solver([str(solver), *flags], path, args.timeout)
                    stem = f"r{round_index}-{index:03}-{name}"
                    (args.out / f"{stem}.stdout").write_text(str(result.pop("stdout")))
                    stderr = str(result.pop("stderr"))
                    (args.out / f"{stem}.stderr").write_text(stderr)
                    summaries = [json.loads(line.split(" ", 1)[1]) for line in stderr.splitlines()
                                 if line.startswith("PHASE_PROBE_SUMMARY ")]
                    expected = parse_smtlib_status(path)
                    wrong = (result["status"] == "ok" and result["answer"] in {"sat", "unsat"}
                             and bool(expected) and result["answer"] != expected)
                    row = dict(result, file=str(path.relative_to(args.root)), profile=name,
                               round=round_index, expected=expected, wrong=wrong,
                               command=[str(solver), *flags, str(path)], env=env,
                               probe=summaries, log_stem=stem)
                    rows.write(json.dumps(row) + "\n")
                    rows.flush()
                    print(json.dumps({k: row[k] for k in ("file", "profile", "status", "answer", "seconds")}), flush=True)
                    if wrong or result["status"] in {"error", "unsupported"}:
                        raise SystemExit("blocking solver outcome; remaining arms not run")
    after = {str(p.relative_to(args.root)): hashlib.sha256(p.read_bytes()).hexdigest()
             for _, p in inputs}
    if after != identity["inputs"]:
        raise SystemExit("input changed during diagnostic")


if __name__ == "__main__":
    main()
