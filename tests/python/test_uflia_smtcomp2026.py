#!/usr/bin/env python3
"""Metadata/harness checks only; these tests never execute Primo or a reference."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.uflia_smtcomp2026.report import metrics, paired, solved
from scripts.qualification.uflia_smtcomp2026.run import run_solver, validate_result


class CompetitionHarnessTests(unittest.TestCase):
    def test_answer_before_abnormal_exit_remains_error(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            fake = root / 'fake.py'
            fake.write_text("print('sat', flush=True)\nraise SystemExit(7)\n")
            source = root / 'input.smt2'
            source.write_text('(check-sat)\n')
            row = run_solver([sys.executable, str(fake)], source, 2)
            self.assertEqual(row['exit_code'], 7)
            self.assertEqual(row['status'], 'error')
            with self.assertRaises(ValueError):
                validate_result(row, 'sat')

    def test_contradictions_rejected_and_known_memory_out_unsolved(self):
        row = {'status': 'ok', 'answer': 'sat', 'timeout': 'false', 'exit_code': 0}
        with self.assertRaises(ValueError):
            validate_result(row, 'unsat')
        row.update(status='error', exit_code=2, stderr_head='error: std::bad_alloc')
        validate_result(row, 'unsat')

    def test_resource_unknown_and_post_cutoff_rows_do_not_count_as_solves(self):
        ok = {'status': 'ok', 'answer': 'sat', 'timeout': 'false', 'exit_code': 0, 'seconds': '0.3'}
        unknown = {**ok, 'answer': 'unknown'}
        exhausted = {**ok, 'status': 'error', 'exit_code': 2, 'memory_out': True}
        self.assertFalse(solved(unknown))
        self.assertFalse(solved(exhausted))
        self.assertFalse(solved({**ok, 'seconds': '120.001'}))
        self.assertFalse(solved({**ok, 'exit_code': 7}))
        result = metrics([ok, unknown, exhausted])
        self.assertEqual(result['solved'], 1)
        self.assertEqual(result['unknown'], 1)
        self.assertEqual(result['memory_out'], 1)
        self.assertEqual(result['unexplained_errors'], 0)
        self.assertAlmostEqual(result['par2_sum'], 480.3)
        before = [{**ok, 'file': 'case'}]
        after = [{**unknown, 'file': 'case'}]
        self.assertEqual(paired(before, after)['losses'], ['case'])

    def test_selection_is_fixed_and_producer_defaults_are_explicit(self):
        path = Path(__file__).resolve().parents[2] / 'benchmarks/uflia-smtcomp2026-20261111/selection.json'
        manifest = json.loads(path.read_text())
        names = [row['file'] for row in manifest['cases']]
        self.assertEqual(len(names), 300)
        self.assertEqual(len(set(names)), 300)
        self.assertEqual(names, sorted(names))
        self.assertEqual(manifest['profiles'], ['off', 'legacy-all', 'model-all', 'legacy-fixed', 'model-fixed'])
        self.assertTrue(all(len(row['sha256']) == 64 for row in manifest['cases']))


if __name__ == '__main__':
    unittest.main()
