import unittest

from scripts.qualification.bound_propagation.analyze import LABELS, summarize


class AnalysisTests(unittest.TestCase):
    def test_interrupted_answer_is_unsolved_and_not_a_paired_runtime(self):
        records = {}
        for rep in range(3):
            for slot in range(2):
                for label in LABELS:
                    records[rep, slot, label] = {
                        "status": "ok",
                        "exit_code": 0,
                        "answer": "sat",
                        "seconds": 2.0 if label == "default" else 1.0,
                        "peak_rss_kib": 1024,
                        "stats": {":lra.theory-propagations": 7},
                    }
            records[rep, 1, "pre"].update(status="timeout", exit_code=-15)
        result = summarize([0, 1], records, 120)
        pre = result["profiles"]["pre"]
        self.assertEqual(pre["solved_counts"], [1, 1, 1])
        self.assertEqual(pre["median_mean_par2_seconds"], 120.5)
        comparison = result["comparisons"]["default->pre"]
        self.assertEqual(comparison["stable_lost_slots"], [1])
        self.assertEqual(comparison["common_solved_all_three"], 1)
        self.assertEqual(comparison["median_of_case_paired_median_ratios"], 0.5)
        self.assertEqual(
            comparison["matched_complete_counter_totals"][":lra.theory-propagations"][
                "matched_complete_rows"
            ],
            3,
        )

    def test_one_repetition_gain_is_not_a_stable_gain(self):
        records = {}
        for rep in range(3):
            for label in LABELS:
                records[rep, 0, label] = {
                    "status": "timeout",
                    "exit_code": -15,
                    "answer": "unknown",
                    "seconds": 120,
                    "peak_rss_kib": 1024,
                    "stats": {},
                }
        records[0, 0, "both"].update(
            status="ok", exit_code=0, answer="unsat", seconds=1
        )
        result = summarize([0], records, 120)
        self.assertEqual(result["profiles"]["both"]["intermittent_solved"], 1)
        self.assertEqual(
            result["comparisons"]["default->both"]["stable_gained_slots"], []
        )
        self.assertEqual(
            result["comparisons"]["default->both"]["repetitions"][0]["gained"], 1
        )
