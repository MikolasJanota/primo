import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.bound_behavior.analyze import audit_model, digest
from scripts.qualification.bound_behavior.diagnose import append_bounds, expanded_bounds


class BoundDiagnosticTests(unittest.TestCase):
    def test_expands_only_selected_bounds_in_shared_export_dag(self):
        export = """(define-fun bound_probe_1 () Int x)
(define-fun bound_probe_2 () Int 4)
(define-fun bound_probe_3 () Bool (<= bound_probe_1 bound_probe_2))
(define-fun bound_probe_4 () Bool (and bound_probe_3 bound_probe_3))
(assert (not (and true bound_probe_3)))(check-sat)
"""
        self.assertEqual(expanded_bounds(export), ["(<= x 4)"])

    def test_insertion_uses_real_command_boundary_not_comments(self):
        source = (
            "; (check-sat) is only a comment\n(set-logic QF_LIA)\n(check-sat)\n(exit)"
        )
        result = append_bounds(source, ["(<= x 4)"])
        self.assertIn("(assert (<= x 4))\n(check-sat)", result)
        self.assertTrue(result.startswith("; (check-sat)"))
        with self.assertRaises(ValueError):
            append_bounds("(check-sat)(check-sat)", [])


class ModelReceiptAuditTests(unittest.TestCase):
    def test_rejects_changed_adapted_formula_even_with_pass_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "input.smt2"
            source.write_text("(set-logic QF_LIA)(check-sat)")
            row = {"args": ["--stats"], "input_sha256": digest(source)}
            metadata = {
                "status": "complete",
                "completed": 1,
                "solver_args": ["--stats", "--check-model"],
                "require_dolmen_pass": True,
                "expect_sat": True,
                "dolmen_args": ["--strict=false"],
                "outcomes": {"pass": 1},
                "validators": {"dolmen": 1},
            }
            (directory / "metadata.json").write_text(json.dumps(metadata))
            (directory / "summary.csv").write_text(
                "answer,outcome,validator,solver_exit,dolmen_exit,message\n"
                "sat,pass,dolmen,0,0,\n"
            )
            # A success receipt cannot validate a different replay formula.
            (directory / "case.primo-input.smt2").write_text(
                "(assert false)(check-sat)"
            )
            (directory / "case.smt2.smt2").write_text("(check-sat)")
            with self.assertRaises(AssertionError):
                audit_model(directory, row, source)

    def test_rejects_model_replay_with_different_solver_options(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "metadata.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "completed": 1,
                        "solver_args": ["--check-model"],
                    }
                )
            )
            with self.assertRaises(AssertionError):
                audit_model(
                    directory,
                    {"args": ["--preprocess-bound-tightening"]},
                    directory / "input",
                )
