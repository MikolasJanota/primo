#!/usr/bin/env python3
"""Check source coverage, input stability, tool success and distinct diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
from collections import Counter
from pathlib import Path

COMMAND = re.compile(
    r"^(?:\[\s*\d+/\d+\]\[[^\]\n]+\]\s+)?(?:\S*/)?clang-tidy(?:-\d+)?\s",
    re.MULTILINE,
)
DIAGNOSTIC = re.compile(
    r"^(.+?):(\d+):(\d+): (warning|error|fatal error): (.*?)(?: \[([^\]]+)\])?$",
    re.MULTILINE,
)


def diagnostics(text: str, repo: Path) -> list[dict]:
    rows = []
    for match in DIAGNOSTIC.finditer(text):
        path, line, column, severity, message, check = match.groups()
        file = Path(path)
        if file.is_relative_to(repo):
            path = str(file.relative_to(repo))
        rows.append(
            {
                "file": path,
                "line": int(line),
                "column": int(column),
                "severity": severity,
                "message": message,
                "check": check or "compiler-diagnostic",
            }
        )
    return rows


def summarize(manifest: dict, text: str, exit_code: int) -> dict:
    repo = Path(manifest["source_root"])
    expected = {name for name in manifest["source_sha256"] if name.endswith(".cpp")}
    changed = [
        name
        for name, digest in manifest["source_sha256"].items()
        if not (repo / name).is_file()
        or hashlib.sha256((repo / name).read_bytes()).hexdigest() != digest
    ]
    generated = manifest.get("generated_revision_header")
    if generated:
        path = Path(generated["path"])
        if (
            not path.is_file()
            or hashlib.sha256(path.read_bytes()).hexdigest() != generated["sha256"]
        ):
            changed.append("generated/primo_git_revision.hpp")
    seen = set()
    for match in COMMAND.finditer(text):
        command = text[match.start() :].splitlines()[0]
        sources = [
            name for name in expected if command.endswith(" " + str(repo / name))
        ]
        if len(sources) != 1:
            raise ValueError(f"unrecognized analysis command: {command}")
        seen.add(sources[0])
    rows = diagnostics(text, repo)
    unique = {tuple(row.items()): row for row in rows}
    errors = [row for row in unique.values() if row["severity"] != "warning"]
    missing = sorted(expected - seen)
    # The LLVM runner reports only a global exit status, not each worker's status.
    # Never override a failed aggregate using a selected successful retry.
    incomplete = bool(missing or errors or changed or exit_code != 0)
    status = (
        "incomplete" if incomplete else "complete-with-findings" if rows else "clean"
    )
    return {
        "status": status,
        "expected_translation_units": len(expected),
        "analyzed_translation_units": len(seen),
        "missing": missing,
        "changed_inputs": changed,
        "tool_exit_code": exit_code,
        "diagnostic_occurrences": len(rows),
        "unique_diagnostics": len(unique),
        "counts": dict(Counter(row["check"] for row in unique.values())),
        "diagnostics": sorted(
            unique.values(),
            key=lambda row: (row["file"], row["line"], row["column"], row["check"]),
        ),
        "scope": "Diagnostic inventory; completed analysis is not a claim that findings are benign.",
    }


def quoted_header(source: Path, include: str, entry: dict) -> Path | None:
    """Resolve supported quoted-include search flags; unknown search rules deny exemption."""
    arguments = entry.get("arguments") or shlex.split(entry.get("command", ""))
    directory = Path(entry.get("directory", source.parent))
    paths = {"-iquote": [], "-I": [], "-isystem": [], "-idirafter": []}
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument == "-I-" or argument.startswith(
            ("-F", "-Wp,", "-Xclang", "-Xpreprocessor", "@", "--config")
        ):
            return None
        matched = False
        for option, directories in paths.items():
            if argument == option:
                index += 1
                if index == len(arguments):
                    return None
                directories.append((directory / arguments[index]).resolve())
                matched = True
                break
            if argument.startswith(option) and len(argument) > len(option):
                directories.append((directory / argument[len(option) :]).resolve())
                matched = True
                break
        if not matched and argument.startswith(("-i", "--sysroot")):
            return None
        index += 1
    # A directory specified as both user and system has special compiler
    # deduplication/precedence rules; deny exemption instead of guessing.
    if set(paths["-I"]) & {*paths["-isystem"], *paths["-idirafter"]}:
        return None
    search = [
        source.parent,
        *paths["-iquote"],
        *paths["-I"],
        *paths["-isystem"],
        *paths["-idirafter"],
    ]
    return next(
        (
            (directory / include).resolve()
            for directory in search
            if (directory / include).is_file()
        ),
        None,
    )


def unused_include_gate(
    report: dict, repo: Path, commands: list[dict] | None = None
) -> dict:
    """Reject redundant includes while retaining each implementation's own header.

    A paired header checks that its declarations compile in isolation. Empty
    implementation anchors intentionally include it even without symbol uses.
    Missing-direct-include advice remains visible and advisory in this gate.
    """
    paired = []
    blocking = []
    for row in report["diagnostics"]:
        if row["check"] != "misc-include-cleaner" or not row["message"].startswith(
            "included header "
        ):
            continue
        source = repo / row["file"]
        lines = source.read_text().splitlines() if source.is_file() else []
        line = lines[row["line"] - 1] if 0 < row["line"] <= len(lines) else ""
        include = re.match(r'^\s*#\s*include\s*"([^"]+)"', line)
        own = False
        if include and source.suffix == ".cpp":
            contexts = [
                entry
                for entry in commands or []
                if Path(entry["file"]).resolve() == source.resolve()
            ]
            paired_paths = (
                source.with_suffix(".hpp").resolve(),
                source.with_suffix(".h").resolve(),
            )
            own = bool(contexts) and all(
                quoted_header(source, include[1], entry) in paired_paths
                for entry in contexts
            )
        (paired if own else blocking).append(row)
    return {
        "status": "failed" if blocking else "passed",
        "blocking": blocking,
        "paired_self_includes": paired,
        "policy": "Retain the implementation's own header; other unused includes fail. Missing direct includes remain advisory.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--exit-code", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reject-unused-includes", action="store_true")
    args = parser.parse_args()
    report = summarize(
        json.loads(args.manifest.read_text()), args.log.read_text(), args.exit_code
    )
    if args.reject_unused_includes:
        database = args.manifest.parent / "compile_commands.json"
        report["unused_include_gate"] = unused_include_gate(
            report,
            Path(json.loads(args.manifest.read_text())["source_root"]),
            json.loads(database.read_text()) if database.is_file() else None,
        )

    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                key: report[key]
                for key in (
                    "status",
                    "analyzed_translation_units",
                    "unique_diagnostics",
                    "counts",
                )
            },
            indent=2,
        )
    )
    if args.reject_unused_includes:
        gate = report["unused_include_gate"]
        print(
            f"Unused include gate: {gate['status']}; {len(gate['blocking'])} blocking, "
            f"{len(gate['paired_self_includes'])} intentional paired headers"
        )
    return int(
        report["status"] == "incomplete"
        or report.get("unused_include_gate", {}).get("status") == "failed"
    )


if __name__ == "__main__":
    raise SystemExit(main())
