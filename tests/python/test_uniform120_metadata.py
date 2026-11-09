"""Metadata guards for reusing references without scheduling their executables."""
import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / 'scripts/qualification/uniform120'
SPEC = importlib.util.spec_from_file_location('uniform_worker', FOLDER / 'campaign.py')
WORKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WORKER)
REPORT_SPEC = importlib.util.spec_from_file_location('uniform_report', FOLDER / 'report.py')
REPORT = importlib.util.module_from_spec(REPORT_SPEC)
with patch.dict(sys.modules, {'campaign': WORKER}), patch.object(sys, 'path', [str(ROOT / 'scripts'), *sys.path]):
    REPORT_SPEC.loader.exec_module(REPORT)


def row(name='a.smt2', **changes):
    return {'file': name, 'family': 'test', 'status': 'ok', 'answer': 'sat', 'seconds': '2',
            'exit_code': '0', 'timeout': 'false', 'wrong': 'false', 'stderr_head': '', **changes}


def write_rows(path, values):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(values[0]))
        writer.writeheader()
        writer.writerows(values)


class UniformMetadataTests(unittest.TestCase):
    def test_deadline_and_post_answer_error_are_unsolved(self):
        rows = [row(seconds='120'), row('b', seconds='120.01'),
                row('c', status='timeout', timeout='true'), row('d', exit_code='1')]
        result = REPORT.metrics(rows, 120)
        self.assertEqual(result['solved'], 1)
        self.assertEqual(result['par2_sum'], 120 + 3 * 240)

    def test_reference_crash_stops_live_campaign(self):
        for reference in [True, False]:
            with self.assertRaisesRegex(ValueError, 'failure'):
                WORKER.strict_rows([row(status='error', exit_code='-11')], [], reference)

    def test_archived_reference_is_censored_without_mutating_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            out, old = Path(directory) / 'new', Path(directory) / 'old'
            WORKER.write(old / 'plan.json', {'tools': {'z3': {'sha256': 'binary'}},
                         'runs': [{'logic': 'QF_UF', 'label': 'z3', 'shards': 1, 'arguments': []}]})
            parent = old / 'timing/QF_UF/z3/shard-0'
            WORKER.write(parent / 'provenance.json', {'status': 'complete', 'inputs_unchanged': True,
                         'timeout': 1200, 'solver': {'sha256': 'binary'}, 'solver_args': [],
                         'inputs': {'a.smt2': 'A', 'b.smt2': 'B', 'c.smt2': 'C'}})
            WORKER.write(parent / 'execution-timing.json', {'memory_mib': 8192,
                         'address_space_limit_bytes': [8192 * 1024**2] * 2, 'partition': 'compute',
                         'cpu_models': ['Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz'],
                         'job': 'old-job', 'array_job': 'old-job', 'task': 0, 'host': 'node-fixture', 'affinity': [0]})
            write_rows(parent / 'answers.csv', [row(), row('b.smt2', seconds='121'),
                                               row('c.smt2', status='error', answer='error', seconds='500', exit_code='1')])
            before = WORKER.digest(parent / 'answers.csv')
            WORKER.write(out / 'inventory/QF_UF.json', {'inputs': {'a.smt2': {'sha256': 'A'}, 'b.smt2': {'sha256': 'B'}, 'c.smt2': {'sha256': 'C'}}})
            config = {'reference_parent_plan_sha256': WORKER.digest(old / 'plan.json'), 'memory_mib': 8192, 'timeout_seconds': 120}
            run = {'logic': 'QF_UF', 'label': 'z3', 'solver': 'z3', 'shards': 1}
            with patch.object(WORKER.subprocess, 'run', side_effect=AssertionError('no reference execution')):
                WORKER.import_references(SimpleNamespace(references=old), config, out, [run])
            self.assertEqual(before, WORKER.digest(parent / 'answers.csv'))
            with (out / 'timing/QF_UF/z3/shard-0/answers.csv').open() as stream:
                derived = list(csv.DictReader(stream))
            self.assertEqual([r['status'] for r in derived], ['ok', 'timeout', 'timeout'])
            self.assertEqual(derived[1]['original_seconds'], '121')
            self.assertEqual(derived[2]['original_status'], 'error')
            original_answers = REPORT.reference_answers(derived)
            self.assertEqual(original_answers[1]['answer'], 'sat')
            with self.assertRaisesRegex(ValueError, 'disagreement'):
                WORKER.strict_rows([row('b.smt2', answer='unsat')], original_answers, False)
            proof = json.loads((out / 'reference-reuse.json').read_text())['QF_UF/z3']
            self.assertEqual(proof['reference_benchmark_executions'], 0)
            # A changed current corpus must not be silently plotted by filename.
            WORKER.write(out / 'inventory/QF_UF.json', {'inputs': {'a.smt2': {'sha256': 'changed'}, 'b.smt2': {'sha256': 'B'}, 'c.smt2': {'sha256': 'C'}}})
            with self.assertRaisesRegex(ValueError, 'input-byte mismatch'):
                WORKER.import_references(SimpleNamespace(references=old), config, out, [run])

    def test_filtered_scores_have_their_own_denominator_and_par2(self):
        with tempfile.TemporaryDirectory() as directory:
            data = {'QF_UF': {
                'primo': {'a': row('a', seconds='0.1'), 'b': row('b', seconds='3')},
                'cvc5': {'a': row('a', seconds='0.2'), 'b': row('b', seconds='0.01')}}}
            report = {'runs': {'QF_UF': {label: REPORT.metrics(list(rows.values()), 120)
                                       for label, rows in data['QF_UF'].items()}}}
            REPORT.plots(Path(directory), {'config': {'timeout_seconds': 120, 'easy_cutoff_seconds': 1}}, data, report)
            filtered = report['filtered']['QF_UF']
            self.assertEqual((filtered['removed_common_easy'], filtered['retained']), (1, 1))
            self.assertEqual(filtered['series']['primo']['par2_sum'], 3)
            self.assertEqual(filtered['series']['cvc5']['par2_sum'], 0.01)
            self.assertEqual(report['runs']['QF_UF']['primo']['inputs'], 2)

    def test_admitted_references_never_enter_execution_tasks(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            WORKER.write(out / 'capabilities.json', {'logics': {'QF_LIA': {'z3': {'supported': True}}}})
            WORKER.write(out / 'tools.json', {})
            WORKER.write(out / 'inventory/QF_LIA.json', {'count': 2, 'inputs': {'a': {}, 'b': {}}})
            config = {'corpora': {'QF_LIA': 2}, 'native_profiles': {'primo': [], 'primo-integer': []},
                      'extra_native_profiles': {'QF_LIA': ['primo-integer']}, 'references': ['z3'],
                      'maximum_inputs_per_shard': 12}
            source = out / 'config.json'
            WORKER.write(source, config)
            with patch.object(WORKER, 'import_references', side_effect=lambda *args: WORKER.write(out / 'reference-reuse.json', {})):
                WORKER.plan(SimpleNamespace(config=source), config, out)
            plan = WORKER.load(out / 'plan.json')
            self.assertEqual(len(plan['runs']), 3)
            self.assertEqual(len(plan['tasks']), 2)
            self.assertTrue(all(t['solver'] == 'primo' for t in plan['tasks']))
            self.assertEqual(plan['reference_reuse_sha256'], WORKER.digest(out / 'reference-reuse.json'))

    def test_percentage_axis_and_solver_colours_are_independent_of_rank(self):
        svg = REPORT.svg([('Primo default', [1]), ('cvc5', [1, 2])], 120, 3, 'test', [],
                         highlighted='Primo default', colour_by_name=REPORT.COLOURS, percentage=True)
        self.assertIn('100%', svg)
        self.assertIn('25%', svg)
        self.assertIn('stroke="#0057b8"', svg)
        self.assertIn('stroke="#009e73"', svg)
        # Percentage tick positions use fractional counts, even for tiny cohorts.
        import xml.etree.ElementTree as ET
        labels = {node.text: float(node.attrib['y']) for node in ET.fromstring(svg).iter()
                  if node.tag.endswith('text') and node.text in {'0%', '25%', '50%', '75%', '100%'}}
        self.assertGreater(labels['0%'], labels['25%'])
        self.assertGreater(labels['25%'], labels['50%'])


if __name__ == '__main__':
    unittest.main()
