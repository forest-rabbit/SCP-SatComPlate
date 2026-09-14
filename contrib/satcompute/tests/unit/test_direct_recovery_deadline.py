import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'integration/regression/audit-direct-recovery-deadline.py'
SPEC = importlib.util.spec_from_file_location('deadline_audit', SCRIPT)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class DirectDeadlineAuditTest(unittest.TestCase):
    def record(self, redo='30', tail=''):
        return dict(recovery_rate_wu_per_s='1000000000', actual_work_units='40',
                    recovery_accept_time_ns='100', original_deadline_ns='200',
                    estimated_remote_redo_ns=redo, estimated_tail_ns=tail)

    def test_post_catchup_and_inclusive_boundary(self):
        result = AUDIT.direct_estimate(self.record('40'), 100)
        self.assertEqual(result['post_catchup_ns'], 60)
        self.assertTrue(result['redo_fits'])
        self.assertFalse(AUDIT.direct_estimate(self.record('41'), 100)['redo_fits'])

    def test_missing_tail_is_not_zero(self):
        result = AUDIT.direct_estimate(self.record('41'), 100)
        self.assertIsNone(result['tail_estimate_ns'])
        self.assertFalse(result['tail_fits'])

    def test_one_path_can_fit(self):
        result = AUDIT.direct_estimate(self.record('41', '35'), 100)
        self.assertTrue(result['tail_fits'])
        self.assertFalse(result['redo_fits'])

    def test_no_actual_outcome_used(self):
        record = self.record()
        before = AUDIT.direct_estimate(record, 100)
        record.update(actual_T_catch_ns='99999999', terminal_state='FAILED', input_received_time_ns='999')
        self.assertEqual(before, AUDIT.direct_estimate(record, 100))

    def test_duration_ceil_and_zero(self):
        self.assertEqual(AUDIT.duration(0, 3), 0)
        self.assertEqual(AUDIT.duration(1, 3), 333333334)

    def test_rerun_normalizes_only_evidence_paths(self):
        import runpy
        run = runpy.run_path(str(SCRIPT.with_name('run-recovery-deadline-reruns.py')))
        normalize = run['normalized']
        self.assertEqual(normalize('satcompute --randomRun=11 --outputDir=a --faultTrace=b'),
                         normalize('satcompute --randomRun=11 --outputDir=c --faultTrace=d'))
        self.assertNotEqual(normalize('satcompute --randomRun=11'),
                            normalize('satcompute --randomRun=12'))


if __name__ == '__main__':
    unittest.main()
