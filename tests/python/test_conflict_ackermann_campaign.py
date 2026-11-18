#!/usr/bin/env python3
"""Check campaign isolation and held-array dependency validation."""

import argparse
import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.conflict_ackermann.prepare import round_robin
from scripts.qualification.conflict_ackermann.submit import submit


class ConflictAckermannCampaignTests(unittest.TestCase):
    def test_family_rotation_is_deterministic_and_fails_when_exhausted(self):
        self.assertEqual(round_robin({"b": [3, 4], "a": [1, 2]}, 4), [1, 3, 2, 4])
        with self.assertRaises(AssertionError):
            round_robin({"a": [1]}, 2)

    def exercise_submission(
        self,
        bad_dependency=False,
        parents="37",
        kind="afterok",
        grouped=False,
        suffix="",
    ):
        calls = []

        def run(command):
            calls.append(command)
            c = command[-1]
            if "\nsbatch " in c:
                self.assertIn("--dependency=" + kind + ":" + parents, c)
                self.assertIn("--array=0-5%6", c)
                return "91"
            if c.startswith("scontrol show job "):
                job = c.split()[-1]
                actual = "99" if bad_dependency and job == "91" else parents
                dependency = (
                    kind + ":" + actual
                    if grouped
                    else ",".join(
                        kind + ":" + p + suffix + "(unfulfilled)"
                        for p in actual.split(":")
                    )
                )
                return f"ArrayJobId={job} Reason=JobHeldUser Dependency={dependency}"
            return ""

        with tempfile.TemporaryDirectory() as d:
            a = argparse.Namespace(
                ssh_config=Path("/cfg"),
                remote="remote",
                snapshot=Path("/snapshot"),
                workdir="/work",
                environment="/env",
                batch_script="/job",
                phase="seeds",
                count=6,
                concurrency=6,
                after=parents,
                dependency_kind=kind,
                job_time="00:30:00",
                exports={"PRIMO_ACK_PHASE": "seeds"},
                receipt=Path(d) / "r.json",
            )
            if bad_dependency:
                with self.assertRaises(AssertionError):
                    submit(a, run)
                self.assertEqual(calls[-1][-1], "scancel 91")
                self.assertFalse(
                    any(c[-1].startswith("scontrol release ") for c in calls)
                )
            else:
                self.assertEqual(submit(a, run), "91")
                self.assertEqual(json.loads(a.receipt.read_text())["parent"], parents)
                self.assertEqual(calls[-1][-1], "scontrol release 91")
            self.assertEqual(sum("--check-only" in c for c in calls), 1)

    def test_model_array_waits_for_the_whole_parent(self):
        self.exercise_submission()

    def test_model_array_waits_for_all_repeated_parents(self):
        self.exercise_submission(parents="37:38")

    def test_grouped_multi_parent_dependencies_are_verified(self):
        self.exercise_submission(parents="37:38", grouped=True)

    def test_scheduler_whole_array_wildcards_preserve_global_barrier(self):
        self.exercise_submission(parents="37:38", kind="afterany", suffix="_*")

    def test_final_audit_runs_after_all_parent_outcomes(self):
        self.exercise_submission(parents="37:38", kind="afterany")

    def test_missing_multi_parent_dependency_never_releases_models(self):
        self.exercise_submission(True, parents="37:38")

    def test_bad_dependency_cancels_the_held_job(self):
        self.exercise_submission(True)


if __name__ == "__main__":
    unittest.main()
