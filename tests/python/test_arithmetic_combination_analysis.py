#!/usr/bin/env python3
"""Check paired interaction arithmetic, cutoff exclusion and regression screening."""

from __future__ import annotations

import unittest

from scripts.qualification.arithmetic_combinations.analyze import (
    compare,
    expected_resource_diagnostic,
    interaction,
    metric,
    observed_metric,
)
from scripts.qualification.bound_propagation.analyze import audit_row


def row(seconds, answer="sat", status="ok", exit_code=0):
    return {
        "seconds": seconds,
        "answer": answer,
        "status": status,
        "exit_code": exit_code,
        "stats": {":sat.conflicts": 7},
        "peak_rss_kib": 100,
    }


class CombinationAnalysis(unittest.TestCase):
    def test_conditioned_interaction_and_cutoff_exclusion(self):
        records = {}
        # The effect of P is 1/2 without R, 1/4 with R, hence interaction 1/2.
        for rep in range(3):
            for p, time in {"H": 8, "PH": 4, "RH": 4, "PRH": 1}.items():
                records[rep, 0, p] = row(time * (rep + 1))
                records[rep, 1, p] = row(time)
        records[1, 1, "PRH"] = row(120, "sat", "timeout", 124)
        value = interaction(records, [0, 1], ("H", "PH", "RH", "PRH"))
        self.assertEqual(value["eligible_cases"], 1)
        self.assertAlmostEqual(value["geomean_case_median_ratio"], 0.5)
        self.assertEqual(value["cases"][0]["repetition_ratios"], [0.5, 0.5, 0.5])

    def test_stable_loss_and_consistent_regression(self):
        records = {}
        for rep in range(3):
            records[rep, 0, "B"] = row(10)
            records[rep, 0, "P"] = row(12)
            records[rep, 1, "B"] = row(10)
            records[rep, 1, "P"] = row(120, "unknown", "timeout", 124)
        result = compare(records, [0, 1], "B", "P", 120)
        self.assertEqual(result["stable_losses"], [1])
        self.assertEqual(result["regression_followups"][0]["slot"], 0)
        self.assertEqual(result["common_solved_all_repetitions"], 1)
        self.assertAlmostEqual(result["conditional_geomean_paired_median_ratio"], 1.2)
        self.assertEqual(result["repetitions"][0]["mean_PAR2_delta_seconds"], 116)
        # One rotated repetition reverses the slowdown: retain its detail but
        # do not describe it as a consistent regression.
        records[2, 0, "P"] = row(9)
        self.assertEqual(
            compare(records, [0, 1], "B", "P", 120)["regression_followups"], []
        )

    def test_missing_statistics_remain_unavailable(self):
        records = {(rep, 0, p): row(2) for rep in range(3) for p in ("B", "P")}
        for rep in range(3):
            records[rep, 0, "P"]["stats"] = {}
        result = compare(records, [0], "B", "P", 120)
        self.assertEqual(result["matched_complete_counter_pairs"], 0)
        self.assertEqual(result["common_solved_counter_pairs_without_statistics"], 3)
        self.assertIsNone(
            result["matched_complete_counter_totals"][":sat.conflicts"]["P"]
        )

    def test_deadline_does_not_hide_unexplained_errors(self):
        late = row(120, answer="error", status="timeout", exit_code=2)
        self.assertEqual(expected_resource_diagnostic(late), "std::bad_alloc")
        late["exit_code"] = -6
        with self.assertRaises(AssertionError):
            expected_resource_diagnostic(late)
        self.assertEqual(
            expected_resource_diagnostic(row(3, answer="unknown")),
            "integer branch-and-bound node limit",
        )

    def test_interrupted_statistics_and_crash_after_answer(self):
        self.assertIsNone(
            metric(row(120, status="timeout", exit_code=124), ":sat.conflicts")
        )
        self.assertEqual(metric(row(1), ":lra.pivots"), 0)
        self.assertEqual(
            observed_metric(
                row(120, status="timeout", exit_code=124), ":sat.conflicts"
            ),
            7,
        )
        self.assertIsNone(
            observed_metric(row(120, status="timeout", exit_code=124), ":lra.pivots")
        )
        with self.assertRaises(AssertionError):
            audit_row(row(1, exit_code=-6))


if __name__ == "__main__":
    unittest.main()
