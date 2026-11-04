"""Check attribution and acceptance boundaries of the integrality result analysis."""

import csv
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.qualification.lia_integrality import analyze


def make_parent(directory: Path, status: str = "ok") -> dict:
    parent = directory / "shard-0"
    parent.mkdir(parents=True)
    row = {
        "file": "a.smt2",
        "family": "A",
        "status": status,
        "answer": "sat" if status == "ok" else "unsupported",
        "expected": "sat",
        "wrong": "false",
        "seconds": "1",
    }
    answers = parent / "answers.csv"
    with answers.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(row))
        writer.writeheader()
        writer.writerow(row)
    provenance = parent / "provenance.json"
    provenance.write_text(
        json.dumps(
            {
                "status": "complete",
                "inputs_unchanged": True,
                "inputs": {"a.smt2": "a" * 64},
                "solver": {"sha256": "b" * 64},
            }
        )
    )
    output = parent / "model-validation"
    output.mkdir()
    output.joinpath("replay-provenance.json").write_text(
        json.dumps(
            {
                "parent_provenance_sha256": hashlib.sha256(
                    provenance.read_bytes()
                ).hexdigest(),
                "parent_answers_sha256": hashlib.sha256(
                    answers.read_bytes()
                ).hexdigest(),
            }
        )
    )
    selected = int(status == "ok")
    output.joinpath("coverage.json").write_text(
        json.dumps(
            {
                "status": "complete",
                "expected_sat": selected,
                "completed": selected,
                "dolmen_pass": selected,
                "outcomes": {"pass": selected} if selected else {},
            }
        )
    )
    return {"a.smt2": row}


class LiaIntegralityAnalysisTests(unittest.TestCase):
    def test_four_profiles_use_selected_baseline_and_all_model_gates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for arm in ("reference", "off", "m12", "m124", "all"):
                make_parent(root / arm)
            argv = [
                "analyze", "--root", str(root), "--reference-root", str(root / "reference"),
                "--expected-files", "1", "--arms", "off", "m12", "m124", "all",
                "--baseline", "off", "--out", str(root / "summary.json"),
            ]
            with patch.object(sys, "argv", argv):
                self.assertEqual(analyze.main(), 0)
            result = json.loads((root / "summary.json").read_text())
            self.assertEqual(result["baseline"], "off")
            self.assertEqual(set(result["comparisons"]), {"m12", "m124", "all"})
            (root / "m12/shard-0/model-validation/coverage.json").unlink()
            with patch.object(sys, "argv", argv):
                self.assertEqual(analyze.main(), 1)

    def test_duplicate_profiles_are_rejected(self) -> None:
        argv = ["analyze", "--root", ".", "--reference-root", ".", "--out", "unused",
                "--arms", "all", "m124", "all"]
        with patch.object(sys, "argv", argv), self.assertRaises(SystemExit) as error:
            analyze.main()
        self.assertEqual(error.exception.code, 2)

    def test_model_coverage_rejects_changed_parent_files(self) -> None:
        for filename in ("answers.csv", "provenance.json"):
            with (
                self.subTest(filename=filename),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                rows = make_parent(root)
                self.assertTrue(analyze.models(root, rows)["complete"])
                parent = root / "shard-0" / filename
                # Changing only whitespace preserves the parsed rows and SAT count.
                # The old model coverage must still be rejected for these new bytes.
                parent.write_bytes(parent.read_bytes() + b"\n")
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    analyze.models(root, rows)

    def test_model_coverage_rejects_wrong_per_shard_sat_selection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rows = make_parent(root)
            coverage = root / "shard-0/model-validation/coverage.json"
            data = json.loads(coverage.read_text())
            data["expected_sat"] = 2
            coverage.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "SAT selection differs"):
                analyze.models(root, rows)

    def test_unsupported_rows_prevent_success_even_without_sat_models(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            make_parent(root / "reference")
            make_parent(root / "all", "unsupported")
            make_parent(root / "m124", "unsupported")
            output = root / "summary.json"
            argv = [
                "analyze",
                "--root",
                str(root),
                "--reference-root",
                str(root / "reference"),
                "--expected-files",
                "1",
                "--out",
                str(output),
            ]
            with patch.object(sys, "argv", argv):
                self.assertEqual(analyze.main(), 1)
            report = json.loads(output.read_text())
            self.assertTrue(report["models"]["all"]["complete"])
            self.assertEqual(report["runs"]["all"]["unsupported"], 1)
            self.assertEqual(report["runs"]["all"]["error"], 0)


class IntegralityRunnerTests(unittest.TestCase):
    def test_rotated_four_profile_arguments_without_running_solver(self) -> None:
        runner = Path("scripts/qualification/lia_integrality/paired_replay.sbatch").resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "contrib/local/bin"
            tools.mkdir(parents=True)
            capture = root / "commands.jsonl"
            fake_python = tools / "python3"
            fake_python.write_text(
                f"#!{sys.executable}\nimport json, os, sys\n"
                "with open(os.environ['CAPTURE_FILE'], 'a') as handle:\n"
                "    handle.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            )
            fake_python.chmod(0o755)
            solver = root / "version-only"
            solver.write_text("#!/bin/sh\ntest \"$1\" = --version\n")
            solver.chmod(0o755)
            environment = dict(os.environ, PRIMO_WORKDIR=str(root), PRIMO_COMPARE_ROOT=str(root),
                               PRIMO_COMPARE_BINARY=str(solver), PRIMO_COMPARE_OUT_ROOT=str(root / "out"),
                               PRIMO_PHASE="benchmark", PRIMO_PAIRED_SHARDS="2", PRIMO_PAIRED_CHUNKS="1",
                               PRIMO_PAIRED_ARMS="off m12 m124 all", PRIMO_EXPECTED_GIT="test",
                               PRIMO_CAMPAIGN_ID="test", SLURM_ARRAY_TASK_ID="1", CAPTURE_FILE=str(capture))
            subprocess.run(["bash", str(runner)], env=environment, check=True, capture_output=True)
            commands = [json.loads(line) for line in capture.read_text().splitlines()]
            self.assertEqual([command[command.index("--run-id") + 1] for command in commands],
                             ["test-m12", "test-m124", "test-all", "test-off"])
            for command in commands:
                arm = command[command.index("--run-id") + 1].removeprefix("test-")
                flags = {arg.removeprefix("--solver-arg=") for arg in command if arg.startswith("--solver-arg=")}
                for option, enabled in [("round-integer-bounds", arm != "off"),
                                        ("primitive-int-rows", arm != "off"),
                                        ("patch-integral-moves", arm in {"m124", "all"}),
                                        ("integer-slacks", arm == "all")]:
                    self.assertIn(f"--lia-{option}" if enabled else f"--no-lia-{option}", flags)
                    self.assertNotIn(f"--no-lia-{option}" if enabled else f"--lia-{option}", flags)


if __name__ == "__main__":
    unittest.main()
