"""Integer cut checking must validate its arithmetic, scope and exact division."""

import copy
import importlib.util
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "lia_checker", ROOT / "scripts/check_lia_certificate.py"
)
CHECKER = importlib.util.module_from_spec(SPEC)
with mock.patch.object(sys, "path", [str(ROOT / "scripts"), *sys.path]):
    SPEC.loader.exec_module(CHECKER)


class LiaCertificateCheckerTests(unittest.TestCase):
    def example(self):
        return CHECKER.parse(
            (ROOT / "docs/examples/qf-lia-certificate-proposal.sexp").read_text()
        )

    def reject(self, root):
        with self.assertRaises((ValueError, TypeError, IndexError)):
            CHECKER.validate(root)

    def test_named_example_and_negative_floor(self):
        report, clauses = CHECKER.validate(self.example())
        self.assertEqual(report["cuts"], 3)
        self.assertEqual(report["entries"], 3)
        self.assertEqual(len(clauses), 4)
        self.assertEqual(
            CHECKER.inequality(["not", ["<=", "x", "2"]], {"x": "Int"}), ({"x": -1}, -3)
        )
        self.assertEqual(
            CHECKER.inequality(["not", ["<", "x", "2"]], {"x": "Int"}), ({"x": -1}, -2)
        )
        self.assertEqual(
            CHECKER.inequality(["<", "x", "2"], {"x": "Int"}), ({"x": 1}, 1)
        )

    def test_bad_divisors_weights_and_scope(self):
        for divisor in ("0", "2", ["/", "3", "1"]):
            root = self.example()
            root[5][1][2][1][2] = divisor
            self.reject(root)
        for weight in ("-1", ["/", "1", "2"]):
            root = self.example()
            root[5][1][2][1][3][1] = weight
            self.reject(root)
        for label in ("upper", "missing", "lower"):
            root = self.example()
            root[5][1][2][1][3][0] = label
            self.reject(root)
        root = self.example()
        root[5][1][2][1][1] = "a"
        self.reject(root)
        root = self.example()
        root[5][1][1][2][0] = "a"
        self.reject(root)
        root = self.example()
        second = copy.deepcopy(root[5][1])
        second[1][1][0] = "renamed"
        second[2][1][3][0] = "upper"
        root[5].append(second)
        self.reject(root)

    def test_bad_contradictions_and_noninteger_terms(self):
        root = self.example()
        root[5][1][2][-1][1] = "upper"
        self.reject(root)
        root = self.example()
        root[5][1][2].append(["contradiction", "impossible"])
        self.reject(root)
        root = self.example()
        root[3][1][3] = "Real"
        self.reject(root)
        root = self.example()
        root[5][1][1][1][1][1] = ["*", "x", "y"]
        self.reject(root)
        root = self.example()
        root[5][1][1][1][1][2] = ["/", "4", "1"]
        self.reject(root)

    def test_zero_weights_repeated_refs_and_unused_premise(self):
        root = self.example()
        first_cut = root[5][1][2][1]
        first_cut += [["a", "0"]]
        first_cut[4][1] = "1"
        first_cut += [["b", "1"]]
        root[5][1][1].append(["unused", ["<=", "x", "100"]])
        report, clauses = CHECKER.validate(root)
        self.assertEqual(report["entries"], 4)
        self.assertEqual(len(clauses[-1]), 4)

    def test_strict_premise_can_be_the_contradiction(self):
        root = self.example()
        root[4] = ["cnf", ["clause", ["<", "0", "0"]]]
        root[5] = [
            "conflicts",
            [
                "conflict-lia",
                ["premises", ["p", ["<", "0", "0"]]],
                ["proof", ["contradiction", "p"]],
            ],
        ]
        self.assertEqual(CHECKER.validate(root)[0]["cuts"], 0)
        root[5][1][1][1][1][0] = "<="
        self.reject(root)


if __name__ == "__main__":
    unittest.main()
