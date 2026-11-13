import copy
import csv
import json
import tempfile
import unittest
from pathlib import Path

from scripts.qualification.bound_propagation.analyze import (
    LABELS,
    audit_configs,
    audit_models,
    audit_row,
    sha,
    summarize,
)


class AnalysisTests(unittest.TestCase):
    def test_rejects_signal_crash_even_with_allocation_diagnostic(self):
        handled = {"status": "error", "exit_code": 2, "memory_out": True}
        audit_row(handled)
        with self.assertRaises(AssertionError):
            audit_row(dict(handled, exit_code=-6))
        audit_row({"status": "timeout", "exit_code": -15})

    def test_configurations_must_match_frozen_selection_and_limits(self):
        selection = {"cases": [{"file": "a.smt2"}]}
        protocol = {
            "inputs": 1,
            "repetitions": 3,
            "time_limit_seconds": 120,
            "address_space_bytes": 8 * 1024**3,
            "profiles": {label: [label] for label in LABELS},
        }
        configs = [
            {
                "cases": [dict(selection["cases"][0], repetition=rep)],
                "timeout": 120,
                "address_space_bytes": 8 * 1024**3,
                "roots": {"QF_LIA": "/corpus"},
                "profiles": {
                    label: {"solver": "/solver", "sha256": "native", "args": [label]}
                    for label in (*LABELS[rep:], *LABELS[:rep])
                },
            }
            for rep in range(3)
        ]
        audit_configs(configs, selection, protocol, "native")
        for key, value in [
            ("timeout", 60),
            ("address_space_bytes", 4 * 1024**3),
            ("cases", [{"file": "other.smt2", "repetition": 1}]),
        ]:
            changed = copy.deepcopy(configs)
            changed[1][key] = value
            with self.subTest(key=key), self.assertRaises(AssertionError):
                audit_configs(changed, selection, protocol, "native")

    def test_replay_must_cover_selected_input_and_exact_profile(self):
        config = {
            "roots": {"QF_LIA": "/corpus"},
            "profiles": {"both": {"solver": "/solver"}},
        }
        case = {"logic": "QF_LIA", "file": "a.smt2"}
        selected = {"both": {"args": ["--quiet", "--preprocess-bound-tightening"]}}
        model = {
            "file": "/corpus/a.smt2",
            "answer": "sat",
            "outcome": "pass",
            "validator": "dolmen",
            "solver_exit": "0",
            "dolmen_exit": "0",
        }
        metadata = {
            "status": "complete",
            "count": 1,
            "completed": 1,
            "solver": "/solver",
            "solver_args": [*selected["both"]["args"], "--check-model"],
            "expect_sat": True,
            "require_dolmen_pass": True,
            "dolmen_args": ["--strict=false"],
            "outcomes": {"pass": 1},
            "validators": {"dolmen": 1},
        }
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            target = directory / "models/both"
            target.mkdir(parents=True)
            summary = target / "summary.csv"
            meta = target / "metadata.json"

            def write(row, replay):
                with summary.open("w") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(row))
                    writer.writeheader()
                    writer.writerow(row)
                meta.write_text(json.dumps(replay))
                (directory / "coverage.json").write_text(
                    json.dumps(
                        {
                            "selected_sat": 1,
                            "direct_dolmen_pass": 1,
                            "rows": [
                                {"profile": "both", "summary_sha256": sha(summary)}
                            ],
                        }
                    )
                )

            self.assertIsNone(audit_models(directory, selected, config, case))
            write(model, metadata)
            self.assertEqual(
                audit_models(directory, selected, config, case), {"both": sha(meta)}
            )
            for row, replay in [
                (dict(model, file="/corpus/other.smt2"), metadata),
                (model, dict(metadata, solver_args=["--quiet", "--check-model"])),
                (model, dict(metadata, status="running")),
            ]:
                write(row, replay)
                with self.assertRaises(AssertionError):
                    audit_models(directory, selected, config, case)
            write(model, metadata)
            (directory / "coverage.json").write_text(
                json.dumps({"selected_sat": 1, "direct_dolmen_pass": 1, "rows": []})
            )
            with self.assertRaises(AssertionError):
                audit_models(directory, selected, config, case)

    def test_interrupted_answer_is_unsolved_and_not_a_paired_runtime(self):
        records = {}
        for rep in range(3):
            for slot in range(2):
                for label in LABELS:
                    records[rep, slot, label] = {
                        "status": "ok",
                        "exit_code": 0,
                        "answer": "sat",
                        "seconds": 2.0 if label == "default" else 1.0,
                        "peak_rss_kib": 1024,
                        "stats": {":lra.theory-propagations": 7},
                    }
            records[rep, 1, "pre"].update(status="timeout", exit_code=-15)
        result = summarize([0, 1], records, 120)
        pre = result["profiles"]["pre"]
        self.assertEqual(pre["solved_counts"], [1, 1, 1])
        self.assertEqual(pre["median_mean_par2_seconds"], 120.5)
        comparison = result["comparisons"]["default->pre"]
        self.assertEqual(comparison["stable_lost_slots"], [1])
        self.assertEqual(comparison["common_solved_all_three"], 1)
        self.assertEqual(comparison["median_of_case_paired_median_ratios"], 0.5)
        self.assertEqual(
            comparison["matched_complete_counter_totals"][":lra.theory-propagations"][
                "matched_complete_rows"
            ],
            3,
        )

    def test_one_repetition_gain_is_not_a_stable_gain(self):
        records = {}
        for rep in range(3):
            for label in LABELS:
                records[rep, 0, label] = {
                    "status": "timeout",
                    "exit_code": -15,
                    "answer": "unknown",
                    "seconds": 120,
                    "peak_rss_kib": 1024,
                    "stats": {},
                }
        records[0, 0, "both"].update(
            status="ok", exit_code=0, answer="unsat", seconds=1
        )
        result = summarize([0], records, 120)
        self.assertEqual(result["profiles"]["both"]["intermittent_solved"], 1)
        self.assertEqual(
            result["comparisons"]["default->both"]["stable_gained_slots"], []
        )
        self.assertEqual(
            result["comparisons"]["default->both"]["repetitions"][0]["gained"], 1
        )
