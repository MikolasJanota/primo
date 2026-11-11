"""Integer cut preservation and streaming lifetime tests; no solver is executed."""

import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from check_lia_certificate import validate
from check_lra_certificate import parse
from farkas_coefficients import better
from lia_coefficients import cheap_cut, cut_optimization_problem, cut_vector, replay_cut
from minimize_lra_coefficients import Optimizer, transform
from test_farkas_coefficients import args

CERTIFICATE = b"""; keep comments and spaces
(primo-conflicts (version 3) (logic QF_LIA)
 (signature (declare-fun |x (proof)| () Int))
 (cnf (clause (<= (* (- 2) |x (proof)|) (- 1)))
      (clause (<= (* 2 |x (proof)|) 1)))
 (conflicts (conflict-lia
  (premises (|p low| (<= (* (- 2) |x (proof)|) (- 1)))
            (high (<= (* 2 |x (proof)|) 1)))
  (proof
   (cut |c low| 200 (|p low| 100))
   (cut impossible 100 (|c low| 200) (high 100))
   (contradiction impossible))))) ; tail
"""


class IntegerCoefficientTests(unittest.TestCase):
    def test_joint_gcd_negative_floor_and_fixed_conclusion(self):
        rows = [({"x": -2}, -1)]
        self.assertEqual(replay_cut(rows, [100], 200), ({"x": -1}, -1))
        self.assertEqual(cheap_cut(rows, [100], 200), ([1], 2))
        self.assertEqual(replay_cut(rows, [1], 2), replay_cut(rows, [100], 200))
        # Dividing only the weight changes the left side/divisibility.
        with self.assertRaises(ValueError):
            replay_cut(rows, [1], 200)

    def test_invalid_integer_steps(self):
        for weights, divisor in (([0], 1), ([-1], 1), ([1], 0), ([1], 3), ([1.0], 1)):
            with self.assertRaises(ValueError):
                replay_cut([({"x": 2}, -1)], weights, divisor)

    def test_duplicate_rows_and_support_guard(self):
        rows = [({"x": 1}, 0), ({"x": 1}, 0), ({"x": -1}, -1)]
        weights, d = cheap_cut(rows, [1, 1, 2], 1, objective="support")
        self.assertEqual(replay_cut(rows, weights, d), ({}, -2))
        self.assertEqual(weights, [2, 0, 2])
        self.assertTrue(
            better(cut_vector(weights, d), cut_vector([1, 1, 2], 1), "support")
        )
        # Sparse candidates still cannot exceed either numeric-size guard.
        self.assertFalse(
            better(cut_vector([100, 0, 100], 1), cut_vector([1, 1, 2], 1), "support")
        )

    def test_fixed_target_encoding_and_limits(self):
        program = cut_optimization_problem(
            [({"x": -2}, -1)], [100], 200, objective="support"
        )
        self.assertIn("(assert (= (* (- 2) w0) (* (- 1) divisor)))", program)
        self.assertIn("(assert (<= (* (- 1) divisor) (* (- 1) w0)))", program)
        self.assertIn("(assert (< (* (- 1) w0) (* 0 divisor)))", program)
        self.assertLess(
            program.index("(minimize (+ (ite"), program.index("(minimize max_weight)")
        )
        self.assertIsNone(
            cut_optimization_problem([({"x": -2}, -1)], [100], 200, bits=2)
        )


class IntegerStreamingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def rewrite(self, data, optimizer=None):
        source = self.root / "input.sexp"
        source.write_bytes(data)
        output = io.BytesIO()
        stats = transform(source, output, optimizer or Optimizer(args(), self.root))
        return output.getvalue(), stats

    def test_only_integer_numeral_spans_change_and_downstream_is_valid(self):
        result, stats = self.rewrite(CERTIFICATE)
        expected = CERTIFICATE.replace(b"200 (|p low| 100)", b"2 (|p low| 1)")
        expected = expected.replace(
            b"100 (|c low| 200) (high 100)", b"1 (|c low| 2) (high 1)"
        )
        self.assertEqual(result, expected)
        self.assertEqual(validate(parse(result.decode()))[0]["cuts"], 2)
        self.assertEqual(stats["changed_cuts"], 2)
        self.assertEqual(stats["changed_conflicts"], 1)
        self.assertEqual(stats["released_rows"], 3)
        self.assertEqual(stats["peak_live_rows"], 2)

    def test_unused_rows_and_zero_weight_references(self):
        data = CERTIFICATE.replace(
            b"(cut impossible", b"(cut unused 1 (high 1))\n   (cut impossible"
        )
        data = data.replace(b"(high 100))", b"(high 100) (unused 0))")
        result, stats = self.rewrite(data)
        validate(parse(result.decode()))
        self.assertEqual(stats["released_rows"], 4)
        self.assertEqual(stats["peak_live_rows"], 3)

    def test_duplicate_references_are_replayed_and_released_once(self):
        data = CERTIFICATE.replace(b"(high 100)", b"(high 50) (high 50)")
        result, stats = self.rewrite(data)
        validate(parse(result.decode()))
        self.assertEqual(stats["released_rows"], 3)

    def test_last_use_storage_is_bounded_on_a_long_chain(self):
        previous = b"|c low|"
        chain = []
        for index in range(500):
            label = f"copy{index}".encode()
            chain.append(b"(cut " + label + b" 1 (" + previous + b" 1))\n")
            previous = label
        data = CERTIFICATE.replace(
            b"(cut impossible", b"".join(chain) + b"(cut impossible"
        )
        data = data.replace(b"(|c low| 200)", b"(" + previous + b" 200)")
        result, stats = self.rewrite(data)
        validate(parse(result.decode()))
        self.assertEqual(stats["cuts"], 502)
        self.assertEqual(stats["peak_live_rows"], 2)
        self.assertEqual(stats["released_rows"], 503)

    def test_second_scan_offsets_beyond_a_lexer_block(self):
        data = b";" + b"padding" * 12000 + b"\n" + CERTIFICATE
        result, _ = self.rewrite(data)
        validate(parse(result.decode()))
        self.assertTrue(result.startswith(b";" + b"padding" * 12000 + b"\n"))

    def test_proof_byte_guard_fires_during_first_scan(self):
        # The malformed tail must not be reached after exceeding the byte budget.
        pairs = b" (high 0)" * 100
        data = CERTIFICATE.replace(b"(high 100))", b"(high 100)" + pairs + b" BROKEN))")
        with patch("minimize_lra_coefficients.INTEGER_CONFLICT_BYTES", 800):
            with self.assertRaisesRegex(ValueError, "byte limit"):
                self.rewrite(data)

    def test_failed_late_cut_preserves_existing_output(self):
        source, output = self.root / "input.sexp", self.root / "output.sexp"
        source.write_bytes(CERTIFICATE.replace(b"(high 100)", b"(high 99)"))
        output.write_bytes(b"previous certificate")
        script = (
            Path(__file__).resolve().parents[2] / "scripts/minimize_lra_coefficients.py"
        )
        stage = subprocess.run(
            [sys.executable, str(script), str(source), str(output)],
            capture_output=True,
            timeout=10,
            check=False,
        )
        self.assertNotEqual(stage.returncode, 0)
        self.assertEqual(output.read_bytes(), b"previous certificate")

    def test_bad_grammar_scope_and_arithmetic_rejected(self):
        for data in (
            CERTIFICATE.replace(b"version 3", b"version 2"),
            CERTIFICATE.replace(b"(|p low| 100)", b"(forward 100)"),
            CERTIFICATE.replace(b"cut impossible", b"cut |c low|"),
            CERTIFICATE.replace(b"|c low| 200", b"|c low| 0"),
            CERTIFICATE.replace(b"|c low| 200", b"|c low| 201"),
            CERTIFICATE.replace(b"contradiction impossible", b"contradiction high"),
            CERTIFICATE.replace(b"(|p low| 100)", b"(|p low| 0)"),
            CERTIFICATE + b"trailing",
        ):
            with self.assertRaises(ValueError):
                self.rewrite(data)

    def test_optimizer_cannot_change_an_intermediate_conclusion(self):
        rows = [({"x": -2}, -1)]
        optimizer = Optimizer(args(optimizer="fake"), self.root)

        def fake_invoke(command, cwd, log, seconds):
            # A valid cut, but -2*x<=-1 is different from -x<=-1.
            (cwd / "optimizer.out").write_text("sat\n((w0 1) (divisor 1))")
            return {"state": "ok", "seconds": 0.01}

        with patch("minimize_lra_coefficients.invoke", fake_invoke):
            self.assertEqual(optimizer.improve_cut(0, rows, [100], 200), ([100], 200))
        self.assertEqual(optimizer.counts["invalid_witness"], 1)


if __name__ == "__main__":
    unittest.main()
