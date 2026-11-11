"""Pure metric validation; no solver or checker execution."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from profile_checker_medium import TIME_FORMAT, TIMED_PIPE, time_metrics


class ProfileMetricsTests(unittest.TestCase):
    def test_time_reports_distinguish_cpu_from_pipe_elapsed_time(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "time"
            path.write_text(
                TIME_FORMAT.replace("%U", "3.25")
                .replace("%S", "0.75")
                .replace("%e", "15.00")
                .replace("%M", "12345")
                .replace("%w", "12")
                .replace("%c", "4")
                .replace("%I", "0")
                .replace("%O", "8")
                .replace("%x", "0")
            )
            values = time_metrics(path)
            self.assertEqual(values["user_seconds"] + values["system_seconds"], 4)
            self.assertEqual(values["elapsed_seconds"], 15)
            self.assertEqual(values["max_rss_kib"], 12345)

    def test_incomplete_nonfinite_or_negative_metrics_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "time"
            for data in ("", "user_seconds=0", "user_seconds=nan", "user_seconds=-1"):
                path.write_text(data)
                with self.assertRaises(ValueError):
                    time_metrics(path)

    def test_generated_shell_is_syntactically_valid(self):
        import subprocess

        subprocess.run(["bash", "-n"], input=TIMED_PIPE, text=True, check=True)


if __name__ == "__main__":
    unittest.main()
