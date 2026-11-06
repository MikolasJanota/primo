"""The checker statement must contain precisely the exported CNF, never lemmas."""

import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from certificate_cnf_statement import convert_certificate, write_cnf_statement
from check_lra_certificate import parse


class CnfStatementTests(unittest.TestCase):
    def statement(self, body, signature="", conflicts="", logic="QF_LRA"):
        version = 2 if logic == "QF_LRA" else 3
        return f"(primo-conflicts (version {version}) (logic {logic}) (signature {signature}) (cnf {body}) (conflicts {conflicts}))"

    def test_cnf_only_and_empty_clause(self):
        source = self.statement(
            "(clause)(clause true)(clause (not p) (<= x 0))",
            "(declare-fun p () Bool)(declare-fun x () Real)",
            "(conflict-lra ((< x 1234567) 1))",
        )
        output = io.StringIO()
        report = write_cnf_statement(io.StringIO(source), output, "QF_LRA")
        commands = parse("(" + output.getvalue() + ")")
        self.assertEqual(report["clauses"], 3)
        self.assertEqual(
            commands[3:6],
            [
                ["assert", "false"],
                ["assert", ["or", "false", "true"]],
                ["assert", ["or", "false", ["not", "p"], ["<=", "x", "0"]]],
            ],
        )
        self.assertNotIn("1234567", output.getvalue())
        self.assertEqual(commands[-1], ["check-sat"])

    def test_empty_cnf_is_true_without_extra_assertions(self):
        output = io.StringIO()
        write_cnf_statement(io.StringIO(self.statement("")), output, "QF_LRA")
        self.assertNotIn("assert", output.getvalue())

    def test_quoted_identifier_newline_and_comment(self):
        symbol = "|name\n); (assert false)|"
        source = self.statement(
            f"; comment\n(clause (<= {symbol} (- 1)))",
            f"(declare-fun {symbol} () Int)",
            logic="QF_LIA",
        )
        output = io.StringIO()
        write_cnf_statement(io.StringIO(source), output, "QF_LIA")
        commands = parse("(" + output.getvalue() + ")")
        self.assertEqual(len(commands), 4)
        self.assertEqual(commands[1], ["declare-fun", symbol, [], "Int"])
        self.assertEqual(
            commands[2], ["assert", ["or", "false", ["<=", symbol, ["-", "1"]]]]
        )

    def test_streaming_read_never_requests_whole_file(self):
        class BoundedInput(io.StringIO):
            def read(self, size=-1):
                self_test.assertEqual(size, 1)
                return super().read(size)

        self_test = self
        output = io.StringIO()
        write_cnf_statement(
            BoundedInput(self.statement("(clause false)")), output, "QF_LRA"
        )

    def test_invalid_input_does_not_publish_statement(self):
        valid = self.statement("(clause false)")
        for source in [
            valid[:-1],
            valid + "()",
            valid.replace("(signature", "(signature bad", 1),
            valid.replace("QF_LRA", "QF_LIA"),
            valid.replace("(version 2)", "(version 9)"),
            valid.replace("false", '"false"'),
        ]:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                certificate, target = root / "native", root / "statement"
                certificate.write_text(source)
                with self.assertRaises(ValueError):
                    convert_certificate(certificate, target, "QF_LRA")
                self.assertFalse(target.exists())

    def test_existing_statement_is_untouched(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            certificate, target = root / "native", root / "statement"
            certificate.write_text(self.statement("(clause false)"))
            target.write_text("keep me")
            with self.assertRaises(FileExistsError):
                convert_certificate(certificate, target, "QF_LRA")
            self.assertEqual(target.read_text(), "keep me")


if __name__ == "__main__":
    unittest.main()
