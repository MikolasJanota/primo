"""Feedback archives must exclude unvalidated or changed replay cases."""

import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from audit_arithmetic_checker_failures import exact_header_copy, package_feedback


class FeedbackPackageTests(unittest.TestCase):
    def test_mismatched_copy_is_rejected_without_mutating_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native, copy = root / "native", root / "adapted"
            original = b"(primo-conflicts\n  (version 3)\n  (logic QF_LIA)\nBODY)"
            native.write_bytes(original)
            copy.write_bytes(original.replace(b"version 3", b"version 2") + b"changed")
            with self.assertRaises(ValueError):
                exact_header_copy(native, copy)
            self.assertEqual(native.read_bytes(), original)
            self.assertTrue(copy.read_bytes().endswith(b"changed"))
            self.assertFalse((root / "expected-v2.sexp").exists())

    def test_no_archive_for_incomplete_validation_or_changed_replay(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = {
                "rows": [
                    {"status": "validation_incomplete"},
                    {"status": "replay_changed"},
                ]
            }
            result = package_feedback(
                SimpleNamespace(out=root), report, {}, Path("scripts")
            )
            self.assertNotIn("archive", result)
            self.assertFalse((root / "micromath-lia-feedback.tar.gz").exists())

    def test_only_validated_case_and_exact_payload_are_packaged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out, saved = root / "out", root / "saved"
            (out / "014").mkdir(parents=True)
            (saved / "014").mkdir(parents=True)
            payload = b"exact payload\n"
            (out / "014/native.sexp").write_bytes(payload)
            (saved / "014/challenge.err").write_text("original failure")
            row = {
                "index": 14,
                "status": "checker_error_reproduced",
                "previous_status": "proof_error",
                "case": {"file": "family/problem.smt2"},
                "files": {"native.sexp": {}},
            }
            report = {
                "rows": [row, {"status": "validation_incomplete"}],
                "micromath_git": "pinned",
                "job_id": "test",
                "yices_version": "Yices test",
            }
            args = SimpleNamespace(out=out, previous_run=saved)
            result = package_feedback(
                args, report, {"job_id": "original"}, Path("scripts")
            )
            with tarfile.open(out / result["archive"]) as archive:
                manifest = json.load(archive.extractfile("manifest.json"))
                self.assertEqual(len(manifest["cases"]), 1)
                name = manifest["cases"][0]["directory"]
                self.assertEqual(
                    archive.extractfile(name + "/native.sexp").read(), payload
                )
                self.assertEqual(archive.getmember("replay.sh").mode, 0o755)
                self.assertFalse(
                    any(member.name.endswith(".umd") for member in archive)
                )


if __name__ == "__main__":
    unittest.main()
