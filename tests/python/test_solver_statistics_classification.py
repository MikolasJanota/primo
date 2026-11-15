"""Protocol regressions for diagnostics whose names overlap printed statistics."""

import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "scripts" / (name + ".py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MODULES = [
    load(n)
    for n in [
        "fuzz_external",
        "qf_uflia_fuzz",
        "qf_uflra_smoke_fuzz",
        "check_solver_seed_corpus",
    ]
]


def invoke(module):
    if module.__name__ == "check_solver_seed_corpus":
        return module.run(["fake"], Path("case.smt2"), 1)
    if module.__name__ == "fuzz_external":
        return module.run_solver(["fake"], Path("case.smt2"), 1)
    return module.run_solver(["fake"], 1)


class StatisticClassificationTests(unittest.TestCase):
    def test_answer_with_unsupported_statistics_is_not_unsupported(self):
        for module in MODULES:
            for answer in ["sat", "unsat", "unknown"]:
                with self.subTest(adapter=module.__name__, answer=answer):
                    result = subprocess.CompletedProcess(
                        ["fake"],
                        0,
                        answer + "\n",
                        ":combination.model.fallback-unsupported 0\n  :other.unsupported 7\n",
                    )
                    with patch.object(module.subprocess, "run", return_value=result):
                        self.assertEqual(invoke(module), (answer, 0))

    def test_actual_unsupported_response_remains_unsupported(self):
        for module in MODULES:
            with self.subTest(adapter=module.__name__):
                result = subprocess.CompletedProcess(
                    ["fake"], 2, '(error "unsupported logic")\n', ""
                )
                with patch.object(module.subprocess, "run", return_value=result):
                    answer, code = invoke(module)
                self.assertIn("unsupported", answer.lower())
                self.assertEqual(code, 2)

    def test_nonzero_exit_after_answer_is_crash_even_with_unsupported_text(self):
        for module in MODULES:
            for answer in ["sat", "unsat"]:
                with self.subTest(adapter=module.__name__, answer=answer):
                    result = subprocess.CompletedProcess(
                        ["fake"],
                        2,
                        answer + "\n",
                        ":combination.model.fallback-unsupported 0\nunsupported later failure\n",
                    )
                    with patch.object(module.subprocess, "run", return_value=result):
                        self.assertEqual(
                            invoke(module),
                            (
                                "crash"
                                if module.__name__ == "check_solver_seed_corpus"
                                else "CRASH",
                                2,
                            ),
                        )


if __name__ == "__main__":
    unittest.main()
