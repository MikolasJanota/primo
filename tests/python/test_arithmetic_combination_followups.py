#!/usr/bin/env python3
"""Keep diagnostic repeats linked to the original flags and profile identities."""

import argparse
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.arithmetic_combinations.followups import (
    analyze,
    configure,
    targets,
)
from scripts.qualification.bound_propagation.analyze import sha
from scripts.qualification.lia_hybrid.run import measurement_order


class CombinationFollowups(unittest.TestCase):
    def test_flag_deduplication_and_exact_rotated_pair(self):
        report = {
            "native_rows": 14364,
            "source": "frozen",
            "native_sha256": "binary",
            "summaries": {
                logic: {"edges": {}}
                for logic in ("QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA")
            },
        }
        report["summaries"]["QF_LIA"]["edges"]["B->P"] = {
            "regression_followups": [{"slot": 0}],
            "stable_losses": [0],
        }
        self.assertEqual(len(targets(report)), 1)
        self.assertEqual(len(targets(report)[0]["reasons"]), 2)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = {
                "cases": [
                    {
                        "logic": "QF_LIA",
                        "file": "case.smt2",
                        "sha256": "input",
                        "repetition": 0,
                    }
                ],
                "profiles": {
                    p: {"args": [p], "git": "frozen", "sha256": "binary"}
                    for p in ("B", "P", "R")
                },
                "timeout": 120,
                "address_space_bytes": 8589934592,
            }
            config = root / "integers-0.json"
            config.write_text(json.dumps(original))
            report["config_sha256"] = {"0/integers": sha(config)}
            (root / "campaign.json").write_text(
                json.dumps(
                    {
                        "git": "frozen",
                        "sha256": "binary",
                        "mapping": [{"cohort": "integers", "slot": 0}],
                    }
                )
            )
            analysis = root / "analysis.json"
            analysis.write_text(json.dumps(report))
            with contextlib.redirect_stdout(io.StringIO()):
                configure(
                    argparse.Namespace(
                        analysis=analysis,
                        configs=root,
                        out=root / "repeats",
                        results_root="/results",
                        remote_configs="/configs",
                    )
                )
            orders = []
            for rep in range(3):
                value = json.loads((root / f"repeats/case-0-{rep}.json").read_text())
                self.assertEqual(
                    value["profiles"], {p: original["profiles"][p] for p in ("B", "P")}
                )
                self.assertEqual(value["cases"][0]["sha256"], "input")
                self.assertEqual(value["cases"][0]["repetition"], rep + 3)
                self.assertEqual(value["timeout"], 120)
                orders.append(measurement_order(value, 0))
            self.assertEqual(orders, [["P", "B"], ["B", "P"], ["P", "B"]])
            manifest = json.loads((root / "repeats/manifest.json").read_text())
            self.assertEqual(manifest["timing_rows"], 6)
            self.assertEqual(manifest["parent_analysis_sha256"], sha(analysis))
            self.assertEqual(
                manifest["targets"][0]["original_config_sha256"], sha(config)
            )
            derived = root / "repeats/case-0-0.json"
            self.assertEqual(
                manifest["targets"][0]["derived_config_sha256"]["0"], sha(derived)
            )
            changed = json.loads(derived.read_text())
            changed["profiles"]["P"]["args"].append("--different-policy")
            derived.write_text(json.dumps(changed))
            with self.assertRaisesRegex(
                AssertionError, "follow-up configuration drift"
            ):
                analyze(
                    argparse.Namespace(
                        configs=root / "repeats",
                        rows=root / "missing-results",
                        out=root / "out.json",
                    )
                )


if __name__ == "__main__":
    unittest.main()
