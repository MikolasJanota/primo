import unittest

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
