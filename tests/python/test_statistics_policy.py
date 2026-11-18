#!/usr/bin/env python3
"""Exercise the statistics boundary scanner without executing a solver."""

import tempfile
import unittest
from pathlib import Path

from scripts.check_statistics_policy import calls, check


class StatisticsPolicyTests(unittest.TestCase):
    def test_typed_keys_and_comments(self):
        self.assertEqual(
            list(
                calls(
                    'stats.increment(util::StatKey::SatConflicts); // stats.set(":bad",1)'
                )
            ),
            [],
        )

    def test_multiline_and_concatenated_keys(self):
        self.assertEqual(
            list(calls('stats_.increment(\n std::string(":prefix.") + suffix, 2);')),
            ['increment(std::string(":prefix.")+suffix)'],
        )

    def test_generic_receiver_with_literal_name(self):
        self.assertEqual(list(calls('counter->set(":fixed", 1);')), ['set(":fixed")'])

    def test_indirect_helper_is_not_exempt(self):
        self.assertEqual(list(calls("statistics.increment(key);")), ["increment(key)"])

    def test_nested_argument_is_complete(self):
        self.assertEqual(
            list(calls("statistics.set(name(index, other), count);")),
            ["set(name(index,other))"],
        )

    def test_explicit_dynamic_api_through_arbitrary_alias(self):
        self.assertEqual(
            list(calls("sink.increment_dynamic(name);")), ["increment_dynamic(name)"]
        )
        self.assertEqual(
            list(calls("util::StatKey key; sink.set_dynamic(key, 1);")),
            ["set_dynamic(key)"],
        )

    def test_statistics_accessor_is_not_exempt(self):
        self.assertEqual(
            list(calls("context.statistics().increment(key);")), ["increment(key)"]
        )

    def test_manifest_count_and_stale_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            source = root / "src/a.cpp"
            source.write_text("stats.set_dynamic(name, 1);")
            entry = {
                "call": "src/a.cpp:set_dynamic(name)",
                "count": 1,
                "reason": "query summary",
            }
            self.assertEqual(check(root, [entry]), [])
            source.write_text("stats.set_dynamic(name, 1); stats.set_dynamic(name, 2);")
            self.assertTrue(check(root, [entry]))
            source.write_text("")
            self.assertTrue(check(root, [entry]))
            self.assertIn("duplicate manifest entry", check(root, [entry, entry]))


if __name__ == "__main__":
    unittest.main()
