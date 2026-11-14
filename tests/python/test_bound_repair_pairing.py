import unittest

from scripts.qualification.lia_hybrid.run import measurement_order


class PairOrderTests(unittest.TestCase):
    def config(self, repetition):
        groups = [
            [f"baseline-{p}", f"candidate-{p}"]
            for p in ("default", "pre", "rows", "both")
        ]
        return {
            "profiles": {label: {} for pair in groups for label in pair},
            "paired_groups": groups,
            "cases": [{"repetition": repetition}] * 9,
        }

    def test_pairs_stay_adjacent_across_slots_and_rotations(self):
        for repetition in range(3):
            config = self.config(repetition)
            for slot in range(9):
                order = measurement_order(config, slot)
                self.assertEqual(set(order), set(config["profiles"]))
                for offset in range(0, len(order), 2):
                    first, second = order[offset : offset + 2]
                    self.assertEqual(first.split("-", 1)[1], second.split("-", 1)[1])
                    self.assertNotEqual(first.split("-", 1)[0], second.split("-", 1)[0])
                expected_prefix = (
                    "candidate-" if (slot + repetition) % 2 else "baseline-"
                )
                self.assertTrue(order[0].startswith(expected_prefix))

    def test_legacy_rotation_is_unchanged(self):
        self.assertEqual(
            measurement_order({"profiles": {x: {} for x in "abcd"}}, 1), list("bcda")
        )

    def test_missing_duplicate_and_malformed_pairs_fail(self):
        for groups in (
            [],
            [["baseline-default", "baseline-default"]],
            [["absent", "candidate-default"]],
            ["ab"],
        ):
            config = self.config(0)
            config["paired_groups"] = groups
            with self.assertRaises(ValueError):
                measurement_order(config, 0)


class PairedSummaryTests(unittest.TestCase):
    def test_failed_answers_are_penalized_and_do_not_enter_paired_ratios(self):
        from scripts.qualification.bound_repairs.analyze import summarize

        records = {}
        for repetition in range(3):
            for slot in range(2):
                for profile in ("default", "pre", "rows", "both"):
                    for variant in ("baseline", "candidate"):
                        failed = variant == "candidate" and slot == 1
                        records[repetition, slot, f"{variant}-{profile}"] = {
                            "status": "timeout" if failed else "ok",
                            "exit_code": 124 if failed else 0,
                            "answer": "sat",  # Printed before termination: never solved.
                            "seconds": "121" if failed else "2",
                            "peak_rss_kib": 100,
                            "stats": {":lra.pivots": "10"},
                        }
        result = summarize([0, 1], records, 120)["profiles"]["rows"]
        self.assertEqual(result["candidate"]["median_solved"], 1)
        self.assertEqual(result["candidate"]["median_mean_par2_seconds"], 121)
        comparison = result["comparison"]
        self.assertEqual(comparison["stable_lost_slots"], [1])
        self.assertEqual(comparison["paired_solved_deltas"], [-1, -1, -1])
        self.assertEqual(comparison["common_solved_all_three"], 1)
        self.assertEqual(comparison["geomean_paired_median_ratio"], 1)
        self.assertEqual(
            comparison["matched_counters"][":lra.pivots"],
            {
                "baseline": 30,
                "candidate": 30,
            },
        )
