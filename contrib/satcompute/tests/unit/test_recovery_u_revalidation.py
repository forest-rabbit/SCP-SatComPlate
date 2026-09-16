"""Bounded affected-run matrix and signed, common-cohort sensitivity audit."""
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1] / 'integration/regression'
RUN = runpy.run_path(str(HERE / 'run-recovery-u-revalidation.py'))
AUDIT = runpy.run_path(str(HERE / 'analyze-recovery-u-revalidation.py'))


class RecoveryURevalidationTest(unittest.TestCase):
    def test_scope_includes_both_input_only_run15_groups(self):
        self.assertEqual(RUN['NEW'], {(12, 'full'), (14, 'full'), (12, 'noU'), (14, 'noU'),
            (15, 'noU'), (12, 'rational-U'), (14, 'rational-U'), (15, 'rational-U')})
        for g in RUN['GROUPS']:
            self.assertIn('recovery-deadline-reruns/formal', str(RUN['source'](11, g)))
        self.assertNotIn((15, 'full'), RUN['NEW'])

    def test_runtime_changes_rejected(self):
        with patch.dict(RUN['PREV'], frozen_scope=lambda: None, git=lambda *args: 'contrib/satcompute/para.cc'):
            with self.assertRaises(ValueError):
                RUN['frozen_scope']()
        with patch.dict(RUN['PREV'], frozen_scope=lambda: None, git=lambda *args: 'AGENTS.md'):
            self.assertEqual(RUN['frozen_scope']()['production_base'], RUN['BASE'])

    def test_signs_and_numeric_ties(self):
        values = {'12:1': 4, '11:10': 4, '11:2': 4, '11:3': -9}
        self.assertEqual(AUDIT['signed_largest'](values, True), '11:2')
        self.assertEqual(AUDIT['signed_largest'](values, False), '11:3')
        self.assertIsNone(AUDIT['signed_largest']({'11:1': 0, '11:2': -2}, True))
        self.assertIsNone(AUDIT['signed_largest']({'11:1': 2}, False))

    def test_missing_catch_excluded_but_zero_valid(self):
        def row(identity, catch):
            return dict(task_id=identity, actual_T_catch_ns=catch, fault_time_ns='1', fault_type='compute')
        values = {g: [row('11:1', '100'), row('11:2', '0')] for g in RUN['GROUPS']}
        values['rational-U'][0]['actual_T_catch_ns'] = ''
        self.assertEqual(AUDIT['common_caught'](values), {('11:2', '1', 'compute')})

    def test_both_signed_exclusions_are_symmetric_single_instances(self):
        def task(cost):
            return dict(completed=True, failed=False, **{k: cost for k in AUDIT['MULTI']['FIELDS']})
        def recovery(identity, catch):
            return dict(task_id=identity, fault_time_ns='10', fault_type='compute', actual_T_catch_ns=catch,
                remote_node='1', phase_at_fault='ON', remote_busy_at_fault='0', chosen_path='TAIL',
                terminal_state='COMPLETED', checkpoint_relocation_bytes='0', attempt_generation='1')
        a = {'11:1': task(10), '12:1': task(10), '12:2': task(10)}
        b = {'11:1': task(2), '12:1': task(11), '12:2': task(10)}
        ra = [recovery('11:1', '9000000000'), recovery('12:1', '1000000000'), recovery('12:2', '')]
        rb = [recovery('11:1', '1000000000'), recovery('12:1', '2000000000'), recovery('12:2', '0')]
        accounts = dict(full=a, noU=b, **{'rational-U': b})
        recoveries = dict(full=ra, noU=rb, **{'rational-U': rb})
        caught = AUDIT['common_caught'](recoveries)
        result = AUDIT['exclusions'](accounts, recoveries, ('full', 'rational-U'), caught)
        self.assertEqual([r['selected_instance'] for r in result], ['11:1', '12:1', '11:1', '12:1'])
        for item in result:
            comparison = item['result']
            self.assertEqual(comparison['reference_cohort']['tasks'], 2)
            self.assertEqual(comparison['candidate_cohort']['tasks'], 2)
            self.assertEqual(comparison['paired_catch']['paired_catch_count'], 1)
        self.assertEqual(result[0]['result']['delta_candidate_minus_reference']['w_waste_actual'], 1)


if __name__ == '__main__':
    unittest.main()
