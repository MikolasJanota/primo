#!/usr/bin/env python3
"""Run one rotated diagnostic case/repetition, or replay all its successful SAT rows."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import resource
import subprocess
import sys
from pathlib import Path

from profiles import TIMING_PROFILES, flags

ROOT = next(p for p in Path(__file__).resolve().parents if (p / "scripts/bench_qf_uf_full.py").is_file())
sys.path.insert(0, str(ROOT))
from scripts.bench_qf_uf_full import parse_smtlib_status, run_solver  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("timing", "models"))
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    case = manifest["cases"][args.slot // 3]
    rep = args.slot % 3
    source = args.root / case["file"]
    assert sha(source) == case["sha256"]
    directory = args.out / f"slot-{args.slot}"
    binaries = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    if args.phase == "timing":
        directory.mkdir(parents=True, exist_ok=False)
        offset = (args.slot // 3 + rep) % len(TIMING_PROFILES)
        order = TIMING_PROFILES[offset:] + TIMING_PROFILES[:offset]
        provenance = {"case": case, "repetition": rep, "order": order, "manifest_sha256": sha(args.manifest),
                      "binaries": {k: {"path": str(p), "sha256": sha(p), "version": subprocess.check_output([str(p), "--version"], text=True)} for k, p in binaries.items()},
                      "job": os.environ["SLURM_JOB_ID"], "timeout": 120, "runner_sha256": sha(Path(__file__))}
        assert provenance["binaries"]["baseline"]["sha256"] == manifest["baseline_binary_sha256"]
        assert "git: " + manifest["baseline_expected_git"] in provenance["binaries"]["baseline"]["version"].splitlines()
        assert "git: " + os.environ["PRIMO_EXPECTED_GIT"] in provenance["binaries"]["candidate"]["version"].splitlines()
        resource.setrlimit(resource.RLIMIT_AS, (manifest["native_address_space_bytes"], manifest["native_address_space_bytes"]))
        (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        with (directory / "rows.jsonl").open("w") as stream:
            for profile in order:
                key = "baseline" if profile == "baseline-off" else "candidate"
                argv = flags(profile)
                if "--stats" not in argv:
                    argv.append("--stats")
                result = run_solver([str(binaries[key]), *argv], source, 120)
                for output in ("stdout", "stderr"):
                    (directory / f"{profile}.{output}").write_text(str(result.pop(output)))
                expected = case["reference_answer"] if case["reference_status"] == "ok" else parse_smtlib_status(source)
                wrong = result["status"] == "ok" and result["answer"] in ("sat", "unsat") and expected in ("sat", "unsat") and result["answer"] != expected
                row = dict(result, profile=profile, binary=key, argv=argv, file=case["file"], repetition=rep, wrong=wrong)
                stream.write(json.dumps(row) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
                print(profile, result["status"], result["answer"], result["seconds"], flush=True)
                if wrong or result["status"] not in ("ok", "timeout"):
                    raise RuntimeError(f"blocking timing result: {row}")
        return 0
    provenance = json.loads((directory / "provenance.json").read_text())
    assert provenance["case"]["sha256"] == sha(source)
    for key, binary in binaries.items():
        assert sha(binary) == provenance["binaries"][key]["sha256"]
    selected = []
    with (directory / "rows.jsonl").open() as stream:
        for line in stream:
            row = json.loads(line)
            if row["status"] != "ok" or row["answer"] != "sat" or str(row["exit_code"]) != "0":
                continue
            model_out = directory / "models" / row["profile"]
            command = [sys.executable, "scripts/validate_model.py", "--solver", str(binaries[row["binary"]]),
                       *(f"--solver-arg={arg}" for arg in row["argv"]), "--solver-arg=--check-model",
                       "--expect-sat", "--require-dolmen-pass", "--dolmen-arg=--strict=false",
                       "--timeout", "180", "--dolmen-timeout", "60", "--out", str(model_out), str(source)]
            subprocess.run(command, check=True)
            with (model_out / "summary.csv").open(newline="") as summary:
                model = next(csv.DictReader(summary))
            assert model["outcome"] == "pass" and model["validator"] == "dolmen"
            selected.append({"profile": row["profile"], "file": row["file"], "repetition": rep,
                             "summary_sha256": sha(model_out / "summary.csv"), "validator": "dolmen"})
    (directory / "coverage.json").write_text(json.dumps({"selected_sat": len(selected), "direct_dolmen_pass": len(selected), "rows": selected}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
