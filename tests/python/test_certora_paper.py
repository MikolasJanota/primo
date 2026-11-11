#!/usr/bin/env python3
"""Checks for paper-data semantics; no solver is executed."""
from __future__ import annotations

import unittest

from scripts.qualification.certora_paper.paper import successful


class PaperDataTests(unittest.TestCase):
    def test_wall_clock_cutoff_is_not_normalized_time(self):
        row = ['case', '0', '590', '605', '601', '1', '0', '0', '']
        self.assertFalse(successful(row))
        row[4] = '600'
        self.assertTrue(successful(row))

    def test_post_answer_error_and_unknown_are_not_solves(self):
        row = ['case', '7', '1', '1', '1', '1', '0', '0', '']
        self.assertFalse(successful(row))
        row[1] = '0'
        row[5], row[7] = '0', '1'
        self.assertFalse(successful(row))


if __name__ == '__main__':
    unittest.main()
