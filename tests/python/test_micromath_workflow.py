#!/usr/bin/env python3
"""Workflow regressions; external solver/checker calls are mocked."""

from __future__ import annotations

import contextlib
import csv
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import weakref
from concurrent.futures import Future
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SCRIPTS = Path(__file__).parents[2] / "scripts"
TIME_COMMAND = Path(os.environ.get("MICROMATH_TIME", "/usr/bin/time"))


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STRESS = load("micromath_stress")
SELECTOR = load("select_micromath_fast_cases")
with mock.patch.dict(sys.modules, {"micromath_stress": STRESS}):
    REPLAY = load("micromath_replay")


class ImmediatePool:
    def __init__(self, **kwargs):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def submit(self, function, *args):
        future = Future()
        future.set_result(function(*args))
        return future


class MicromathWorkflowTests(unittest.TestCase):
    @unittest.skipUnless(
        shutil.which("prlimit") and shutil.which("timeout") and TIME_COMMAND.exists(),
        "GNU pipeline tools required",
    )
    def test_pipe_replay_requires_both_successes_and_preserves_saved_inputs(self):
        for producer_exit, consumer_exit, verdict in [
            (0, 0, "challenge proven"),
            (7, 0, "challenge proven"),
            (0, 9, "challenge proven"),
            (0, 0, "Inconsistency proven!\nchallenge proven"),
        ]:
            with (
                self.subTest(producer_exit=producer_exit, verdict=verdict),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                checker = root / "checker"
                (checker / "smt").mkdir(parents=True)
                producer = checker / "smt/prove-lrat"
                producer.write_text(
                    f"#!{sys.executable}\nimport sys,resource\n"
                    "assert sys.argv[-2:] == ['-', '--raw']\n"
                    "resource.setrlimit(resource.RLIMIT_FSIZE, (1024,1024))\n"
                    "for _ in range(64): sys.stdout.buffer.write(b'x'*65536)\n"
                    f"sys.stdout.buffer.flush()\nsys.exit({producer_exit})\n"
                )
                consumer = checker / "check-solution"
                consumer.write_text(
                    f"#!{sys.executable}\nimport sys\n"
                    "assert sys.argv[-1] == '-'\nn=0\n"
                    "while chunk:=sys.stdin.buffer.read(65536): n+=len(chunk)\n"
                    f"assert n==4194304\nprint({verdict!r})\n"
                    f"sys.exit({consumer_exit})\n"
                )
                producer.chmod(0o755)
                consumer.chmod(0o755)
                source = root / "inputs/case"
                source.mkdir(parents=True)
                for name in (
                    "input.primo",
                    "challenge.umd",
                    "problem.cnf",
                    "problem.lrat",
                ):
                    (source / name).write_text("data")
                original = {p.name: p.read_bytes() for p in source.iterdir()}
                args = SimpleNamespace(
                    inputs=source.parent,
                    out=root / "out",
                    reuse_lrat=True,
                    micromath=checker,
                    cadical=root / "cadical",
                    pipe_proof=True,
                    keep_success_artifacts="all",
                )
                real_invoke = REPLAY.invoke

                def invoke(command, cwd, log, timeout, real_invoke=real_invoke):
                    if log.name == "conflicts":
                        log.with_suffix(".out").write_text("")
                        log.with_suffix(".err").write_text("")
                        return {
                            "state": "ok",
                            "stdout": str(log.with_suffix(".out")),
                            "stderr": str(log.with_suffix(".err")),
                            "exit_code": 0,
                        }
                    return real_invoke(command, cwd, log, min(timeout, 5))

                with (
                    mock.patch.object(REPLAY, "invoke", invoke),
                    mock.patch.object(REPLAY.resource, "setrlimit"),
                    mock.patch.object(REPLAY, "trace_info", return_value={}),
                ):
                    row = REPLAY.replay(args, source / "input.primo")
                valid = (
                    producer_exit == consumer_exit == 0
                    and verdict == "challenge proven"
                )
                self.assertEqual(row["status"] == "kernel_proven", valid)
                self.assertEqual(row["proof_transport"], "pipe")
                self.assertEqual(
                    row["stages"]["proof_pipe"]["process_exit_codes"][0], producer_exit
                )
                self.assertEqual(
                    row["stages"]["proof_pipe"]["process_exit_codes"][1], consumer_exit
                )
                self.assertFalse(list(args.out.rglob("*.ump")))
                self.assertEqual(
                    {p.name: p.read_bytes() for p in source.iterdir()}, original
                )

    @unittest.skipUnless(
        shutil.which("prlimit") and shutil.which("timeout") and TIME_COMMAND.exists(),
        "GNU pipeline tools required",
    )
    def test_pipe_timeout_terminates_the_pipeline(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "smt").mkdir()
            for target in (root / "smt/prove-lrat", root / "check-solution"):
                target.write_text(f"#!{sys.executable}\nimport time\ntime.sleep(30)\n")
                target.chmod(0o755)
            result = subprocess.run(
                [
                    "bash",
                    str(SCRIPTS / "check_micromath_pipe.sh"),
                    str(root),
                    "challenge",
                    "input",
                    "lrat",
                    str(root / "logs"),
                    "0.2",
                ],
                capture_output=True,
                timeout=5,
                check=False,
            )
            self.assertEqual(result.returncode, 124)

    def test_raw_replay_preserves_sources_and_discards_only_successful_outputs(self):
        for confirmed in (True, False):
            with (
                self.subTest(confirmed=confirmed),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                source = root / "inputs" / "case"
                source.mkdir(parents=True)
                for name in (
                    "input.primo",
                    "challenge.umd",
                    "problem.cnf",
                    "problem.lrat",
                ):
                    (source / name).write_text(
                        "p cnf 0 1\n" if name == "problem.cnf" else "0\n"
                    )
                before = {p.name: p.read_bytes() for p in source.iterdir()}
                args = SimpleNamespace(
                    inputs=source.parent,
                    out=root / "out",
                    reuse_lrat=True,
                    micromath=root,
                    cadical=root / "cadical",
                    keep_success_artifacts="certificates",
                    pipe_proof=False,
                )
                commands = []

                def invoke(
                    command, cwd, log, timeout, commands=commands, confirmed=confirmed
                ):
                    commands.append(command)
                    if log.name == "proof":
                        self.assertEqual(command[-1], "--raw")
                        Path(command[-2]).write_bytes(b"proof")
                    output = "challenge proven\n" if confirmed else "unconfirmed\n"
                    log.with_suffix(".out").write_text(output)
                    log.with_suffix(".err").write_text("")
                    return {
                        "state": "ok",
                        "stdout": str(log.with_suffix(".out")),
                        "stderr": str(log.with_suffix(".err")),
                    }

                with (
                    mock.patch.object(REPLAY, "invoke", invoke),
                    mock.patch.object(REPLAY.resource, "setrlimit") as limits,
                    mock.patch.object(REPLAY, "trace_info", return_value={}),
                ):
                    row = REPLAY.replay(args, source / "input.primo")
                self.assertEqual(
                    [Path(c[0]).name for c in commands],
                    ["check-conflicts", "prove-lrat", "check-solution"],
                )
                limits.assert_any_call(REPLAY.resource.RLIMIT_FSIZE, (1024**3, 1024**3))
                self.assertEqual(
                    {p.name: p.read_bytes() for p in source.iterdir()}, before
                )
                self.assertEqual(
                    (args.out / "cases/case/solution.ump").exists(), not confirmed
                )
                self.assertEqual(
                    row["removed_success_artifacts"],
                    ["solution.ump"] if confirmed else [],
                )

    def test_stress_uses_raw_proof_export_and_one_gib_file_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "input.smt2").write_text("source")
            args = SimpleNamespace(
                out=root / "out",
                corpus=root,
                micromath=root,
                reference="reference",
                reference_timeout=1,
                modes=["core"],
                primo="primo",
                mode_budget=60,
                admission_timeout=5,
                export_timeout=30,
                cadical="cadical",
                keep_success_artifacts="all",
            )

            def invoke(command, cwd, log, timeout):
                if log.name == "proof":
                    self.assertEqual(command[-1], "--raw")
                log.with_suffix(".out").write_text(
                    "challenge proven\n" if log.name == "kernel" else "unsat\n"
                )
                log.with_suffix(".err").write_text("")
                return {
                    "state": "ok",
                    "stdout": str(log.with_suffix(".out")),
                    "stderr": str(log.with_suffix(".err")),
                    "exit_code": 0,
                }

            case = {"file": "input.smt2", "sha256": STRESS.digest(root / "input.smt2")}
            with (
                mock.patch.object(STRESS, "invoke", invoke),
                mock.patch.object(STRESS.resource, "setrlimit") as limits,
            ):
                row = STRESS.run_case(args, 0, case)
            limits.assert_any_call(STRESS.resource.RLIMIT_FSIZE, (1024**3, 1024**3))
            self.assertEqual(row["profiles"][0]["status"], "kernel_proven")

    def test_uncapped_selection_and_boolean_signature_filter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rows = []
            for index in range(9):
                source = b"(declare-fun f (U) U)\n" + str(index).encode()
                if index == 8:
                    source = b"(declare-fun f (Bool) U)"
                path = root / f"{index}.smt2"
                path.write_bytes(source)
                rows.append(
                    {
                        "file": path.name,
                        "expected": "unsat",
                        "family": "same",
                        "sha256": hashlib.sha256(source).hexdigest(),
                        "historical_seconds": 5,
                    }
                )
            (root / "records.json").write_text(json.dumps(rows))
            argv = [
                "selector",
                "--records",
                str(root / "records.json"),
                "--corpus",
                str(root),
                "--out",
                str(root / "manifest.json"),
                "--all-known-unsat",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.dict(os.environ, {"SLURM_JOB_ID": "test"}),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                SELECTOR.main()
            result = json.loads((root / "manifest.json").read_text())
            self.assertEqual(result["selected_cases"], 8)
            self.assertIsNone(result["historical_seconds_max"])
            self.assertIsNone(result["max_per_family"])
            self.assertEqual(
                result["rejected_before_selection"], {"boolean_function_signature": 1}
            )

    def test_history_accepts_slower_unsat_but_not_failed_or_sat_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / "shard-0"
            shard.mkdir()
            (shard / "provenance.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "inputs_unchanged": True,
                        "solver": {"version": "git: abc123\n", "sha256": "binary"},
                        "solver_args": ["--quiet"],
                        "inputs": {"slow": "hash"},
                    }
                )
            )
            with (shard / "answers.csv").open("w") as stream:
                writer = csv.DictWriter(
                    stream,
                    fieldnames=[
                        "status",
                        "answer",
                        "wrong",
                        "exit_code",
                        "seconds",
                        "file",
                        "family",
                    ],
                )
                writer.writeheader()
                for state, answer, code in [
                    ("ok", "unsat", "0"),
                    ("ok", "sat", "0"),
                    ("ok", "unsat", "1"),
                ]:
                    writer.writerow(
                        {
                            "status": state,
                            "answer": answer,
                            "wrong": "false",
                            "exit_code": code,
                            "seconds": "5",
                            "file": "slow",
                            "family": "one",
                        }
                    )
            self.assertEqual(len(SELECTOR.read_history(root, "abc123", None)), 1)
            with self.assertRaisesRegex(ValueError, "no completed"):
                SELECTOR.read_history(root, "abc123")

    def test_core_only_has_no_baseline_and_preserves_timeout_statistics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "input.smt2").write_text("source")
            args = SimpleNamespace(
                out=root / "out",
                corpus=root,
                micromath=root,
                reference="reference",
                reference_timeout=1,
                modes=["core"],
                primo="primo",
                mode_budget=60,
                admission_timeout=5,
                export_timeout=30,
                cadical="cadical",
                keep_success_artifacts="certificates",
            )
            calls = []

            def invoke(command, cwd, log, timeout):
                calls.append(command)
                stdout, stderr = log.with_suffix(".out"), log.with_suffix(".err")
                stdout.write_text("unsat\n")
                stderr.write_text(":export.main-unsat 1\n:export.reduction.us 999\n")
                return {
                    "state": "timeout" if command[0] == "primo" else "ok",
                    "exit_code": None if command[0] == "primo" else 0,
                    "stdout": str(stdout),
                    "stderr": str(stderr),
                    "seconds": 1,
                }

            case = {"file": "input.smt2", "sha256": STRESS.digest(root / "input.smt2")}
            with (
                mock.patch.object(STRESS, "invoke", invoke),
                mock.patch.object(STRESS.resource, "setrlimit"),
            ):
                result = STRESS.run_case(args, 3, case)
            self.assertEqual(len(calls), 2)
            self.assertIn("--export-cnf-reduction=core", calls[1])
            profile = result["profiles"][0]
            self.assertEqual(profile["status"], "export_timeout")
            self.assertEqual(profile["statistics"][":export.main-unsat"], "1")
            self.assertFalse((root / "out/cases/003/none").exists())

    def test_compact_shards_checkpoint_disjoint_cases(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = [
                {
                    "file": f"{i}.smt2",
                    "family": "one",
                    "expected": "unsat",
                    "historical_seconds": 3,
                    "sha256": "hash",
                }
                for i in range(5)
            ]
            indices = []
            references = {}
            processed = []
            original_write = STRESS.write_json

            class Row(dict):
                pass

            def checkpoint(path, value):
                original_write(path, value)
                if path.name == "result.json":
                    processed.append(value["index"])
                if path.name == "summary.json":
                    for previous in processed[:-1]:
                        self.assertIsNone(
                            references[previous](), "completed row retained"
                        )

            def invoke(command, cwd, log, timeout):
                log.with_suffix(".out").write_text("git: test\n")
                log.with_suffix(".err").write_text("")
                return {"state": "ok", "stdout": str(log.with_suffix(".out"))}

            def run_case(args, index, case):
                row = Row(
                    {
                        "index": index,
                        "case": case,
                        "profiles": [{"mode": "core", "status": "kernel_proven"}],
                    }
                )
                references[index] = weakref.ref(row)
                return row

            for shard in range(2):
                out = root / str(shard)
                processed.clear()
                argv = [
                    "stress",
                    "--corpus",
                    str(root),
                    "--micromath",
                    str(root),
                    "--out",
                    str(out),
                    "--primo",
                    "primo",
                    "--reference",
                    "reference",
                    "--cadical",
                    "cadical",
                    "--expected-git",
                    "test",
                    "--manifest",
                    "unused",
                    "--modes",
                    "core",
                    "--shard-count",
                    "2",
                    "--shard-index",
                    str(shard),
                    "--compact-summary",
                ]
                with (
                    mock.patch.object(sys, "argv", argv),
                    mock.patch.dict(os.environ, {"SLURM_JOB_ID": "test"}),
                    mock.patch.object(STRESS, "select", return_value={"cases": cases}),
                    mock.patch.object(STRESS, "invoke", invoke),
                    mock.patch.object(STRESS, "digest", return_value="hash"),
                    mock.patch.object(STRESS, "run_case", run_case),
                    mock.patch.object(STRESS, "write_json", checkpoint),
                    mock.patch.object(STRESS, "ProcessPoolExecutor", ImmediatePool),
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    STRESS.main()
                summary = json.loads((out / "summary.json").read_text())
                self.assertNotIn("rows", summary)
                self.assertTrue(summary["all_profiles_proven"])
                rows = [
                    json.loads(line)
                    for line in (out / "rows.jsonl").read_text().splitlines()
                ]
                self.assertEqual(len(rows), summary["cases_selected"])
                for row in rows:
                    self.assertEqual(row["index"] % 2, shard)
                    self.assertEqual(
                        json.loads(
                            (
                                out / "cases" / f"{row['index']:03d}" / "result.json"
                            ).read_text()
                        ),
                        row,
                    )
                indices.extend(row["index"] for row in rows)
            self.assertEqual(sorted(indices), list(range(5)))


if __name__ == "__main__":
    unittest.main()
