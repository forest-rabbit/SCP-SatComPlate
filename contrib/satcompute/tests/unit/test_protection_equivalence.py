"""Narrow owner-path metadata exception; all execution fields remain strict."""
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

    def test_historical_placement_rename_is_narrow_and_opt_in(self):
        old = self.left / 'n5c-placement-decisions.csv'
        new = self.right / 'compfrr-placement-decisions.csv'
        old.write_text('placement,reason,bytes\nn5c,N5C_POST_BATCH_QUOTA,123\n')
        new.write_text('placement,reason,bytes\ncompfrr,COMPFRR_P_POST_BATCH_QUOTA,123\n')
        with self.assertRaisesRegex(AssertionError, 'output set differs'):
            API['compare'](self.left, self.right)
        self.assertTrue(API['compare'](self.left, self.right, allow_placement_rename=True)['placement_rename_only'])
        new.write_text('placement,reason,bytes\ncompfrr,COMPFRR_P_POST_BATCH_QUOTA,124\n')
        with self.assertRaisesRegex(AssertionError, 'SEMANTIC_DIFFERENCE'):
            API['compare'](self.left, self.right, allow_placement_rename=True)



class CbProfileRelocationTest(unittest.TestCase):
    def check(self, after, allow=True, filename='cb-recompute/cb-sat-parameters.json'):
        with tempfile.TemporaryDirectory() as temp:
            before_root, after_root = (Path(temp) / name for name in ('before', 'after'))
            before = dict(profile_path=str(API['ROOT'] / API['CB_PROFILE_OLD']), mtbf_seconds=41.47642679900744)
            for root, value in ((before_root, before), (after_root, after)):
                path = root / filename
                path.parent.mkdir(parents=True)
                path.write_text(json.dumps(value))
            return API['compare'](before_root, after_root, allow_cb_profile_relocation=allow)

    def value(self):
        return dict(profile_path=str(API['ROOT'] / API['CB_PROFILE_NEW']), mtbf_seconds=41.47642679900744)

    def test_owner_metadata_move_is_explicit_and_reported(self):
        with self.assertRaises(AssertionError):
            self.check(self.value(), allow=False)
        self.assertEqual(self.check(self.value())['authorized_cb_profile_path_relocations'],
                         ['cb-recompute/cb-sat-parameters.json'])

    def test_no_other_field_or_file_is_exempt(self):
        for value in (dict(self.value(), mtbf_seconds=42), dict(self.value(), profile_path='/tmp/other'),
                      dict(self.value(), new_field=1)):
            with self.assertRaises(AssertionError):
                self.check(value)
        with self.assertRaises(AssertionError):
            self.check(self.value(), filename='f-eager/other.json')


if __name__ == '__main__':
    unittest.main()
