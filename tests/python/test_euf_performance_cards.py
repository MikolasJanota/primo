#!/usr/bin/env python3
"""Check milestone routing and counter availability without running a solver."""

import json
import tempfile
import types
import unittest
from pathlib import Path

from scripts.qualification.euf_performance.audit import describe
from scripts.qualification.euf_performance.configure import configure
from scripts.qualification.lia_hybrid.run import measurement_order


class EufPerformanceCardsTests(unittest.TestCase):
    def test_every_card_routes_to_the_frozen_case_and_rotates(self):
        pilot_path = Path("benchmarks/conflict-ackermann-20261117/selection.json")
        controls_path = Path("benchmarks/bound-propagation-20261113/selection.json")
        pilot = json.loads(pilot_path.read_text())["cases"]
        controls = json.loads(controls_path.read_text())["cases"]
        selected = pilot + controls + [c for c in pilot if c["logic"] == "QF_UF"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign.json"
            sources = root / "sources.json"
            campaign.write_text(json.dumps({"roots": {}}))
            sources.write_text(
                json.dumps(
                    {
                        k: {
                            "solver": "binary",
                            "git": "abc",
                            "sha256": "0" * 64,
                            "args": [k],
                        }
                        for k in ("BASE", "MECH", "OFF", "OLDON", "ON", "DEMAND")
                    }
                )
            )
            out = root / "cards"
            configure(
                types.SimpleNamespace(
                    campaign=campaign,
                    sources=sources,
                    pilot=pilot_path,
                    controls=controls_path,
                    out=out,
                    remote_configs="private-configs",
                    results="private-results",
                )
            )
            master = json.loads((out / "master-campaign.json").read_text())
            orders = []
            total = 0
            self.assertEqual(len(master["mapping"]), 817)
            for rep in range(3):
                for entry, case in zip(master["mapping"], selected, strict=True):
                    card = json.loads(
                        (out / f"{entry['cohort']}-{rep}.json").read_text()
                    )
                    self.assertEqual(
                        card["cases"][entry["slot"]], dict(case, repetition=rep)
                    )
                    total += len(card["profiles"])
                first = json.loads((out / f"pilot-case-0-{rep}.json").read_text())
                orders.append(tuple(measurement_order(first, 0)))
            self.assertEqual(total, 5862)
            self.assertEqual(len(set(orders)), 3)

    def test_peaks_and_unavailable_counters_are_not_summed_or_zero_filled(self):
        rows = [
            {
                "status": "ok",
                "exit_code": 0,
                "answer": "sat",
                "seconds": 1.0,
                "peak_rss_kib": 100,
                "stats": {
                    ":euf.conflict-ack.v2.peak-admitted-pairs": str(n),
                    ":euf.incremental.max-use-list-size": str(n),
                    ":euf.conflict-ack.v2.policy-version": "2",
                },
            }
            for n in (2, 4)
        ]
        rows.append(
            {
                "status": "timeout",
                "exit_code": 124,
                "answer": "error",
                "seconds": 30,
                "peak_rss_kib": 200,
                "stats": {":euf.conflict-ack.v2.peak-admitted-pairs": "9"},
            }
        )
        result = describe(rows)
        key = ":euf.conflict-ack.v2.peak-admitted-pairs"
        self.assertEqual(
            result["successful_printed_counters"][key], {"rows": 2, "value": 4}
        )
        self.assertEqual(
            result["incomplete_printed_counters"][key], {"rows": 1, "value": 9}
        )
        self.assertNotIn(
            ":euf.conflict-ack.peak-pairs", result["successful_printed_counters"]
        )
        self.assertEqual(result["observed_policy_versions"], [2])
        self.assertEqual(
            result["successful_printed_counters"][":euf.incremental.max-use-list-size"],
            {"rows": 2, "value": 4},
        )


if __name__ == "__main__":
    unittest.main()
