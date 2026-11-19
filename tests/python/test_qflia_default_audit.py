"""The promotion report must retain incomplete outcomes and paired losses."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.qflia_defaults.audit import (
    assert_single_core,
    comparison,
    expected_answer,
    read_rows,
)


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


class TimingWireFormatTests(unittest.TestCase):
    def test_real_harness_decimal_string_is_normalized(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.jsonl"
            path.write_text('{"seconds":"0.013483","answer":"unsat"}\n')
            self.assertEqual(
                read_rows(path), [{"seconds": 0.013483, "answer": "unsat"}]
            )

    def test_nonfinite_time_cannot_enter_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.jsonl"
            path.write_text('{"seconds":"NaN"}\n')
            with self.assertRaises(AssertionError):
                read_rows(path)


class CpuTopologyTests(unittest.TestCase):
    def setUp(self):
        self.topology = {
            "host": {
                "cpus": {
                    "0": {"physical_package_id": "0", "core_id": "0"},
                    "28": {"physical_package_id": "0", "core_id": "0"},
                    "14": {"physical_package_id": "1", "core_id": "0"},
                }
            }
        }

    def test_sibling_threads_share_one_core(self):
        assert_single_core([0, 28], "host", self.topology)

    def test_same_core_id_on_different_sockets_is_rejected(self):
        with self.assertRaises(AssertionError):
            assert_single_core([0, 14], "host", self.topology)

    def test_unrecorded_cpu_cannot_be_assumed_to_be_sibling(self):
        with self.assertRaises(KeyError):
            assert_single_core([0, 29], "host", self.topology)


class ExpectedStatusTests(unittest.TestCase):
    def test_unknown_archive_uses_hashed_input_declared_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "QF_LIA" / "case.smt2"
            source.parent.mkdir()
            source.write_text("(set-info :status unsat)\n")
            case = {
                "logic": "QF_LIA",
                "file": "case.smt2",
                "expected": "unknown",
                "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            }
            self.assertEqual(expected_answer(case, root), "unsat")
            case["sha256"] = "wrong"
            with self.assertRaises(AssertionError):
                expected_answer(case, root)

    def test_known_oracle_does_not_require_fallback_input(self):
        self.assertEqual(expected_answer({"expected": "sat"}, None), "sat")
