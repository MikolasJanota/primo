"""Full-cohort admission and worker cancellation use no local solver execution."""

import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import select_arithmetic_certificate_sweep as selector


class FullSelectionTests(unittest.TestCase):
    def test_full_mode_removes_all_caps_and_bounds_shards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            records = []
            for name, seconds, padding in (
                ("control", 0.1, 0),
                ("slow", 55, 0),
                ("large", 2, 8388609),
            ):
                source = root / f"{name}.smt2"
                source.write_text(
                    "(set-logic QF_LIA)(declare-fun x () Int)(assert false)(check-sat)\n;"
                    + name
                    + "\n;"
                    + "x" * padding
                )
                records.append(
                    {
                        "file": source.name,
                        "family": "one-family",
                        "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                        "historical_seconds": seconds,
                        "historical_solver_args": ["--quiet"],
                    }
                )
            baseline = root / "baseline.json"
            baseline.write_text(json.dumps({"cases": records[:1]}))
            output = root / "selection"
            args = [
                "selector",
                "--history",
                str(root),
                "--corpus",
                str(root),
                "--baseline-selection",
                str(baseline),
                "--out",
                str(output),
                "--historical-git",
                "abc",
                "--logic",
                "QF_LIA",
                "--count",
                "1",
                "--max-per-family",
                "1",
                "--all-known-unsat",
                "--cases-per-shard",
                "2",
            ]
            with (
                patch.object(sys, "argv", args),
                patch.dict(os.environ, {"SLURM_JOB_ID": "unit"}),
                patch.object(selector, "read_history", return_value=records) as history,
                contextlib.redirect_stdout(io.StringIO()),
            ):
                selector.main()
            history.assert_called_once_with(root, "abc", None)
            result = json.loads((output / "selection.json").read_text())
            self.assertEqual(len(result["cases"]), 3)
            self.assertEqual(result["baseline_controls"], 1)
            self.assertIsNone(result["historical_seconds_max"])
            self.assertTrue(result["all_known_unsat"])
            self.assertEqual(result["shards"], 2)
            shards = [
                json.loads((output / f"shard-{i}.json").read_text()) for i in range(2)
            ]
            self.assertEqual(sorted(len(s["cases"]) for s in shards), [1, 2])
            self.assertTrue(all(s["logic"] == "QF_LIA" for s in shards))


class FullWorkerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.workflow = self.root / "workflow"
        self.workflow.mkdir()
        script = (
            Path(__file__).resolve().parents[2]
            / "scripts/slurm_full_arithmetic_certificates.sbatch"
        )
        self.script = self.workflow / script.name
        shutil.copyfile(script, self.script)
        self.shared = self.workflow / "slurm_arithmetic_certificate_stress.sbatch"
        self.shared.write_text(
            'printf "%s %s %s %s\\n" "$ARITHMETIC_REFERENCE_SECONDS" "$ARITHMETIC_EXPORT_SECONDS" "$ARITHMETIC_CHALLENGE_SECONDS" "$ARITHMETIC_PROOF_SECONDS" > "$PRIMO_WORKDIR/profile.txt"\n'
        )
        checker = self.root / "checker"
        checker.mkdir()
        (checker / "tool").write_text("qualified tool\n")
        build = self.root / "build"
        build.mkdir()
        (build / "binaries.sha256").write_text(
            hashlib.sha256((checker / "tool").read_bytes()).hexdigest() + "  tool\n"
        )
        for logic in ("QF_LRA", "QF_LIA"):
            manifest = self.root / "selection" / logic
            manifest.mkdir(parents=True)
            blob = json.dumps({"logic": logic, "cases": []}).encode()
            (manifest / "shard-0.json").write_bytes(blob)
            (manifest / "selection.json").write_text("{}")
            (manifest / "manifest-hashes.json").write_text(
                json.dumps(
                    {
                        "shard-0.json": {
                            "sha256": hashlib.sha256(blob).hexdigest(),
                            "cases": 0,
                        }
                    }
                )
            )
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        cancel = bin_dir / "scancel"
        cancel.write_text(
            '#!/usr/bin/env bash\nprintf "%s\\n" "$*" >> "$MOCK_CANCEL_LOG"\n'
        )
        cancel.chmod(0o755)
        self.env = {
            **os.environ,
            "PRIMO_WORKDIR": str(self.root),
            "ARITHMETIC_WORKFLOW_DIR": str(self.workflow),
            "ARITHMETIC_HISTORY_ROOT": str(self.root),
            "FULL_ARITHMETIC_STAGE": "run",
            "FULL_ARITHMETIC_SELECTION": str(self.root / "selection"),
            "FULL_LRA_SHARDS": "1",
            "FULL_LIA_SHARDS": "1",
            "CHECKER_SWEEP_BUILD_REPORT": str(build),
            "MICROMATH_LRA_DIR": str(checker),
            "MICROMATH_LIA_DIR": str(checker),
            "SLURM_ARRAY_JOB_ID": "12345",
            "SLURM_ARRAY_TASK_ID": "0",
            "PATH": str(bin_dir) + os.pathsep + os.environ["PATH"],
            "MOCK_CANCEL_LOG": str(self.root / "cancel.txt"),
        }
        self.stop = self.root / "results/full-arithmetic-certificates-12345/STOP"

    def run_worker(self, task="0"):
        return subprocess.run(
            ["bash", str(self.script)],
            env={**self.env, "SLURM_ARRAY_TASK_ID": task},
            capture_output=True,
            check=False,
            text=True,
            timeout=5,
        )

    def test_success_uses_full_profile_without_cancelling(self):
        result = self.run_worker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "profile.txt").read_text(), "60 120 60 120\n")
        self.assertFalse(self.stop.exists())
        self.assertFalse((self.root / "cancel.txt").exists())

    def test_first_failure_cancels_only_own_array_and_remains_recorded(self):
        self.shared.write_text("exit 2\n")
        result = self.run_worker()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.stop.read_text(), "task=0 exit=2\n")
        later = self.run_worker("1")
        self.assertEqual(later.returncode, 3)
        self.assertEqual(self.stop.read_text(), "task=0 exit=2\n")
        self.assertEqual((self.root / "cancel.txt").read_text(), "12345\n")

    def test_failed_binary_check_stops_before_worker_and_cancels(self):
        (self.root / "checker/tool").write_text("changed\n")
        result = self.run_worker()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.stop.exists())
        self.assertEqual((self.root / "cancel.txt").read_text(), "12345\n")
        self.assertFalse((self.root / "profile.txt").exists())


if __name__ == "__main__":
    unittest.main()
