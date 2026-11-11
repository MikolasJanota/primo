"""Pure exact-arithmetic and streaming tests; no external solver is executed."""

import argparse
import io
import json
import subprocess
import sys
import tempfile
import unittest
from fractions import Fraction as F
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from farkas_coefficients import (
    cheap,
    cost,
    integers,
    model_weights,
    optimization_problem,
    valid,
)
from micromath_stress import invoke
from minimize_lra_coefficients import Lexer, Optimizer, transform


def args(**changes):
    values = {
        "optimizer": None,
        "problems_dir": None,
        "min_bits": 0,
        "max_premises": 256,
        "max_queries": 32,
        "max_bits": 4096,
        "total_seconds": 30,
        "call_seconds": 1,
        "optimizer_arg": [],
        "objective": "size",
    }
    values.update(changes)
    return argparse.Namespace(**values)


class CoefficientTests(unittest.TestCase):
    def setUp(self):
        self.premises = [({"x": F(1)}, F(0), False), ({"x": F(-1)}, F(-1), False)]

    def test_common_scale_and_fractions(self):
        for weights in ([F(1000000), F(1000000)], [F(1, 100001)] * 2):
            self.assertEqual(cheap(self.premises, weights), [F(1), F(1)])

    def test_different_integer_ratio(self):
        premises = [({"x": F(2)}, F(0), False), ({"x": F(-3)}, F(-1), False)]
        self.assertEqual(cheap(premises, [F(3000), F(2000)]), [F(3), F(2)])

    def test_lcm_budget_and_bad_candidate(self):
        self.assertIsNone(integers([F(1, 17), F(1, 19)], bits=5))
        self.assertFalse(valid(self.premises, [F(1), F(2)]))
        self.assertFalse(valid(self.premises, [F(0), F(0)]))
        with self.assertRaisesRegex(ValueError, "original"):
            cheap(self.premises, [F(1), F(2)])

    def test_duplicate_and_strict_opposite_rows(self):
        premises = [
            ({"x": F(1)}, F(0), False),
            ({"x": F(1)}, F(0), False),
            ({"x": F(-1)}, F(0), True),
        ]
        result = cheap(premises, [F(1), F(1), F(2)])
        self.assertTrue(valid(premises, result))
        self.assertEqual(cost(result)[2], 2)
        self.assertEqual(max(result), 1)

    def test_zero_weight_strictness_is_not_evidence(self):
        premises = [
            ({"x": F(1)}, F(0), False),
            ({"x": F(-1)}, F(0), False),
            ({"x": F(1)}, F(0), True),
        ]
        self.assertFalse(valid(premises, [F(1), F(1), F(0)]))
        self.assertTrue(valid(premises, [F(0), F(1), F(1)]))

    def test_constant_contradiction(self):
        premises = [({}, F(0), True), ({"x": F(1)}, F(5), False)]
        self.assertEqual(cheap(premises, [F(500), F(0)]), [F(1), F(0)])

    def test_optimization_has_integer_contradiction_and_objective_order(self):
        program = optimization_problem(self.premises, [F(10)] * 2)
        self.assertIn("(set-logic QF_LIA)", program)
        self.assertIn("(minimize max_weight)", program)
        self.assertLess(
            program.index("(minimize max_weight)"), program.index("(minimize (+")
        )
        self.assertIn("(- 1)", program)
        self.assertIsNone(optimization_problem(self.premises, [F(1000)] * 2, bits=2))

    def test_model_response_contract(self):
        self.assertEqual(model_weights("sat\n((w0 1) (w1 2))\n", 2), [F(1), F(2)])
        for text in (
            "unknown\n((w0 1))",
            "sat\n((w0 1))",
            "sat\n((w0 1) (w0 2))",
            "sat\n((w0 (- 1)) (w1 2))",
            "sat\n((w0 1) (w1 2)) extra",
        ):
            with self.assertRaises(ValueError):
                model_weights(text, 2)

    def test_support_objective_is_explicit_and_precedes_size(self):
        program = optimization_problem(self.premises, [F(10)] * 2, objective="support")
        self.assertLess(
            program.index("(minimize (+ (ite"), program.index("(minimize max_weight)")
        )
        self.assertNotIn("(ite (> w0", optimization_problem(self.premises, [F(10)] * 2))

    def test_interruption_reaps_external_process(self):
        process = Mock(pid=1234)
        process.wait.side_effect = [InterruptedError("cancelled"), 0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                patch("micromath_stress.subprocess.Popen", return_value=process),
                patch("micromath_stress.os.killpg") as kill,
                self.assertRaises(InterruptedError),
            ):
                invoke(["fake"], root, root / "log", 1)
        self.assertEqual(process.wait.call_count, 2)
        self.assertEqual(kill.call_args.args[0], process.pid)


class StreamingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def rewrite(self, data):
        source = self.root / "input.sexp"
        source.write_bytes(data)
        out = io.BytesIO()
        stats = transform(source, out, Optimizer(args(), self.root))
        return out.getvalue(), stats

    def test_only_weight_spans_change(self):
        data = (
            b"; retained header\n(primo-conflicts (version 2) (logic QF_LRA)\n"
            b"(signature (declare-fun |x\n  (conflicts\n)| () Real))\n"
            b"(cnf (clause false))\n(conflicts\n"
            b" (conflict-lra ((<= |x\n  (conflicts\n)| 0) 1000)\n"
            b" ((not (< |x\n  (conflicts\n)| 1)) 1000))))\n; tail\n"
        )
        result, stats = self.rewrite(data)
        self.assertEqual(result, data.replace(b" 1000)", b" 1)"))
        self.assertEqual(stats["changed_conflicts"], 1)
        self.assertEqual(stats["before_max_bits"], 10)
        self.assertEqual(stats["after_max_bits"], 1)

    def test_boolean_only_and_chunk_boundaries(self):
        data = (
            b"(primo-conflicts (version 2) (logic QF_LRA) (signature) (cnf"
            + b" (clause false)" * 10000
            + b") (conflicts))\n"
        )
        result, stats = self.rewrite(data)
        self.assertEqual(result, data)
        self.assertEqual(stats["conflicts"], 0)
        lexer = Lexer(io.BytesIO(b" " * 65535 + b"abcdef)"))
        self.assertEqual(lexer.next(), ("abcdef", 65535, 65541))
        self.assertEqual(lexer.next(), (")", 65541, 65542))

    def test_malformed_or_invalid_original_rejected(self):
        prefix = (
            b"(primo-conflicts (version 2) (logic QF_LRA) (signature) (cnf) (conflicts "
        )
        for suffix in (
            b"(conflict-lra ((< 0 0) 0))))",
            b"(conflict-lra ((< 0 0) (- 1)))))",
            b"(conflict-lra ((< 0 0) 1)))) trailing",
            b"(conflict-lra",
        ):
            with self.assertRaises(ValueError):
                self.rewrite(prefix + suffix)
        with self.assertRaises(ValueError):
            self.rewrite(prefix.replace(b"QF_LRA", b"QF_LIA") + b"))")

    def test_external_model_cannot_override_process_failure_or_arithmetic(self):
        premises = [({"x": F(1)}, F(0), False), ({"x": F(-1)}, F(-1), False)]
        for state, output, reason in (
            ("error", "sat\n((w0 1)(w1 1))", "error"),
            ("timeout", "sat\n((w0 1)(w1 1))", "timeout"),
            ("ok", "sat\n((w0 1)(w1 2))", "invalid_witness"),
            ("ok", "sat\n((w0 1))", "invalid_model"),
        ):
            with tempfile.TemporaryDirectory(dir=self.root) as directory:
                optimizer = Optimizer(args(optimizer="fake"), Path(directory))

                def fake_invoke(command, cwd, log, seconds, output=output, state=state):
                    (cwd / "optimizer.out").write_text(output)
                    return {"state": state, "seconds": 0.01}

                with patch("minimize_lra_coefficients.invoke", fake_invoke):
                    self.assertEqual(
                        optimizer.improve(0, premises, [F(100)] * 2), [F(100)] * 2
                    )
                self.assertEqual(optimizer.counts[reason], 1)

    def test_cli_publication_and_relative_optimizer_paths(self):
        script = (
            Path(__file__).resolve().parents[2] / "scripts/minimize_lra_coefficients.py"
        )
        source, output = self.root / "input.sexp", self.root / "output.sexp"
        output.write_text("preserve previous artifact")
        source.write_text("invalid input")
        result = subprocess.run(
            [sys.executable, str(script), str(source), str(output)],
            capture_output=True,
            check=False,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_text(), "preserve previous artifact")
        source.write_text(
            "(primo-conflicts (version 2) (logic QF_LRA) (signature) (cnf) "
            "(conflicts (conflict-lra ((< 0 0) 1000))))"
        )
        backend = self.root / "fake-optimizer"
        backend.write_text("#!/bin/sh\nprintf 'sat\\n((w0 1))\\n'\n")
        backend.chmod(0o755)
        result = subprocess.run(
            [
                sys.executable,
                str(script),
                "input.sexp",
                "output.sexp",
                "--optimizer",
                "./fake-optimizer",
                "--min-bits",
                "0",
                "--problems-dir",
                "problems",
            ],
            cwd=self.root,
            capture_output=True,
            check=False,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("((< 0 0) 1)", output.read_text())
        self.assertEqual(json.loads(result.stdout)["optimizer"]["ok"], 1)


if __name__ == "__main__":
    unittest.main()
