"""Pure discovery selection and strict checker acceptance checks."""

import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from replay_checker_handoff import pipeline_status
from replay_checker_medium import cost_band, validate_selection


class MediumSelectionTests(unittest.TestCase):
    def setUp(self):
        self.selection = {
            "rows": [
                {
                    "logic": "QF_LIA",
                    "file": "test.smt2",
                    "primary_case": "QF_LIA/shard-0/run/000",
                    "retained_lemmas": 1,
                    "previous": {"status": "verified", "pipeline_exit_codes": [0, 0]},
                }
            ]
        }

    def test_cost_band_edges(self):
        self.assertEqual(
            [cost_band(x) for x in (0, 9.99, 10, 90, 90.01)],
            ["short", "short", "medium", "medium", "near_limit"],
        )

    def test_selection_rejects_bad_paths_duplicates_and_failed_inputs(self):
        for field, value in (
            ("primary_case", "../elsewhere"),
            ("primary_case", "/tmp/data"),
            ("primary_case", "QF_LRA/shard-0/run/000"),
            ("logic", "QF_UF"),
            ("retained_lemmas", 0),
        ):
            selection = copy.deepcopy(self.selection)
            selection["rows"][0][field] = value
            with self.assertRaises(ValueError):
                validate_selection(selection)
        selection = copy.deepcopy(self.selection)
        selection["rows"] *= 2
        with self.assertRaises(ValueError):
            validate_selection(selection)
        selection = copy.deepcopy(self.selection)
        selection["rows"][0]["previous"]["pipeline_exit_codes"] = [1, 0]
        with self.assertRaises(ValueError):
            validate_selection(selection)

    def test_kernel_output_cannot_hide_failed_process(self):
        self.assertEqual(
            pipeline_status({"state": "error"}, [1, 0], "challenge proven\n"),
            "proof_error",
        )
        self.assertEqual(
            pipeline_status(
                {"state": "ok"}, [0, 0], "challenge proven\nInconsistency proven!\n"
            ),
            "proof_error",
        )

    def test_frozen_selection_covers_diverse_positive_inputs(self):
        root = Path(__file__).resolve().parents[2]
        selection = json.loads(
            (
                root
                / "benchmarks/checker-regressions/checker-medium-20261111/selection.json"
            ).read_text()
        )
        validate_selection(selection)
        self.assertEqual(len(selection["rows"]), 9)
        self.assertEqual(len({r["family"] for r in selection["rows"]}), 9)
        self.assertTrue(
            all(10 <= r["previous"]["proof_seconds"] <= 90 for r in selection["rows"])
        )
        self.assertTrue(all(r["native_bytes"] <= 25_000_000 for r in selection["rows"]))


if __name__ == "__main__":
    unittest.main()
