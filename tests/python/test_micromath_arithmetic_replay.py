"""Replay success/rejection must not hide crashes or ignore kernel completion."""

import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import replay_micromath_lra as workflow


class PipelineClassificationTests(unittest.TestCase):
    def run_case(self, codes, output, expected, state, wanted):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "input.smt2"
            certificate = root / "input.sexp"
            source.write_text("controlled statement")
            certificate.write_text("controlled certificate")
            args = SimpleNamespace(
                out=root, micromath=root, cadical=root / "cadical", stage_seconds=1
            )
            rows = []

            def pipeline(*unused):
                directory = root / "case/pipe"
                (directory / "exit-status.txt").write_text(" ".join(map(str, codes)))
                (directory / "kernel.out").write_text(output)
                return {"state": state}

            with (
                patch.object(workflow, "bounded", return_value={"state": "ok"}),
                patch.object(workflow, "invoke", side_effect=pipeline),
            ):
                try:
                    workflow.replay_case(
                        args,
                        root / "axioms",
                        rows,
                        lambda: None,
                        "case",
                        source,
                        certificate,
                        expected,
                    )
                except SystemExit:
                    pass
            self.assertEqual(rows[0]["status"], wanted)

    def test_kernel_crash_is_not_an_expected_rejection(self):
        for codes in ([1, 139], [1, 137], [1, 127], [139, 1], [127, 1]):
            with self.subTest(codes=codes):
                self.run_case(codes, "invalid solution\n", "reject", "error", "error")

    def test_rejection_and_proof_completion_are_distinct(self):
        self.run_case(
            [1, 1], "invalid solution\n", "reject", "error", "expected_rejection"
        )
        self.run_case(
            [0, 1], "invalid solution\n", "reject", "error", "expected_rejection"
        )
        self.run_case([0, 0], "challenge proven\n", "reject", "ok", "invalid_accepted")
        self.run_case([0, 0], "challenge proven\n", "accept", "ok", "verified")
        self.run_case([0, 0], "", "accept", "ok", "error")
        self.run_case(
            [0, 0], "challenge proven\nInconsistency proven!\n", "accept", "ok", "error"
        )
        self.run_case([0, 0], "challenge proven\n", "accept", "timeout", "timeout")


if __name__ == "__main__":
    unittest.main()
