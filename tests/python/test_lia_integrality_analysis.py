"""Check attribution and acceptance boundaries of the integrality result analysis."""

import csv
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.qualification.lia_integrality import analyze


def make_parent(directory: Path, status: str = "ok") -> dict:
    parent = directory / "shard-0"
    parent.mkdir(parents=True)
    row = {
        "file": "a.smt2",
        "family": "A",
        "status": status,
        "answer": "sat" if status == "ok" else "unsupported",
        "expected": "sat",
        "wrong": "false",
        "seconds": "1",
    }
    answers = parent / "answers.csv"
    with answers.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(row))
        writer.writeheader()
        writer.writerow(row)
    provenance = parent / "provenance.json"
    provenance.write_text(
        json.dumps(
            {
                "status": "complete",
                "inputs_unchanged": True,
                "inputs": {"a.smt2": "a" * 64},
                "solver": {"sha256": "b" * 64},
            }
        )
    )
    output = parent / "model-validation"
    output.mkdir()
    output.joinpath("replay-provenance.json").write_text(
        json.dumps(
            {
                "parent_provenance_sha256": hashlib.sha256(
                    provenance.read_bytes()
                ).hexdigest(),
                "parent_answers_sha256": hashlib.sha256(
                    answers.read_bytes()
                ).hexdigest(),
            }
        )
    )
    selected = int(status == "ok")
    output.joinpath("coverage.json").write_text(
        json.dumps(
            {
                "status": "complete",
                "expected_sat": selected,
                "completed": selected,
                "dolmen_pass": selected,
                "outcomes": {"pass": selected} if selected else {},
            }
        )
    )
    return {"a.smt2": row}


class LiaIntegralityAnalysisTests(unittest.TestCase):
    def test_model_coverage_rejects_changed_parent_files(self) -> None:
        for filename in ("answers.csv", "provenance.json"):
            with (
                self.subTest(filename=filename),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                rows = make_parent(root)
                self.assertTrue(analyze.models(root, rows)["complete"])
                parent = root / "shard-0" / filename
                # Changing only whitespace preserves the parsed rows and SAT count.
                # The old model coverage must still be rejected for these new bytes.
                parent.write_bytes(parent.read_bytes() + b"\n")
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    analyze.models(root, rows)

    def test_model_coverage_rejects_wrong_per_shard_sat_selection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rows = make_parent(root)
            coverage = root / "shard-0/model-validation/coverage.json"
            data = json.loads(coverage.read_text())
            data["expected_sat"] = 2
            coverage.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "SAT selection differs"):
                analyze.models(root, rows)

    def test_unsupported_rows_prevent_success_even_without_sat_models(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            make_parent(root / "reference")
            make_parent(root / "all", "unsupported")
            make_parent(root / "m124", "unsupported")
            output = root / "summary.json"
            argv = [
                "analyze",
                "--root",
                str(root),
                "--reference-root",
                str(root / "reference"),
                "--expected-files",
                "1",
                "--out",
                str(output),
            ]
            with patch.object(sys, "argv", argv):
                self.assertEqual(analyze.main(), 1)
            report = json.loads(output.read_text())
            self.assertTrue(report["models"]["all"]["complete"])
            self.assertEqual(report["runs"]["all"]["unsupported"], 1)
            self.assertEqual(report["runs"]["all"]["error"], 0)


if __name__ == "__main__":
    unittest.main()
