#!/usr/bin/env python3
"""Exercise the statistics boundary scanner without executing a solver."""

import unittest

from scripts.check_statistics_policy import calls


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


if __name__ == "__main__":
    unittest.main()
