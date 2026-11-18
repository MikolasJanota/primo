#!/usr/bin/env python3
"""Reject unreviewed string-key statistics calls; inventory is not proof of coldness."""

from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path

TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|[A-Za-z_]\w*|::|->|[^\s]', re.DOTALL
)


def calls(text):
    tokens = [
        m.group()
        for m in TOKEN.finditer(text)
        if not m.group().startswith(("//", "/*"))
    ]
    typed = set(
        re.findall(r"(?:StatKey|auto)\s+(kStat\w+)\s*=\s*(?:util::)?StatKey::", text)
    )
    for i, token in enumerate(tokens):
        if (
            token not in {"increment", "set", "get", "contains"}
            or i < 2
            or i + 1 >= len(tokens)
        ):
            continue
        if tokens[i - 1] not in {".", "->"} or tokens[i + 1] != "(":
            continue
        receiver = tokens[i - 2]
        arg = []
        depth = 0
        for part in tokens[i + 2 :]:
            if depth == 0 and part in {",", ")"}:
                break
            depth += part in {"(", "[", "{"}
            depth -= part in {")", "]", "}"}
            arg.append(part)
        if not arg:
            continue
        key = "".join(arg)
        if "StatKey::" in key or key in typed:
            continue
        if "stat" not in receiver.lower() and not any(x.startswith('":') for x in arg):
            continue
        yield token + "(" + key + ")"


def inventory(root):
    result = collections.Counter()
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in {".cpp", ".hpp"}:
            continue
        for call in calls(path.read_text()):
            result[str(path.relative_to(root)) + ":" + call] += 1
    return result


def check(root, manifest):
    observed = inventory(root)
    expected = {row["call"]: row["count"] for row in manifest}
    problems = []
    for row in manifest:
        if not row.get("reason"):
            problems.append("missing rationale: " + row["call"])
    for call in observed.keys() | expected.keys():
        if observed.get(call, 0) != expected.get(call, 0):
            problems.append(
                f"{call}: observed {observed.get(call, 0)}, approved {expected.get(call, 0)}"
            )
    return sorted(problems)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument(
        "--manifest", type=Path, default=Path("scripts/statistics_cold_calls.json")
    )
    a = p.parse_args()
    problems = check(a.root, json.loads(a.manifest.read_text()))
    if problems:
        print("\n".join(problems))
        raise SystemExit(1)
    print(
        "Statistics boundary inventory matches reviewed exceptions; review helper aliases manually."
    )


if __name__ == "__main__":
    main()
