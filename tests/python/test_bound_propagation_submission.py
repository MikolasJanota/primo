import argparse
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.bound_propagation.submit import submit


class SubmissionTests(unittest.TestCase):
    def exercise(self, malformed_dependency=False):
        calls = []
        ids = iter(["100", "371", "900"])

        def run(command):
            calls.append(command)
            text = command[-1]
            if "\nsbatch " in text:
                return next(ids)
            if text.startswith("scontrol show job "):
                job = text.split()[-1]
                parent = {"100": None, "371": "100", "900": "371"}[job]
                if malformed_dependency and job == "371":
                    parent = "101"  # Child task ID instead of the returned array ID.
                dependency = f"afterok:{parent}(unfulfilled)" if parent else "(null)"
                return f"JobId={job} ArrayJobId={job} JobState=PENDING Reason=JobHeldUser Dependency={dependency}"
            return ""

        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(
                ssh_config=Path("/private/config"),
                remote="test",
                workdir="/remote",
                environment="/remote/env",
                configs="/remote/configs",
                snapshot=Path("/snapshot"),
                phase="timing",
                after=None,
                concurrency=32,
                receipt=Path(directory) / "receipt.json",
            )
            if malformed_dependency:
                with self.assertRaises(AssertionError):
                    submit(args, run)
            else:
                self.assertEqual(submit(args, run), ["100", "371", "900"])
        return calls

    def test_nonconsecutive_array_ids_and_release_after_all_checks(self):
        calls = self.exercise()
        batches = [c[-1] for c in calls if "\nsbatch " in c[-1]]
        self.assertNotIn("--dependency", batches[0])
        self.assertIn("--dependency=afterok:100", batches[1])
        self.assertIn("--dependency=afterok:371", batches[2])
        first_release = next(
            i for i, c in enumerate(calls) if c[-1].startswith("scontrol release")
        )
        self.assertEqual(
            sum(c[-1].startswith("scontrol show job") for c in calls[:first_release]), 3
        )

    def test_bad_parent_cancels_all_submissions_without_release(self):
        calls = self.exercise(malformed_dependency=True)
        self.assertFalse(any(c[-1].startswith("scontrol release") for c in calls))
        self.assertEqual(calls[-1][-1], "scancel 100 371")
