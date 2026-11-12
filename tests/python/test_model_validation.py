#!/usr/bin/env python3

from __future__ import annotations

import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[2] / "scripts" / "validate_qfuf_models_with_dolmen.py"
SPEC = importlib.util.spec_from_file_location(
    "validate_qfuf_models_with_dolmen", SCRIPT
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def solver_model() -> tuple[str, int, str, str, float]:
    return "done", 0, "sat\n(model\n  (define-fun x () Real 0)\n)\n", "", 0.01


class ModelValidationTests(unittest.TestCase):
    def test_resource_and_validator_failures_never_become_model_errors(self) -> None:
        cases = [
            ("done", 2, "space limit", "dolmen-resource-limit"),
            ("done", 125, "Dolmen_loop.Alarm.Out_of_space", "dolmen-resource-limit"),
            ("done", 125, "internal bug", "validator-error"),
            ("done", 1, "generic error", "validator-error"),
            ("done", -9, "", "validator-error"),
            ("timeout", None, "", "dolmen-timeout"),
            ("done", 6, "assertion falsified", "model-error"),
        ]
        for status, code, message, expected in cases:
            with (
                self.subTest(expected=expected, code=code),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                case = root / "case.smt2"
                self.write_case(case)
                result, out = self.run_main(
                    ["--dolmen-size=8G", "--out", str(root / "out"), str(case)],
                    [solver_model(), (status, code, "", message, 0.01)],
                )
                self.assertEqual(result, 1)
                with (out / "summary.csv").open() as stream:
                    row = next(csv.DictReader(stream))
                self.assertEqual(row["outcome"], expected)
                self.assertEqual(row["validator"], "dolmen")
                self.assertEqual(
                    json.loads((out / "metadata.json").read_text())["dolmen_size"], "8G"
                )

    def test_input_probe_uses_same_budget_and_retains_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            out = root / "out"
            with (
                mock.patch.object(
                    sys,
                    "argv",
                    [str(SCRIPT), "--dolmen-size=8G", "--out", str(out), str(case)],
                ),
                mock.patch.object(
                    MODULE.shutil, "which", side_effect=lambda name: f"/tools/{name}"
                ),
                mock.patch.object(
                    MODULE,
                    "run_command",
                    side_effect=[
                        solver_model(),
                        ("done", 4, "response diagnostic", "response typing", 0.01),
                        ("done", 2, "probe diagnostic", "probe memory limit", 0.01),
                    ],
                ) as run,
            ):
                self.assertEqual(MODULE.main(), 1)
                for call in run.call_args_list[1:]:
                    self.assertIn("--size=8G", call.args[0])
            with (out / "summary.csv").open() as stream:
                self.assertEqual(
                    next(csv.DictReader(stream))["outcome"], "dolmen-resource-limit"
                )
            logs = "\n".join(path.read_text() for path in out.rglob("*.stderr"))
            self.assertIn("probe memory limit", logs)

    def test_command_adapter_preserves_assertions_and_places_model_before_exit(
        self,
    ) -> None:
        assertion = '(assert (= |x;()| "a;()""b"))'
        source = (
            "; (get-model) is only a comment\n"
            "(set-option :produce-models false)\n"
            "(set-option :print-success true)\n"
            '(set-info :source "text; (exit)")\n'
            + assertion
            + "\n(check-sat)\n(get-model)\n(exit)\n"
        )
        adapted = MODULE.solver_problem_text(source)
        heads = [head for _, _, head in MODULE.command_spans(adapted)]
        self.assertEqual(heads[-3:], [("check-sat",), ("get-model",), ("exit",)])
        self.assertEqual(sum(head[0] == "get-model" for head in heads), 1)
        self.assertIn(assertion, adapted)
        self.assertNotIn(":produce-models false", adapted)
        self.assertNotIn(":print-success true", adapted)
        problem_heads = [
            head[0]
            for _, _, head in MODULE.command_spans(MODULE.model_problem_text(source))
        ]
        self.assertNotIn("get-model", problem_heads)

    def test_adapter_rejects_ambiguous_queries_and_malformed_syntax(self) -> None:
        for source in [
            "(check-sat)(check-sat)",
            "(push 1)(check-sat)",
            "(check-sat-assuming ())",
            "(exit)(check-sat)",
            "(check-sat)(assert false)",
            '(set-info :source "bad)',
            "(check-sat",
            "(check-sat))",
        ]:
            with self.subTest(source=source), self.assertRaises(ValueError):
                MODULE.solver_problem_text(source)

    def test_adapter_is_iterative_for_deep_assertions(self) -> None:
        assertion = "(assert " + "(not " * 10000 + "true" + ")" * 10000 + ")"
        self.assertIn(assertion, MODULE.solver_problem_text(assertion + "(check-sat)"))

    def test_expected_sat_rejects_unsat_and_post_answer_errors(self) -> None:
        cases = [
            (0, "unsat\n", "answer-changed"),
            (9, "sat\n", "solver-error"),
            (0, 'sat\n(error "failure")\n', "solver-error"),
        ]
        for code, stdout, expected in cases:
            with (
                self.subTest(expected=expected),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                case = root / "case.smt2"
                self.write_case(case)
                result, out = self.run_main(
                    ["--expect-sat", "--out", str(root / "out"), str(case)],
                    [("done", code, stdout, "", 0.01)],
                )
                self.assertEqual(result, 1)
                with (out / "summary.csv").open() as stream:
                    self.assertEqual(next(csv.DictReader(stream))["outcome"], expected)

    def test_refused_get_model_after_unsat_is_not_a_solver_error(self) -> None:
        refusal = 'unsat\n(error "get-model requires a current SAT model")\n'
        message = "get-model requires a current SAT model\n"
        cases = [
            # Primo's actual shape: error on stdout, message on stderr, exit 2.
            (2, refusal, message, "not-sat", 0),
            (0, refusal, "", "not-sat", 0),
            (1, refusal, refusal.splitlines()[1] + "\n", "not-sat", 0),
            # Anything beyond that exact shape stays a solver error.
            (-6, refusal, message, "solver-error", 1),
            (
                2,
                refusal,
                message + "AddressSanitizer: heap-use-after-free\n",
                "solver-error",
                1,
            ),
            (2, refusal + '(error "second")\n', message, "solver-error", 1),
            (2, 'unsat\n(model)\n(error "x")\n', "x\n", "solver-error", 1),
            (2, '(error "x")\nunsat\n', "x\n", "solver-error", 1),
            (2, "unsat\n", "", "solver-error", 1),
            (2, "unsat\n(error)\n", "", "solver-error", 1),
            (2, 'unsat\n(error "")\n', "", "solver-error", 1),
            (2, 'sat\n(error "get-model failed")\n', "", "solver-error", 1),
        ]
        for code, stdout, stderr, expected, failures in cases:
            with (
                self.subTest(code=code, stdout=stdout, stderr=stderr),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                case = root / "case.smt2"
                self.write_case(case)
                result, out = self.run_main(
                    ["--out", str(root / "out"), str(case)],
                    [("done", code, stdout, stderr, 0.01)],
                )
                self.assertEqual(result, failures)
                with (out / "summary.csv").open() as stream:
                    row = next(csv.DictReader(stream))
                self.assertEqual(row["outcome"], expected)
                self.assertEqual(row["solver_exit"], str(code))
                if expected == "not-sat":
                    self.assertEqual(
                        row["message"], "no model expected; get-model refused"
                    )

    def test_refused_get_model_keeps_unknown_and_replay_semantics(self) -> None:
        cases = [
            # An unknown answer stays solver-unknown, which the gates judge.
            ([], 'unknown\n(error "no model")\n', "solver-unknown"),
            # A replay expected to stay SAT still fails on unsat.
            (["--expect-sat"], 'unsat\n(error "no model")\n', "answer-changed"),
        ]
        for extra, stdout, expected in cases:
            with (
                self.subTest(expected=expected),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                case = root / "case.smt2"
                self.write_case(case)
                result, out = self.run_main(
                    [*extra, "--out", str(root / "out"), str(case)],
                    [("done", 2, stdout, "no model\n", 0.01)],
                )
                self.assertEqual(result, 1)
                with (out / "summary.csv").open() as stream:
                    self.assertEqual(next(csv.DictReader(stream))["outcome"], expected)

    def test_refusal_shape_check_is_exact(self) -> None:
        accept = MODULE.refused_model_after_non_sat
        self.assertTrue(accept("unsat", 2, 'unsat\n(error "m")\n', "m\n"))
        self.assertTrue(accept("unknown", 2, 'unknown\n(error "m")\n', ""))
        self.assertFalse(accept("sat", 2, 'sat\n(error "m")\n', ""))
        self.assertFalse(accept("unsat", None, 'unsat\n(error "m")\n', ""))
        self.assertFalse(accept("unsat", -11, 'unsat\n(error "m")\n', ""))
        self.assertFalse(accept("unsat", 2, 'unsat\n(error "m")\n', "other\n"))
        self.assertFalse(accept("unsat", 2, 'unsat\n(error "a"b")\n', ""))
        # A resource-limit reason before the refusal is accepted after unknown
        # only, once, and only from the allowlist.
        limit = "integer branch-and-bound node limit\n"
        self.assertTrue(accept("unknown", 2, 'unknown\n(error "m")\n', limit + "m\n"))
        self.assertFalse(accept("unsat", 2, 'unsat\n(error "m")\n', limit + "m\n"))
        self.assertFalse(
            accept(
                "unknown",
                2,
                'unknown\n(error "m")\n',
                limit + "theory conflict limit reached\n" + "m\n",
            )
        )
        self.assertFalse(
            accept(
                "unknown",
                2,
                'unknown\n(error "m")\n',
                "LIA model omitted a shared Int term\nm\n",
            )
        )

    def test_resource_limit_unknown_is_solver_unknown_with_its_reason(self) -> None:
        # The shape of fuzzsmt_447507__file_288 in the 2026-11-04 QF_LIA gate.
        stdout = 'unknown\n(error "get-model requires a current SAT model")\n'
        cases = [
            (
                (
                    "integer branch-and-bound node limit\n"
                    "get-model requires a current SAT model\n"
                ),
                "solver-unknown",
                "; reason: integer branch-and-bound node limit",
            ),
            (
                "stale theory model snapshot\nget-model requires a current SAT model\n",
                "solver-error",
                None,
            ),
        ]
        for stderr, expected, suffix in cases:
            with (
                self.subTest(expected=expected),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                case = root / "case.smt2"
                self.write_case(case)
                result, out = self.run_main(
                    ["--out", str(root / "out"), str(case)],
                    [("done", 2, stdout, stderr, 0.01)],
                )
                self.assertEqual(result, 1)
                with (out / "summary.csv").open() as stream:
                    row = next(csv.DictReader(stream))
                self.assertEqual(row["outcome"], expected)
                if suffix is not None:
                    self.assertTrue(row["message"].endswith(suffix), row["message"])

    def test_hybrid_split_atom_limit_preserves_resource_unknown(self) -> None:
        stdout = 'unknown\n(error "get-model requires a current SAT model")\n'
        stderr = (
            "integer split atom limit\n"
            "get-model requires a current SAT model\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--out", str(root / "out"), str(case)],
                [("done", 2, stdout, stderr, 0.01)],
            )
            self.assertEqual(result, 1)
            with (out / "summary.csv").open() as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(row["answer"], "unknown")
            self.assertEqual(row["outcome"], "solver-unknown")
            self.assertEqual(row["solver_exit"], "2")
            self.assertTrue(
                row["message"].endswith("; reason: integer split atom limit"),
                row["message"],
            )

    def test_unsupported_query_is_recorded_without_launching_solver(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            case.write_text("(check-sat)(check-sat)")
            result, out = self.run_main(["--out", str(root / "out"), str(case)], [])
            self.assertEqual(result, 1)
            with (out / "summary.csv").open() as stream:
                self.assertEqual(
                    next(csv.DictReader(stream))["outcome"], "input-unsupported"
                )

    def write_case(self, path: Path) -> None:
        path.write_text(
            """(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun p (Real) Bool)
(assert (and (= x 0) (p x)))
(check-sat)
""",
            encoding="utf-8",
        )

    def run_main(
        self, arguments: list[str], side_effect: list[object]
    ) -> tuple[int, Path]:
        out = Path(arguments[arguments.index("--out") + 1])
        argv = [str(SCRIPT), *arguments]
        with (
            mock.patch.object(sys, "argv", argv),
            mock.patch.object(
                MODULE.shutil, "which", side_effect=lambda name: f"/tools/{name}"
            ),
            mock.patch.object(MODULE, "run_command", side_effect=side_effect),
        ):
            result = MODULE.main()
        return result, out

    def test_completed_rows_survive_interruption(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first.smt2"
            second = root / "second.smt2"
            self.write_case(first)
            self.write_case(second)
            out = root / "out"
            argv = [str(SCRIPT), "--out", str(out), str(first), str(second)]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    MODULE.shutil, "which", side_effect=lambda name: f"/tools/{name}"
                ),
                mock.patch.object(
                    MODULE,
                    "run_command",
                    side_effect=[
                        solver_model(),
                        ("done", 0, "", "", 0.01),
                        RuntimeError("stop"),
                    ],
                ),
                self.assertRaisesRegex(RuntimeError, "stop"),
            ):
                MODULE.main()

            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["outcome"], "pass")
            self.assertEqual(rows[0]["validator"], "dolmen")
            metadata = json.loads((out / "metadata.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["status"], "running")
            self.assertEqual(metadata["count"], 2)

    def test_records_z3_fallback_without_overwriting_dolmen_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--out", str(root / "out"), str(case)],
                [
                    solver_model(),
                    ("done", 4, "", "dolmen rejected response", 0.01),
                    ("done", 0, "", "", 0.01),  # Problem alone is well-formed.
                    ("done", 0, "sat\n", "", 0.01),
                ],
            )
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["validator"], "z3-fallback")
            self.assertEqual(row["dolmen_exit"], "4")
            self.assertEqual(row["z3_exit"], "0")

    def test_z3_only_path_records_empty_dolmen_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            out = root / "out"
            argv = [str(SCRIPT), "--out", str(out), str(case)]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    MODULE.shutil,
                    "which",
                    side_effect=lambda name: (
                        None if "dolmen" in name else f"/tools/{name}"
                    ),
                ),
                mock.patch.object(
                    MODULE,
                    "run_command",
                    side_effect=[solver_model(), ("done", 0, "sat\n", "", 0.01)],
                ),
            ):
                result = MODULE.main()
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["validator"], "z3-only")
            self.assertEqual(row["dolmen_exit"], "")
            self.assertEqual(row["z3_exit"], "0")

    def test_required_dolmen_pass_rejects_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--require-dolmen-pass", "--out", str(root / "out"), str(case)],
                [
                    solver_model(),
                    ("done", 4, "", "dolmen rejected response", 0.01),
                    ("done", 0, "", "", 0.01),  # Problem alone is well-formed.
                    ("done", 0, "sat\n", "", 0.01),
                ],
            )
            self.assertEqual(result, 1)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["outcome"], "fallback-pass")
            self.assertEqual(row["validator"], "z3-fallback")

    def test_unknown_is_not_counted_as_unsat(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--out", str(root / "out"), str(case)],
                [("done", 0, "unknown\n", "", 0.01)],
            )
            self.assertEqual(result, 1)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["outcome"], "solver-unknown")

    def test_manifest_root_and_sharding_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = root / "cases"
            cases.mkdir()
            for index in range(4):
                self.write_case(cases / f"case-{index}.smt2")
            manifest = root / "manifest.txt"
            manifest.write_text(
                "\n".join(f"cases/case-{index}.smt2" for index in range(4)) + "\n",
                encoding="utf-8",
            )
            result, out = self.run_main(
                [
                    "--manifest",
                    str(manifest),
                    "--root",
                    str(root),
                    "--shards",
                    "2",
                    "--shard-index",
                    "1",
                    "--out",
                    str(root / "out"),
                ],
                [solver_model(), ("done", 0, "", "", 0.01)] * 2,
            )
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(
                [Path(row["file"]).name for row in rows], ["case-1.smt2", "case-3.smt2"]
            )
            metadata = json.loads((out / "metadata.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["status"], "complete")
            self.assertEqual(metadata["completed"], 2)

    def test_fuzz_summarizer_reports_model_validation_separately(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            model = root / "task-0" / "model-validation" / "general"
            model.mkdir(parents=True)
            (model / "summary.csv").write_text(
                "file,answer,outcome,validator\ncase.smt2,sat,pass,dolmen\n",
                encoding="utf-8",
            )
            (model / "metadata.json").write_text(
                json.dumps({"status": "complete", "count": 1}), encoding="utf-8"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(
                        Path(__file__).parents[2]
                        / "scripts"
                        / "summarize_fuzz_results.py"
                    ),
                    str(root),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("model-validation: summaries=1 cases=1", result.stdout)
            self.assertIn("validators={'dolmen': 1}", result.stdout)

    def test_fuzz_summarizer_recognizes_standalone_model_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / "shard-0"
            shard.mkdir()
            (shard / "summary.csv").write_text(
                "file,answer,outcome,validator,dolmen_exit\n"
                "sat.smt2,sat,pass,dolmen,0\n"
                "unsat.smt2,unsat,not-sat,,\n",
                encoding="utf-8",
            )
            (shard / "metadata.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "count": 2,
                        "require_dolmen_pass": True,
                    }
                ),
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(
                        Path(__file__).parents[2]
                        / "scripts"
                        / "summarize_fuzz_results.py"
                    ),
                    str(root),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("model-validation: summaries=1 cases=2", result.stdout)
            self.assertIn("{'pass': 1, 'not-sat': 1}", result.stdout)


if __name__ == "__main__":
    unittest.main()
