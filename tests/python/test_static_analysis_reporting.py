"""Static-analysis reports must distinguish findings from incomplete execution."""

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"scripts/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SUMMARY = load_script("summarize_static_analysis")
HEADERS = load_script("check_static_headers")


class StaticReportingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name) / "source with spaces"
        self.repo.mkdir()
        self.files = ["src/example.cpp", "tests/unit/test.cpp", "src/example.hpp"]
        for name in self.files:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// fixture\n")
        self.manifest = {
            "source_root": str(self.repo),
            "source_sha256": {
                name: hashlib.sha256((self.repo / name).read_bytes()).hexdigest()
                for name in self.files
            },
        }
        self.commands = [
            {
                "file": str(self.repo / name),
                "directory": str(self.repo),
                "arguments": ["c++", "-I", str(self.repo / "src")],
            }
            for name in self.files[:2]
        ]
        self.log = "".join(
            f"/usr/bin/clang-tidy-18 -p database {self.repo / name}\n"
            for name in self.files[:2]
        )

    def test_success_and_complete_coverage_are_required(self):
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log, 0)["status"], "clean"
        )
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log, 1)["status"], "incomplete"
        )
        report = SUMMARY.summarize(self.manifest, self.log.splitlines()[0] + "\n", 0)
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(report["missing"], [self.files[1]])

    def test_header_diagnostics_are_deduplicated_and_visible(self):
        warning = f"{self.repo / self.files[2]}:1:2: warning: questionable copy [performance-test]\n"
        report = SUMMARY.summarize(self.manifest, self.log + warning * 2, 0)
        self.assertEqual(report["status"], "complete-with-findings")
        self.assertEqual(report["unique_diagnostics"], 1)
        self.assertEqual(report["diagnostic_occurrences"], 2)
        self.assertEqual(report["diagnostics"][0]["file"], self.files[2])

    def test_llvm21_progress_prefixes_preserve_coverage(self):
        log = "".join(
            f"[{index:3d}/2][0.8s] {command}\n"
            for index, command in enumerate(self.log.splitlines(), 1)
        )
        report = SUMMARY.summarize(self.manifest, log, 0)
        self.assertEqual(report["status"], "clean")
        self.assertEqual(report["analyzed_translation_units"], 2)

    def test_unbracketed_compiler_errors_are_failures(self):
        error = f"{self.repo / self.files[0]}:1:2: fatal error: missing header\n"
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log + error, 0)["status"],
            "incomplete",
        )

    def test_changed_or_deleted_sources_fail_even_if_tool_succeeded(self):
        (self.repo / self.files[0]).write_text("// changed\n")
        (self.repo / self.files[2]).unlink()
        report = SUMMARY.summarize(self.manifest, self.log, 0)
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(set(report["changed_inputs"]), {self.files[0], self.files[2]})

    def test_changed_generated_revision_header_fails(self):
        header = self.repo / "revision.hpp"
        header.write_text("revision1")
        self.manifest["generated_revision_header"] = {
            "path": str(header),
            "sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        }
        header.write_text("revision2")
        report = SUMMARY.summarize(self.manifest, self.log, 0)
        self.assertEqual(report["status"], "incomplete")

    def test_unknown_source_command_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unrecognized analysis command"):
            SUMMARY.summarize(
                self.manifest, self.log + "clang-tidy-18 /other/source.cpp\n", 0
            )

    def include_report(self, include, *, name="src/example.cpp"):
        source = self.repo / name
        source.write_text(f'#include "{include}"\n')
        return {
            "diagnostics": [
                {
                    "file": name,
                    "line": 1,
                    "column": 1,
                    "check": "misc-include-cleaner",
                    "severity": "warning",
                    "message": "included header fixture.hpp is not used directly",
                }
            ]
        }

    def test_paired_self_include_is_kept(self):
        report = self.include_report("example.hpp")
        gate = SUMMARY.unused_include_gate(report, self.repo, self.commands)
        self.assertEqual(gate["status"], "passed")
        self.assertEqual(len(gate["paired_self_includes"]), 1)

    def test_source_root_qualified_self_include_is_kept(self):
        nested = self.repo / "src/nested"
        nested.mkdir()
        (nested / "owned.hpp").write_text("// fixture\n")
        report = self.include_report("nested/owned.hpp", name="src/nested/owned.cpp")
        commands = [
            {
                "file": str(nested / "owned.cpp"),
                "directory": str(self.repo),
                "arguments": ["c++", "-I", str(self.repo / "src")],
            }
        ]
        gate = SUMMARY.unused_include_gate(report, self.repo, commands)
        self.assertEqual(gate["blocking"], [])
        self.assertEqual(len(gate["paired_self_includes"]), 1)

    def test_unrelated_unused_header_is_blocking(self):
        (self.repo / "src/unrelated.hpp").write_text("// fixture\n")
        report = self.include_report("unrelated.hpp")
        gate = SUMMARY.unused_include_gate(report, self.repo, self.commands)
        self.assertEqual(gate["status"], "failed")
        self.assertEqual(len(gate["blocking"]), 1)

    def test_same_basename_from_other_directory_is_not_self_include(self):
        other = self.repo / "other/example.hpp"
        other.parent.mkdir()
        other.write_text("// fixture\n")
        report = self.include_report("../other/example.hpp")
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, self.commands)["status"],
            "failed",
        )

    def test_source_relative_shadow_cannot_be_exempted_as_paired_header(self):
        nested = self.repo / "src/nested"
        (nested / "nested").mkdir(parents=True)
        (nested / "owned.hpp").write_text("// actual paired header\n")
        (nested / "nested/owned.hpp").write_text("// source-relative shadow\n")
        report = self.include_report("nested/owned.hpp", name="src/nested/owned.cpp")
        commands = [
            {
                "file": str(nested / "owned.cpp"),
                "directory": str(self.repo),
                "arguments": ["c++", "-I", str(self.repo / "src")],
            }
        ]
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, commands)["status"], "failed"
        )

    def test_earlier_quote_search_path_cannot_be_exempted(self):
        nested = self.repo / "src/nested"
        nested.mkdir()
        (nested / "owned.hpp").write_text("// paired header\n")
        shadow = self.repo / "shadow/nested"
        shadow.mkdir(parents=True)
        (shadow / "owned.hpp").write_text("// quote-search shadow\n")
        report = self.include_report("nested/owned.hpp", name="src/nested/owned.cpp")
        commands = [
            {
                "file": str(nested / "owned.cpp"),
                "directory": str(self.repo),
                "arguments": [
                    "c++",
                    "-I",
                    str(self.repo / "src"),
                    "-iquote",
                    str(shadow.parent),
                ],
            }
        ]
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, commands)["status"], "failed"
        )

    def test_unsupported_search_flags_deny_exemption(self):
        report = self.include_report("example.hpp")
        commands = [
            {
                "file": str(self.repo / "src/example.cpp"),
                "directory": str(self.repo),
                "arguments": ["c++", "-I-"],
            }
        ]
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, commands)["status"], "failed"
        )

    def test_ambiguous_user_and_system_search_denies_exemption(self):
        report = self.include_report("example.hpp")
        commands = [
            {
                "file": str(self.repo / "src/example.cpp"),
                "directory": str(self.repo),
                "arguments": [
                    "c++",
                    "-I",
                    str(self.repo / "src"),
                    "-isystem",
                    str(self.repo / "src"),
                ],
            }
        ]
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, commands)["status"], "failed"
        )

    def test_missing_compile_context_denies_self_include_exemption(self):
        report = self.include_report("example.hpp")
        self.assertEqual(
            SUMMARY.unused_include_gate(report, self.repo, [])["status"], "failed"
        )

    def test_missing_direct_include_advice_stays_visible(self):
        report = {
            "diagnostics": [
                {
                    "file": "src/example.cpp",
                    "line": 1,
                    "column": 1,
                    "check": "misc-include-cleaner",
                    "severity": "warning",
                    "message": 'no header providing "std::vector" is directly included',
                }
            ]
        }
        gate = SUMMARY.unused_include_gate(report, self.repo, self.commands)
        self.assertEqual(gate["status"], "passed")
        self.assertEqual(len(report["diagnostics"]), 1)

    def test_cli_propagates_unused_include_gate_failure(self):
        for include, expected_exit in [("example.hpp", 0), ("unrelated.hpp", 1)]:
            with self.subTest(include=include):
                report = self.include_report(include)
                self.manifest["source_sha256"]["src/example.cpp"] = hashlib.sha256(
                    (self.repo / "src/example.cpp").read_bytes()
                ).hexdigest()
                manifest = self.repo / "manifest.json"
                manifest.write_text(json.dumps(self.manifest))
                (self.repo / "compile_commands.json").write_text(
                    json.dumps(self.commands)
                )
                log = self.repo / "tidy.log"
                message = report["diagnostics"][0]["message"]
                log.write_text(
                    self.log + f"{self.repo / 'src/example.cpp'}:1:1: warning: "
                    f"{message} [misc-include-cleaner]\n"
                )
                out = self.repo / "report.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "scripts/summarize_static_analysis.py"),
                        "--manifest",
                        str(manifest),
                        "--log",
                        str(log),
                        "--exit-code",
                        "0",
                        "--reject-unused-includes",
                        "--out",
                        str(out),
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, expected_exit, result.stderr)
                self.assertEqual(json.loads(out.read_text())["tool_exit_code"], 0)

    def test_header_command_preserves_flags_without_object_generation(self):
        source = str(self.repo / self.files[0])
        entry = {
            "directory": str(self.repo),
            "file": source,
            "arguments": [
                "clang++-18",
                "-std=c++20",
                "-Ipath with spaces",
                "-DPRIMO_TEST=1",
                "-o",
                "object.o",
                "-c",
                source,
            ],
        }
        generated = self.repo / "header.cpp"
        command = HEADERS.syntax_command(entry, generated)
        self.assertEqual(
            command,
            [
                "clang++-18",
                "-std=c++20",
                "-Ipath with spaces",
                "-DPRIMO_TEST=1",
                "-fsyntax-only",
                "-x",
                "c++",
                str(generated),
            ],
        )


if __name__ == "__main__":
    unittest.main()
