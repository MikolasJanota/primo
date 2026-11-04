#!/usr/bin/env python3
"""Independent exact checker negative controls, without solver subprocesses."""

from __future__ import annotations

import copy
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "checker", Path(__file__).parents[2] / "scripts/check_lra_certificate.py"
)
CHECKER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(CHECKER)

SOURCE = """(primo-conflicts (version 2) (logic QF_LRA)
(signature (declare-fun |x; quoted| () Real) (declare-fun y () Real))
(cnf (clause (<= (+ |x; quoted| y) 2)) (clause (>= |x; quoted| 2)) (clause (> y 0)))
(conflicts (conflict-lra
 ((<= (+ |x; quoted| y) 2) 1) ((>= |x; quoted| 2) 1) ((> y 0) 1))))"""


class ExactCertificateCheckerTests(unittest.TestCase):
    def test_strict_and_fractional_witnesses(self):
        root = CHECKER.parse(SOURCE)
        report, clauses = CHECKER.validate(root)
        self.assertEqual(report["conflicts"], 1)
        self.assertEqual(len(clauses), 4)
        for pair in root[5][1][1:]:
            pair[1] = ["/", "1", "7"]
        CHECKER.validate(root)

    def test_rejects_bad_coefficients_and_lost_strictness(self):
        root = CHECKER.parse(SOURCE)
        for coefficient in ["-1", "0", ["/", "1", "0"], "0.5"]:
            damaged = copy.deepcopy(root)
            damaged[5][1][-1][1] = coefficient
            with self.assertRaises(ValueError):
                CHECKER.validate(damaged)
        damaged = copy.deepcopy(root)
        damaged[5][1][-1][0][0] = ">="
        with self.assertRaises(ValueError):
            CHECKER.validate(damaged)

    def test_rejects_uncancelled_variable_polarity_and_wrong_constant(self):
        root = CHECKER.parse(SOURCE)
        mutations = [
            ["<=", "y", "0"],
            ["not", [">", "y", "0"]],
            [">", "y", "-1"],
            [">", ["*", "y", "y"], "0"],
        ]
        for literal in mutations:
            damaged = copy.deepcopy(root)
            damaged[5][1][-1][0] = literal
            with self.assertRaises(ValueError):
                CHECKER.validate(damaged)

    def test_zero_weight_strict_fact_does_not_create_contradiction(self):
        root = CHECKER.parse(SOURCE)
        root[5][1] = ["conflict-lra", [["<", "y", "1"], "0"]]
        with self.assertRaises(ValueError):
            CHECKER.validate(root)

    def test_weak_contradiction_and_zero_support_premise(self):
        source = """(primo-conflicts (version 2) (logic QF_LRA)
        (signature (declare-fun x () Real)) (cnf)
        (conflicts (conflict-lra ((<= x 0) 1) ((>= x 1) 1) ((< x 9) 0))))"""
        report, _ = CHECKER.validate(CHECKER.parse(source))
        self.assertEqual(report["entries"], 3)

    def test_rejects_declarations_sort_aliases_and_container_errors(self):
        for source in [
            SOURCE + ")",
            SOURCE.replace("(version 2)", "(version 1)"),
            SOURCE.replace("() Real", "() Int", 1),
            SOURCE.replace("(declare-fun y () Real)", ""),
        ]:
            with self.assertRaises(ValueError):
                CHECKER.validate(CHECKER.parse(source))


if __name__ == "__main__":
    unittest.main()
