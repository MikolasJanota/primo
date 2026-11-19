"""The promotion report must retain incomplete outcomes and paired losses."""

import unittest

from scripts.qualification.qflia_defaults.audit import comparison


def row(file, answer, seconds=1.0, status="ok", exit_code=0):
    return {
        "file": file,
        "answer": answer,
        "seconds": seconds,
        "status": status,
        "exit_code": exit_code,
    }


class DefaultComparisonTests(unittest.TestCase):
    def test_resource_unknown_is_unsolved_and_penalized(self):
        rows = [{"OLD": row("a", "unknown"), "NEW": row("a", "sat", 2)}]
        result = comparison(rows, "OLD", "NEW", 120)
        self.assertEqual(result["gains"], ["a"])
        self.assertEqual(result["mean_PAR2_delta_seconds"], -238)
        self.assertIsNone(result["conditional_geomean_ratio"])

    def test_timeout_answer_and_failed_process_do_not_count_as_solves(self):
        rows = [
            {"OLD": row("a", "sat"), "NEW": row("a", "sat", 120, "timeout", -15)},
            {"OLD": row("b", "unsat"), "NEW": row("b", "unsat", 2, "error", 2)},
        ]
        result = comparison(rows, "OLD", "NEW", 120)
        self.assertEqual(result["losses"], ["a", "b"])
        self.assertEqual(result["common_solved"], 0)
        self.assertEqual(result["mean_PAR2_delta_seconds"], 239)

    def test_subsecond_pairs_are_excluded_from_runtime_ratio(self):
        rows = [
            {"OLD": row("a", "sat", 0.001), "NEW": row("a", "sat", 0.1)},
            {"OLD": row("b", "unsat", 4), "NEW": row("b", "unsat", 2)},
        ]
        result = comparison(rows, "OLD", "NEW", 120)
        self.assertEqual(result["common_solved"], 2)
        self.assertEqual(result["common_before_at_least_1s"], 1)
        self.assertAlmostEqual(result["conditional_geomean_ratio"], 0.5)

    def test_report_does_not_hide_successful_runtime_regressions(self):
        rows = [{"OLD": row("a", "sat", 3), "NEW": row("a", "sat", 6)}]
        result = comparison(rows, "OLD", "NEW", 120)
        self.assertEqual(
            result["runtime_regressions_to_repeat"],
            [{"file": "a", "before": 3, "after": 6}],
        )
        self.assertIn("not repeat-qualified", result["limitation"])
