"""Exercise proof transport and bounds using small synthetic byte producers."""

import gzip
import hashlib
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from capture_micromath_lra import capture


class CaptureTests(unittest.TestCase):
    def run_capture(self, code, **limits):
        directory = Path(self.tmp.name)
        report = capture([sys.executable, "-c", code], directory, directory, **limits)
        data = gzip.decompress((directory / report["gzip_file"]).read_bytes())
        self.assertEqual(len(data), report["raw_bytes"])
        self.assertEqual(hashlib.sha256(data).hexdigest(), report["raw_sha256"])
        return report, data

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_complete_capture_round_trip(self):
        report, data = self.run_capture(
            "import sys;sys.stdout.buffer.write(b'a\\nb\\n'*50000)", seconds=5
        )
        self.assertTrue(report["complete"])
        self.assertEqual(data, b"a\nb\n" * 50000)
        self.assertEqual(report["newline_count"], 100000)
        self.assertEqual(report["gzip_file"], "proof.umd.gz")
        self.assertLess(report["gzip_bytes"], 5000)

    def test_error_output_is_partial(self):
        report, data = self.run_capture(
            "import sys;print('prefix');sys.exit(2)", seconds=5
        )
        self.assertEqual(report["state"], "error")
        self.assertFalse(report["complete"])
        self.assertIn("partial", report["gzip_file"])
        self.assertEqual(data, b"prefix\n")

    def test_raw_cap_leaves_valid_partial_stream(self):
        report, _ = self.run_capture(
            "import sys;sys.stdout.buffer.write(b'x'*200000)",
            seconds=5,
            raw_limit=70000,
        )
        self.assertEqual(report["state"], "raw_limit")
        self.assertFalse(report["complete"])
        self.assertLessEqual(report["raw_bytes"], 70000)

    def test_gzip_cap_is_separate_from_raw_cap(self):
        report, _ = self.run_capture(
            "import sys;sys.stdout.buffer.write(b'x'*200000)",
            seconds=5,
            gzip_limit=65536,
        )
        self.assertEqual(report["state"], "gzip_limit")
        self.assertFalse(report["complete"])
        self.assertLess(report["gzip_bytes"], 65536)

    def test_timeout_kills_descendant_and_finalizes_gzip(self):
        # The child holds stdout open after its parent exits: killing only the
        # direct process would leak the child and leave capture waiting for EOF.
        code = "import os,time;pid=os.fork();open('child.pid','w').write(str(pid)) if pid else time.sleep(30)"
        report, _ = self.run_capture(code, seconds=0.4)
        self.assertEqual(report["state"], "timeout")
        pid = int((Path(self.tmp.name) / "child.pid").read_text())
        status = Path(f"/proc/{pid}/stat")
        for _ in range(100):
            if not status.exists() or status.read_text().split()[2] == "Z":
                break
            time.sleep(0.01)
        self.assertTrue(not status.exists() or status.read_text().split()[2] == "Z")
        self.assertFalse(report["complete"])
        self.assertLess(report["seconds"], 3)


if __name__ == "__main__":
    unittest.main()
