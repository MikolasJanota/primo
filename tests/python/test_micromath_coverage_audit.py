#!/usr/bin/env python3
"""Coverage accounting must reject mismatched sources and unconfirmed proofs."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SCRIPTS = Path(__file__).parents[2] / "scripts"


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SELECTOR = load("select_micromath_fast_cases")
with mock.patch.dict(sys.modules, {"select_micromath_fast_cases": SELECTOR}):
    AUDIT = load("audit_micromath_coverage")


class CoverageAuditTests(unittest.TestCase):
    def test_kernel_requires_success_and_exact_positive_without_inconsistency(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "kernel.out"
            path.write_text("challenge proven\n")
            AUDIT.confirm_kernel(path, {"state": "ok", "exit_code": 0})
            for stage in [
                {"state": "timeout", "exit_code": 0},
                {"state": "ok", "exit_code": 1},
            ]:
                with self.assertRaises(ValueError):
                    AUDIT.confirm_kernel(path, stage)
            for text in [
                "",
                "not challenge proven\n",
                "Inconsistency proven!\nchallenge proven\n",
            ]:
                path.write_text(text)
                with self.assertRaises(ValueError):
                    AUDIT.confirm_kernel(path, {"state": "ok", "exit_code": 0})

    def test_signature_audit_distinguishes_bool_domain_and_codomain(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            declarations = {
                "plain": "(declare-fun a () Bool)\n(declare-fun f (U) U)",
                "domain": "(declare-fun f (Bool) U)",
                "codomain": "(declare-fun p (U) Bool)",
                "both": "(declare-fun g (Bool) Bool)",
            }
            records = []
            for name, text in declarations.items():
                path = root / f"{name}.smt2"
                path.write_text(text)
                records.append(
                    {
                        "file": path.name,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    }
                )
            selected = [r for r in records if r["file"] == "plain.smt2"]
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"cases": selected}))
            args = SimpleNamespace(
                history=root, source_git="abc", corpus=root, manifest=manifest
            )
            with (
                mock.patch.object(AUDIT, "read_history", return_value=records),
                mock.patch.dict("os.environ", {"SLURM_JOB_ID": "test"}),
            ):
                report = AUDIT.signatures(args)
                self.assertEqual(
                    report["categories"],
                    {
                        "selected": 1,
                        "bool_argument_only": 1,
                        "bool_result_only": 1,
                        "bool_argument_and_result": 1,
                    },
                )
                manifest.write_text(json.dumps({"cases": selected + [records[1]]}))
                with self.assertRaisesRegex(ValueError, "selection mismatch"):
                    AUDIT.signatures(args)
                manifest.write_text(json.dumps({"cases": selected}))
                (root / "domain.smt2").write_text("changed")
                with self.assertRaisesRegex(ValueError, "source hash mismatch"):
                    AUDIT.signatures(args)


if __name__ == "__main__":
    unittest.main()
