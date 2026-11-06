"""Stress admission and LIA envelope transport must preserve their boundaries."""

import hashlib
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from arithmetic_certificate_stress import lia_header_copy, select_cases


class StressBoundaryTests(unittest.TestCase):
    def test_header_copy_preserves_body_and_native_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, target = root / "native", root / "copy"
            body = b"  (signature (declare-fun |name\n  (version 3)\ncontinued| () Int)))\n"
            original = b"(primo-conflicts\n  (version 3)\n  (logic QF_LIA)\n" + body
            source.write_bytes(original)
            metadata = lia_header_copy(source, target)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(target.read_bytes()[29:], original[29:])
            self.assertEqual(
                [
                    i
                    for i, pair in enumerate(zip(original, target.read_bytes()))
                    if pair[0] != pair[1]
                ],
                [28],
            )
            self.assertEqual(
                metadata["copy_sha256"], hashlib.sha256(target.read_bytes()).hexdigest()
            )

    def test_wrong_header_writes_no_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, target = root / "source", root / "target"
            source.write_bytes(b"(primo-conflicts\n  (version 2)\n  (logic QF_LRA)\n")
            with self.assertRaises(ValueError):
                lia_header_copy(source, target)
            self.assertFalse(target.exists())

    def test_selection_balances_tiers_and_caps_families(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            records = []
            for tier, size in enumerate((5000, 40000, 150000)):
                for family in range(6):
                    relative = f"f{family}/t{tier}.smt2"
                    path = root / relative
                    path.parent.mkdir(exist_ok=True)
                    source = f"; {relative}\n(set-logic QF_LIA)(declare-fun x () Int)(assert false)(check-sat)\n"
                    path.write_text(source + ";" + "x" * (size - len(source) - 1))
                    records.append(
                        {
                            "file": relative,
                            "family": f"f{family}",
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                            "historical_seconds": 0.1,
                        }
                    )
            selection = select_cases(records, root, 12, 2)
            self.assertEqual(len(selection["cases"]), 12)
            self.assertEqual(set(selection["tier_selected"]), {0, 1, 2})
            self.assertTrue(
                all(
                    n <= 2
                    for n in Counter(
                        row["family"] for row in selection["cases"]
                    ).values()
                )
            )
            self.assertEqual(len({row["sha256"] for row in selection["cases"]}), 12)
            self.assertEqual(
                [row["size_tier"] for row in selection["cases"]],
                sorted(row["size_tier"] for row in selection["cases"]),
            )


if __name__ == "__main__":
    unittest.main()
