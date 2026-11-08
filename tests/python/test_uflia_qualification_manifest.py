from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.qualification.uflia_equalities import run


class QualificationManifestTests(unittest.TestCase):
    def test_relative_corpus_root_keeps_manifest_and_output_paths_consistent(self) -> None:
        # Exercise the actual runner without launching a solver or scheduler.
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory).resolve()
            corpus = scratch / "tmp/QF_UFLIA"
            source = corpus / "family/input.smt2"
            source.parent.mkdir(parents=True)
            source.write_text("(set-logic QF_UFLIA)\n(set-info :status sat)\n(check-sat)\n")
            (scratch / "manifest.txt").write_text("# Selected input\nfamily/input.smt2\n")
            for name in ("baseline", "candidate"):
                (scratch / name).write_text(name)

            def answer(command: list[str], path: Path, timeout: float) -> dict:
                self.assertEqual(path, source)
                return {"status": "ok", "answer": "sat", "seconds": 0.01,
                        "exit_code": 0, "stdout": "sat\n", "stderr": ""}

            argv = ["run.py", "--root", "tmp/QF_UFLIA", "--manifest", "manifest.txt",
                    "--baseline", "baseline", "--candidate", "candidate",
                    "--baseline-git", "fixture", "--candidate-git", "fixture",
                    "--out", "out", "--shards", "1", "--shard", "0"]
            previous = Path.cwd()
            try:
                os.chdir(scratch)
                with (patch("sys.argv", argv),
                      patch.object(run.subprocess, "check_output", return_value="git: fixture\n"),
                      patch.object(run, "run_solver", side_effect=answer) as solver,
                      contextlib.redirect_stdout(io.StringIO())):
                    run.main()
            finally:
                os.chdir(previous)

            self.assertEqual(solver.call_count, 4)
            provenance = json.loads((scratch / "out/provenance.json").read_text())
            self.assertEqual(list(provenance["inputs"]), ["family/input.smt2"])
            rows = [json.loads(line) for line in (scratch / "out/rows.jsonl").read_text().splitlines()]
            self.assertEqual([row["file"] for row in rows], ["family/input.smt2"] * 4)
            self.assertTrue((scratch / "out/complete").is_file())


if __name__ == "__main__":
    unittest.main()
