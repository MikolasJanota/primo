#!/usr/bin/env python3
"""Check frozen parameter recipes and qualification dependency isolation."""

import argparse
import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.lia_tuning.prepare import POLICIES, flags, tasks
from scripts.qualification.lia_tuning.submit import submit


class PilotTests(unittest.TestCase):
    def test_unique_budgets_and_row_contexts(self):
        self.assertEqual(len(POLICIES), 8)
        self.assertEqual(len(tasks()), 32)
        self.assertEqual(len({tuple(flags(p, 0)) for p in POLICIES}), 8)
        self.assertEqual(
            sum(
                mode == "hybrid" and factor == 0
                for mode, factor, _ in POLICIES.values()
            ),
            1,
        )
        for p in POLICIES:
            off, on = flags(p, 0), flags(p, 1)
            self.assertNotIn("--preprocess-bound-tightening", on)
            self.assertIn("--lia-split-atom-limit=0", off)
            self.assertEqual([x for x in on if "row-propagation" not in x], off)

    def test_qualification_waits_for_all_seed_tasks(self):
        calls = []
        ids = iter(["100", "371", "900"])

        def run(command):
            calls.append(command)
            cmd = command[-1]
            if "\nsbatch " in cmd:
                return next(ids)
            if cmd.startswith("scontrol show job "):
                job = cmd.split()[-1]
                dep = "(null)" if job == "100" else "afterok:100(unfulfilled)"
                return f"ArrayJobId={job} Reason=JobHeldUser Dependency={dep}"
            return ""

        with tempfile.TemporaryDirectory() as d:
            a = argparse.Namespace(
                ssh_config=Path("/config"),
                remote="remote",
                snapshot=Path("/snapshot"),
                workdir="/work",
                environment="/env",
                campaign="/campaign",
                receipt=Path(d) / "receipt.json",
            )
            self.assertEqual(submit(a, run), ["100", "371", "900"])
            receipt = json.loads(a.receipt.read_text())
            self.assertEqual(
                [r["parent"] for r in receipt["jobs"]], [None, "100", "100"]
            )
        first = next(
            i for i, c in enumerate(calls) if c[-1].startswith("scontrol release")
        )
        self.assertEqual(
            sum(c[-1].startswith("scontrol show job ") for c in calls[:first]), 3
        )
        self.assertEqual(sum("--check-only" in c for c in calls), 3)


if __name__ == "__main__":
    unittest.main()
