#!/usr/bin/env python3
"""Distinguish unavailable counters, repeat variation and changed selected work."""

import unittest

from scripts.qualification.arithmetic_combinations.mechanisms import KEYS, summarize


class MechanismDiagnostics(unittest.TestCase):
    def test_selected_work_changes_and_missing_data(self):
        rows = []
        for slot in range(3):
            for rep in range(3):
                for profile in ("B", "P"):
                    row = {
                        "logic": "QF_LIA",
                        "slot": str(slot),
                        "repetition": str(rep),
                        "profile": profile,
                        "solved": "True",
                        **dict.fromkeys(KEYS, "1"),
                    }
                    if slot == 1 and profile == "P":
                        row[KEYS[0]] = "2"
                    if slot == 2 and profile == "P":
                        row[KEYS[0]] = ""
                    rows.append(row)
        report = summarize(rows)["QF_LIA"]
        edge = report["one_switch_edges"]["B->P"]
        self.assertEqual(edge["eligible_cases"], 2)
        self.assertEqual(edge["cases_with_changed_selected_counters"], [1])
        self.assertEqual(edge["cases_with_equal_selected_counters"], 1)
        self.assertEqual(edge["cases_changed_by_counter"][KEYS[0]], 1)
        self.assertEqual(report["within_profile"]["P"]["eligible_cases"], 2)
        self.assertEqual(
            report["within_profile"]["P"][
                "cases_varying_selected_counters_across_repetitions"
            ],
            [],
        )
        rows[1][KEYS[1]] = "2"
        self.assertEqual(
            summarize(rows)["QF_LIA"]["within_profile"]["P"][
                "cases_varying_selected_counters_across_repetitions"
            ],
            [0],
        )


if __name__ == "__main__":
    unittest.main()
