#!/usr/bin/env python3
"""Stage metadata and submit paired arrays with durable global audit barriers."""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qualification.conflict_ackermann.submit import submit  # noqa: E402


def launch(args):
    protocol = json.loads((args.configs / "protocol.json").read_text())
    assert protocol["cases"] == 1404 and protocol["native_rows"] == 8424
    assert not args.state.exists()
    ssh = ["ssh", "-F", str(args.ssh_config), "-o", "ControlPath=none", args.remote]
    remote_shell = shlex.join(ssh[:-1])
    subprocess.run(
        [*ssh, shlex.join(["mkdir", "-p", args.remote_configs, args.remote_methods])],
        check=True,
    )
    # Only experiment metadata/methods are staged. The qualified native source
    # and its helper-built binary remain unchanged throughout both phases.
    subprocess.run(
        [
            "rsync",
            "-az",
            "-e",
            remote_shell,
            str(args.configs) + "/",
            args.remote + ":" + args.remote_configs + "/",
        ],
        check=True,
    )
    subprocess.run(
        [
            "rsync",
            "-az",
            "-e",
            remote_shell,
            str(Path(__file__).with_name("audit.py")),
            str(Path(__file__).with_name("audit.sbatch")),
            args.remote + ":" + args.remote_methods + "/",
        ],
        check=True,
    )
    state = {"timing": [], "models": [], "status": "submitting", "native_rows": 8424}
    args.state.parent.mkdir(parents=True, exist_ok=True)

    def persist():
        args.state.write_text(json.dumps(state, indent=2) + "\n")

    def one(label, count, exports, parents=(), kind="afterok", audit_job=False):
        namespace = SimpleNamespace(
            ssh_config=args.ssh_config,
            remote=args.remote,
            snapshot=args.snapshot,
            workdir=args.workdir,
            environment=args.environment,
            batch_script=args.remote_methods + "/audit.sbatch"
            if audit_job
            else "scripts/qualification/arithmetic_combinations/run.sbatch",
            phase=label,
            count=count,
            concurrency=1 if audit_job else 12,
            after=":".join(parents) or None,
            dependency_kind=kind,
            job_time="00:20:00"
            if audit_job
            else ("00:15:00" if "timing" in label else "00:35:00"),
            exports=exports,
            receipt=args.state.parent / (label + "-submission.json"),
        )
        return submit(namespace)

    def worker(phase, rep, chunk, parents=()):
        return one(
            f"{phase}-{rep}-{chunk}",
            702,
            {
                "PRIMO_COMB_CAMPAIGN": args.remote_configs + f"/campaign-{chunk}.json",
                "PRIMO_COMB_PHASE": phase,
                "PRIMO_COMB_REPETITION": str(rep),
            },
            parents,
        )

    def barrier(phase, parents):
        return one(
            phase + "-audit",
            1,
            {
                "PRIMO_EUF_METHODS": args.remote_methods,
                "PRIMO_EUF_AUDIT_PHASE": phase,
                "PRIMO_EUF_CONFIGS": args.remote_configs,
                "PRIMO_EUF_ROWS": args.results + "/raw",
                "PRIMO_EUF_AUDIT_OUT": args.results + "/" + phase + "-audit",
            },
            parents,
            kind="afterany",
            audit_job=True,
        )

    persist()
    try:
        for rep in range(3):
            for chunk in range(2):
                state["timing"].append(worker("timing", rep, chunk))
                persist()
        state["timing_audit"] = barrier("timing", state["timing"])
        persist()
        for rep in range(3):
            for chunk in range(2):
                state["models"].append(
                    worker("models", rep, chunk, [state["timing_audit"]])
                )
                persist()
        state["final_audit"] = barrier("final", state["models"])
        state["status"] = "submitted; all models wait for global timing audit"
        persist()
    except BaseException:
        jobs = (
            state["timing"]
            + state["models"]
            + [state[k] for k in ("timing_audit", "final_audit") if k in state]
        )
        if jobs:
            subprocess.run([*ssh, shlex.join(["scancel", *jobs])], check=False)
        state["status"] = "submission failed; created jobs cancelled"
        persist()
        raise
    print(json.dumps(state, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("configs", "ssh-config", "snapshot", "state"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in (
        "remote",
        "workdir",
        "environment",
        "remote-configs",
        "remote-methods",
        "results",
    ):
        parser.add_argument("--" + name, required=True)
    launch(parser.parse_args())


if __name__ == "__main__":
    main()
