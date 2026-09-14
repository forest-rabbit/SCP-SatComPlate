"""A refactor gate must reject semantic, schema and missing-evidence differences."""
import json
from pathlib import Path
import runpy
import tempfile
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                        'integration/regression/run-protection-equivalence.py'))


class ProtectionEquivalenceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='scp-equivalence-test-')
        self.addCleanup(self.tmp.cleanup)
        self.left, self.right = (Path(self.tmp.name) / x for x in ('left', 'right'))
        self.left.mkdir()
        self.right.mkdir()
        for directory in (self.left, self.right):
            (directory / 'events.csv').write_text('task_id,time_ns,bytes\n1,42,123\n')
            (directory / 'state.json').write_text(json.dumps(
                dict(output=str(directory), wall_clock_ns=42, fault_ns=17)))

    def test_exact_csv_and_nonsemantic_json_location(self):
        result = API['compare'](self.left, self.right)
        self.assertEqual((result['files'], result['csv']), (2, 1))

    def test_actual_bytes_or_event_time_difference_is_rejected(self):
        for row in ('1,42,124', '1,43,123'):
            (self.right / 'events.csv').write_text('task_id,time_ns,bytes\n'+row+'\n')
            with self.assertRaisesRegex(AssertionError, 'SEMANTIC_DIFFERENCE'):
                API['compare'](self.left, self.right)

    def test_schema_and_missing_evidence_are_rejected(self):
        (self.right / 'events.csv').write_text('task_id,bytes,time_ns\n1,123,42\n')
        with self.assertRaises(AssertionError):
            API['compare'](self.left, self.right)
        (self.right / 'extra.csv').write_text('field\nvalue\n')
        with self.assertRaisesRegex(AssertionError, 'output set differs'):
            API['compare'](self.left, self.right)

    def test_simulation_time_is_not_wall_clock(self):
        (self.right / 'state.json').write_text(json.dumps(
            dict(output=str(self.right), wall_clock_ns=900, fault_ns=18)))
        with self.assertRaisesRegex(AssertionError, 'SEMANTIC_DIFFERENCE'):
            API['compare'](self.left, self.right)

    def test_collect_never_overwrites_existing_evidence(self):
        with self.assertRaisesRegex(ValueError, 'preserve evidence'):
            API['collect'](self.left, 1)


if __name__ == '__main__':
    unittest.main()
