"""Check strict proof acceptance independently of external checker execution."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from replay_arithmetic_checker_update import pipeline_result


class ReplayAcceptanceTests(unittest.TestCase):
    def result(self, message, codes="0 0\n", state="ok"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "kernel.out").write_text(message)
            (path / "exits.txt").write_text(codes)
            return pipeline_result({"state": state}, path, "exits.txt")[0]

    def test_both_exits_and_exact_acceptance(self):
        self.assertEqual(self.result("challenge proven\n"), "verified")

    def test_exit_errors_override_printed_acceptance(self):
        for codes in ("0 1\n", "1 0\n", "", "0\n"):
            with self.subTest(codes=codes):
                self.assertEqual(
                    self.result("challenge proven\n", codes), "proof_error"
                )
        self.assertEqual(
            self.result("challenge proven\n", state="error"), "proof_error"
        )

    def test_reject_missing_exact_marker_or_inconsistency(self):
        for text in (
            "",
            "prefix challenge proven\n",
            "challenge proven extra\n",
            "challenge proven\nInconsistency proven!\n",
        ):
            with self.subTest(text=text):
                self.assertEqual(self.result(text), "proof_error")

    def test_timeout_overrides_printed_acceptance(self):
        self.assertEqual(
            self.result("challenge proven\n", state="timeout"), "proof_timeout"
        )


if __name__ == "__main__":
    unittest.main()
