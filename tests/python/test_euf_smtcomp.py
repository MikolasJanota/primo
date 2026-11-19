#!/usr/bin/env python3
"""Check the 120s censoring and timing-to-model acceptance boundary without solvers."""

import csv
import gzip
import json
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.qualification.bound_propagation.analyze import sha
from scripts.qualification.euf_smtcomp import launch as launch_module
from scripts.qualification.euf_smtcomp.audit import ROOT, audit, describe
from scripts.qualification.euf_smtcomp.references import export as reference_export
from scripts.qualification.euf_smtcomp.symmetry_audit import decisive, sat_models


class EufSmtcompTests(unittest.TestCase):
    def test_native_reuse_requires_every_clean_completed_allocation(self):
        jobs = [str(n) for n in range(10, 16)]
        accounting = "".join(f"{j}_{s}|COMPLETED|0:0\n" for j in jobs for s in range(3))
        launch_module.completed_native(accounting, jobs, 3)
        for bad in (
            accounting.split("\n", 1)[1],
            accounting.replace("COMPLETED", "RUNNING", 1),
            accounting.replace("0:0", "1:0", 1),
        ):
            with self.assertRaises(AssertionError):
                launch_module.completed_native(bad, jobs, 3)

    def test_model_reuse_requires_complete_unstarted_allocation_coverage(self):
        jobs = [str(n) for n in range(20, 26)]
        accounting = "".join(f"{j}_[0-1,2%12]|CANCELLED|0:0|0\n" for j in jobs)
        launch_module.unstarted_models(accounting, jobs, 3)
        expanded = "".join(f"{j}_{i}|CANCELLED|0:0|0\n" for j in jobs for i in range(3))
        launch_module.unstarted_models(expanded, jobs, 3)
        for bad in (
            accounting.replace("[0-1,2%12]", "0", 1),
            accounting.split("\n", 1)[1],
            accounting.replace("|0\n", "|1\n", 1),
            accounting.replace("CANCELLED", "RUNNING", 1),
            accounting + accounting.splitlines()[0] + "\n",
            accounting.replace("[0-1,2%12]", "[0-3]", 1),
            accounting.replace("[0-1,2%12]", "[2-0]", 1),
        ):
            with self.subTest(accounting=bad), self.assertRaises(AssertionError):
                launch_module.unstarted_models(bad, jobs, 3)

    def test_native_only_restart_keeps_global_model_barrier_and_saved_timings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            configs = root / "configs"
            configs.mkdir()
            (configs / "protocol.json").write_text(
                json.dumps({"cases": 1404, "native_rows": 8424, "chunk_size": 702})
            )
            jobs = [str(n) for n in range(10, 16)]
            old = root / "previous.json"
            old.write_text(
                json.dumps(
                    {
                        "native_rows": 8424,
                        "timing": jobs,
                        "models": [str(n) for n in range(20, 26)],
                    }
                )
            )
            native = "".join(
                f"{j}_{s}|COMPLETED|0:0\n" for j in jobs for s in range(702)
            )
            models = "".join(f"{j}_[0-701]|CANCELLED|0:0|0\n" for j in range(20, 26))
            args = types.SimpleNamespace(
                configs=configs,
                state=root / "restart/jobs.json",
                reuse_completed_native=old,
                ssh_config=Path("/cfg"),
                remote="remote",
                snapshot=Path("/snapshot"),
                workdir="/work",
                environment="/env",
                remote_configs="/cards",
                remote_methods="/methods",
                results="/result",
                audit_results="/audits",
            )
            calls = []

            def submit(namespace):
                calls.append(namespace)
                return str(1000 + len(calls))

            with (
                patch.object(
                    launch_module.subprocess,
                    "check_output",
                    side_effect=[native, models],
                ),
                patch.object(launch_module.subprocess, "run"),
                patch.object(launch_module, "submit", side_effect=submit),
            ):
                launch_module.launch(args)
            self.assertEqual(
                [x.phase for x in calls],
                [
                    "timing-audit",
                    *[f"models-{r}-{c}" for r in range(3) for c in range(2)],
                    "final-audit",
                ],
            )
            self.assertIsNone(calls[0].after)
            self.assertTrue(all(x.after == "1001" for x in calls[1:7]))
            self.assertEqual(calls[0].exports["PRIMO_EUF_ROWS"], "/result/raw")
            self.assertEqual(
                calls[0].exports["PRIMO_EUF_AUDIT_OUT"], "/audits/timing-audit"
            )
            self.assertEqual(
                calls[-1].after, ":".join(str(n) for n in range(1002, 1008))
            )
            self.assertEqual(json.loads(args.state.read_text())["timing"], jobs)
            batch = ROOT / "scripts/qualification/euf_smtcomp/audit.sbatch"
            self.assertIn(
                'python3.11 "${PRIMO_EUF_METHODS:?}/audit.py"', batch.read_text()
            )

    def test_focused_replay_requires_correct_answer_and_clean_process(self):
        decisive({"status": "ok", "exit_code": 0, "answer": "sat"}, "sat")
        for row in (
            {"status": "ok", "exit_code": 0, "answer": "unsat"},
            {"status": "ok", "exit_code": 1, "answer": "sat"},
            {"status": "timeout", "exit_code": 124, "answer": "sat"},
        ):
            with self.subTest(row=row), self.assertRaises(AssertionError):
                decisive(row, "sat")

    def test_focused_models_require_same_profile_and_complete_dolmen_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            row = {
                "file": "input.smt2",
                "answer": "sat",
                "outcome": "pass",
                "validator": "dolmen",
                "solver_exit": "0",
                "dolmen_exit": "0",
            }
            with (directory / "summary.csv").open("w") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(row))
                writer.writeheader()
                writer.writerow(row)
            meta = {
                "status": "complete",
                "completed": 1,
                "solver": "frozen/primo",
                "solver_args": ["--check-model"],
                "require_dolmen_pass": True,
                "expect_sat": True,
            }
            path = directory / "metadata.json"
            path.write_text(json.dumps(meta))
            sat_models(directory, {"input.smt2"}, "frozen/primo", ["--check-model"])
            with self.assertRaises(AssertionError):
                sat_models(directory, {"other.smt2"}, "frozen/primo", ["--check-model"])
            with self.assertRaises(AssertionError):
                sat_models(
                    directory,
                    {"input.smt2"},
                    "frozen/primo",
                    ["--no-uf-symmetry-breaking", "--check-model"],
                )
            meta["completed"] = 0
            path.write_text(json.dumps(meta))
            with self.assertRaises(AssertionError):
                sat_models(directory, {"input.smt2"}, "frozen/primo", ["--check-model"])

    def test_timeout_par2_and_missing_counters(self):
        rows = [
            {
                "status": "ok",
                "exit_code": 0,
                "answer": "sat",
                "seconds": 10,
                "peak_rss_kib": 10,
                "stats": {":euf.test": 2, ":euf.peak-test": 3},
            },
            {
                "status": "timeout",
                "exit_code": 124,
                "answer": "sat",
                "seconds": 120,
                "peak_rss_kib": 20,
                "stats": {":euf.test": 500, ":euf.peak-test": 500},
            },
        ]
        result = describe(rows)
        self.assertEqual(result["solved"], 1)
        self.assertEqual(result["mean_PAR2_seconds"], 125)
        self.assertEqual(result["successful_printed_counters"][":euf.test"]["value"], 2)
        self.assertEqual(
            result["successful_printed_counters"][":euf.peak-test"]["value"], 3
        )
        self.assertNotIn(":euf.absent", result["successful_printed_counters"])

    def test_censoring_does_not_hide_original_reference_disagreement(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            for name, value in {
                "selection": {
                    "cases": [
                        {
                            "logic": "QF_UF",
                            "file": "a",
                            "sha256": "x",
                            "expected": "sat",
                        }
                    ]
                },
                "experiment": {"config": {"timeout_seconds": 120, "memory_mib": 8192}},
                "provenance": {
                    "reference_reuse_sha256": "x",
                    "tools": {p: {} for p in ("z3", "cvc5", "yices", "opensmt")},
                },
            }.items():
                (base / (name + ".json")).write_text(json.dumps(value))
            row = {
                "logic": "QF_UF",
                "profile": "z3",
                "file": "a",
                "sha256": "x",
                "wrong": "false",
                "status": "timeout",
                "answer": "",
                "seconds": "120",
                "exit_code": "124",
                "timeout": "true",
                "original_status": "ok",
                "original_answer": "unsat",
                "original_exit_code": "0",
                "original_timeout": "false",
            }
            archive = base / "archive.csv.gz"
            with gzip.open(archive, "wt", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(row))
                writer.writeheader()
                writer.writerow(row)
            with self.assertRaises(AssertionError):
                reference_export(
                    types.SimpleNamespace(
                        selection=base / "selection.json",
                        experiment=base / "experiment.json",
                        provenance=base / "provenance.json",
                        archive=archive,
                        out=base / "out",
                    )
                )
            self.assertFalse((base / "out").exists())

    def test_complete_timings_do_not_qualify_missing_models(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            configs, raw = base / "configs", base / "raw"
            configs.mkdir()

            def write(path, value):
                path.write_text(json.dumps(value))

            case = {
                "logic": "QF_UF",
                "file": "a.smt2",
                "sha256": "1" * 64,
                "family": "a",
                "expected": "sat",
            }
            profiles = {
                p: {
                    "solver": "fake",
                    "git": "abc",
                    "sha256": "0" * 64,
                    "args": ["--quiet", "--stats"],
                }
                for p in ("BASE", "OFF")
            }
            write(configs / "selection.json", {"cases": [case]})
            runner = sha(ROOT / "scripts/qualification/lia_hybrid/run.py")
            protocol = {
                "selection_sha256": sha(configs / "selection.json"),
                "sources": {
                    p: {k: v for k, v in data.items() if k != "solver"}
                    for p, data in profiles.items()
                },
                "timeout_seconds": 120,
                "repetitions": 3,
                "address_space_bytes": 8589934592,
                "cases": 1,
                "native_rows": 6,
                "chunk_size": 1,
                "runner_sha256": runner,
            }
            write(configs / "protocol.json", protocol)
            write(configs / "routing.json", {"profiles": profiles, "roots": {}})
            for rep in range(3):
                config = {
                    "profiles": profiles,
                    "roots": {},
                    "cases": [dict(case, repetition=rep)],
                    "timeout": 120,
                    "address_space_bytes": 8589934592,
                }
                card = configs / f"chunk-0-{rep}.json"
                write(card, config)
                directory = raw / f"rep-{rep}" / "chunk-0" / "slot-0"
                directory.mkdir(parents=True)
                (directory / "timing-complete").touch()
                write(
                    directory / "provenance.json",
                    {
                        "config_sha256": sha(card),
                        "runner_sha256": runner,
                        "case": config["cases"][0],
                        "profiles": profiles,
                    },
                )
                (directory / "rows.jsonl").write_text(
                    "\n".join(
                        json.dumps(
                            {
                                "profile": p,
                                "file": case["file"],
                                "logic": case["logic"],
                                "args": data["args"],
                                "status": "ok",
                                "exit_code": 0,
                                "answer": "sat",
                                "seconds": 1.0,
                                "peak_rss_kib": 100,
                                "stats": {},
                            }
                        )
                        for p, data in profiles.items()
                    )
                )
            audit(
                types.SimpleNamespace(
                    configs=configs, rows=raw, out=base / "timing", phase="timing"
                )
            )
            with self.assertRaises(AssertionError):
                audit(
                    types.SimpleNamespace(
                        configs=configs, rows=raw, out=base / "final", phase="final"
                    )
                )
            self.assertFalse((base / "final").exists())


if __name__ == "__main__":
    unittest.main()
