"""Acceptance-data guards; all solver and model rows here are synthetic."""

from __future__ import annotations

import csv
import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.arithmetic_overhead.analyze import (
    audit_shard,
    counter_differences,
    digest,
)


class ArithmeticOverheadAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = {
            "command": ["/solver", "--quiet"],
            "binary": "candidate",
            "binary_sha256": "abc",
        }
        self.inputs = {"a.smt2": {"sha256": "input-hash"}}
        self.row = {
            "file": "a.smt2",
            "status": "ok",
            "answer": "sat",
            "wrong": "false",
            "seconds": ".1",
            "exit_code": "0",
            "timeout": "false",
        }
        self.identity = {
            "status": "complete",
            "inputs_unchanged": True,
            "inputs": {"a.smt2": "input-hash"},
            "solver": {"sha256": "abc"},
            "solver_args": ["--quiet"],
            "timeout": 60,
            "root": "/corpus",
        }
        self.write_parent()

    def write_parent(self):
        (self.root / "provenance.json").write_text(json.dumps(self.identity))
        with (self.root / "answers.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(self.row))
            writer.writeheader()
            writer.writerow(self.row)

    def write_model(self, **overrides):
        path = self.root / "model-validation"
        path.mkdir(exist_ok=True)
        (path / "models").mkdir(exist_ok=True)
        (path / "sat-manifest.txt").write_text("/corpus/a.smt2\n")
        coverage = {
            "status": "complete",
            "expected_sat": 1,
            "dolmen_pass": 1,
            "inputs_and_binary_unchanged": True,
            "provenance": {
                "parent_answers_sha256": digest(self.root / "answers.csv"),
                "parent_provenance_sha256": digest(self.root / "provenance.json"),
                "manifest_sha256": digest(path / "sat-manifest.txt"),
            },
        }
        coverage.update(overrides)
        (path / "coverage.json").write_text(json.dumps(coverage))
        model = {
            "file": "/corpus/a.smt2",
            "answer": "sat",
            "outcome": "pass",
            "validator": "dolmen",
            "solver_exit": "0",
            "dolmen_exit": "0",
        }
        with (path / "models/summary.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(model))
            writer.writeheader()
            writer.writerow(model)
        coverage["artifacts"] = {
            "models/summary.csv": digest(path / "models/summary.csv")
        }
        (path / "coverage.json").write_text(json.dumps(coverage))

    def audit(self, require_models=True):
        return audit_shard(self.root, self.profile, self.inputs, 60, require_models)

    def test_complete_direct_model_rows_are_required(self):
        with self.assertRaisesRegex(ValueError, "incomplete direct SAT coverage"):
            self.audit()
        self.write_model()
        rows, cover = self.audit()
        self.assertEqual(len(rows), 1)
        self.assertEqual(cover["dolmen_pass"], 1)

    def test_post_answer_process_failure_blocks_acceptance(self):
        self.row["exit_code"] = "1"
        self.write_parent()
        with self.assertRaisesRegex(ValueError, "post-answer process error"):
            self.audit(False)

    def test_replay_cannot_be_reused_after_timing_changes(self):
        self.write_model()
        self.row["seconds"] = ".2"
        self.write_parent()
        with self.assertRaisesRegex(ValueError, "different timing data"):
            self.audit()

    def test_input_and_binary_identity_must_match(self):
        self.identity["solver"]["sha256"] = "different"
        self.write_parent()
        with self.assertRaisesRegex(ValueError, "binary identity mismatch"):
            self.audit(False)
        self.identity["solver"]["sha256"] = "abc"
        self.identity["inputs"] = {}
        self.write_parent()
        with self.assertRaisesRegex(ValueError, "input identity mismatch"):
            self.audit(False)

    def test_counter_identity_rejects_lost_interval_requests(self):
        self.row.update(
            {
                ":lia.patch-interval-requests": "10",
                ":lia.patch-interval-computations": "2",
                ":lia.patch-interval-cache-hits": "7",
            }
        )
        self.write_parent()
        with self.assertRaisesRegex(ValueError, "inconsistent interval-work"):
            self.audit(False)

    def test_only_mechanism_work_and_duration_differences_are_exempt(self):
        old = {
            ":lia.branch-nodes": "4",
            ":rational.big-operations": "20",
            ":lia.cube-microseconds": "50",
        }
        new = {
            ":lia.branch-nodes": "5",
            ":rational.big-operations": "10",
            ":lia.cube-microseconds": "20",
            ":lia.patch-interval-cache-hits": "8",
        }
        self.assertEqual(
            counter_differences(old, new), {":lia.branch-nodes": ["4", "5"]}
        )


if __name__ == "__main__":
    unittest.main()
