"""Pure metadata/acceptance checks; these tests never run a solver or checker."""

import hashlib
import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from replay_checker_handoff import pipeline_status, unpack_case
from replay_remaining_checker_failures import assignment, checked_copy, profiles


class ReplayAcceptanceTests(unittest.TestCase):
    def test_acceptance_requires_both_successful_exits(self):
        stage = {"state": "ok"}
        self.assertEqual(
            pipeline_status(stage, [0, 0], "challenge proven\n"), "verified"
        )
        for codes in ([1, 0], [0, 1], []):
            self.assertEqual(
                pipeline_status(stage, codes, "challenge proven\n"), "proof_error"
            )
        self.assertEqual(
            pipeline_status({"state": "error"}, [0, 0], "challenge proven\n"),
            "proof_error",
        )

    def test_timeout_overrides_success_text(self):
        self.assertEqual(
            pipeline_status({"state": "timeout"}, [0, 0], "challenge proven\n"),
            "proof_timeout",
        )

    def test_inconsistency_and_nonexact_acceptance_are_rejected(self):
        for text in (
            "prefix challenge proven\n",
            "",
            "challenge proven\nInconsistency proven!\n",
        ):
            self.assertEqual(
                pipeline_status({"state": "ok"}, [0, 0], text), "proof_error"
            )


class InputIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "inputs.tar.gz"
        self.output = self.root / "inputs"
        self.output.mkdir()
        self.relative = "packets/challenge-deadline"
        self.files = {"case.json": b"{}\n", "exported-cnf.smt2": b"(check-sat)\n"}
        with tarfile.open(self.bundle, "w:gz") as archive:
            for name, data in self.files.items():
                member = tarfile.TarInfo(
                    f"micromath-handoff-20261109/{self.relative}/{name}"
                )
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        self.manifest = {
            "sha256": hashlib.sha256(self.bundle.read_bytes()).hexdigest(),
            "payload_sha256": {
                f"{self.relative}/{name}": hashlib.sha256(data).hexdigest()
                for name, data in self.files.items()
            },
        }

    def test_copy_and_verify_builder_only_inputs(self):
        payloads = unpack_case(self.bundle, self.manifest, self.relative, self.output)
        self.assertEqual(set(payloads), set(self.files))
        for name, data in self.files.items():
            self.assertEqual((self.output / name).read_bytes(), data)

    def test_archive_and_member_hash_changes_are_rejected(self):
        self.manifest["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "archive hash"):
            unpack_case(self.bundle, self.manifest, self.relative, self.output)
        self.manifest["sha256"] = hashlib.sha256(self.bundle.read_bytes()).hexdigest()
        self.manifest["payload_sha256"][f"{self.relative}/case.json"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "input changed"):
            unpack_case(self.bundle, self.manifest, self.relative, self.output)


class CompleteReplayTests(unittest.TestCase):
    def test_complete_index_coverage_and_profile_selection(self):
        path = (
            Path(__file__).resolve().parents[2]
            / "benchmarks/checker-regressions/checker-replay-20261110/selection.json"
        )
        rows = json.loads(path.read_text())["rows"]
        assigned = assignment(rows, 8)
        self.assertEqual(
            sorted(i for shard in assigned for i in shard), list(range(109))
        )
        self.assertEqual(sum(len(profiles(row)) for row in rows), 139)
        self.assertTrue(
            all(len(profiles(row)) == 1 for row in rows if row["logic"] == "QF_LIA")
        )

    def test_lia_header_only_copy_across_block_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            native, adapted = Path(directory) / "native", Path(directory) / "adapted"
            body = (
                b"(primo-conflicts\n  (version 3)\n  (logic QF_LIA)\n" + b"x" * 1024**2
            )
            native.write_bytes(body)
            converted = body[:28] + b"2" + body[29:]
            adapted.write_bytes(converted)
            self.assertEqual(
                checked_copy(native, adapted), hashlib.sha256(converted).hexdigest()
            )
            adapted.write_bytes(converted[:-1] + b"y")
            with self.assertRaisesRegex(ValueError, "body"):
                checked_copy(native, adapted)


if __name__ == "__main__":
    unittest.main()
