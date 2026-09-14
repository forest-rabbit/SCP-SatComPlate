"""Historical maintenance impact is broader than the set of later-faulted tasks."""
from pathlib import Path
import runpy
import unittest

AUDIT = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                          'integration/regression/audit-checkpoint-maintenance.py'))
RUN = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                        'integration/regression/run-checkpoint-maintenance.py'))


class MaintenanceAuditTest(unittest.TestCase):
    def test_no_later_fault_is_not_equivalence(self):
        self.assertEqual(AUDIT['classify']([{'fault_after_pause': False}], False),
                         'RERUN_FULL_SCENARIO')

    def test_changed_capture_reservation_also_counts(self):
        self.assertEqual(AUDIT['classify']([], True), 'RERUN_FULL_SCENARIO')

    def test_missing_trajectory_proof_is_unknown(self):
        self.assertEqual(AUDIT['classify']([], False), 'UNKNOWN_REQUIRES_VALIDATION')

    def test_only_output_paths_are_ignored_in_invocation_comparison(self):
        self.assertEqual(RUN['normalized']('satcompute --outputDir=a --faultTrace=b --randomRun=11'),
                         ['satcompute', '--randomRun=11'])
        self.assertNotEqual(RUN['normalized']('satcompute --randomRun=11'),
                            RUN['normalized']('satcompute --randomRun=12'))


if __name__ == '__main__':
    unittest.main()
