"""Policy-aware START development-run and additive-audit contracts."""
import json
from pathlib import Path
import runpy
import tempfile
import unittest

TESTS = Path(__file__).resolve().parents[1]
RUN = runpy.run_path(str(TESTS/'integration/regression/run-policy-aware-input-admission-development.py'))
AUDIT = runpy.run_path(str(TESTS/'support/protection/policy_aware_input_admission_audit.py'))


class PolicyAwareInputAdmissionTests(unittest.TestCase):
    def test_development_arguments_are_only_5_and_10_gbps_run11(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            five, receipt = RUN['arguments'](root/'five', 5_000_000_000)
            ten, identity = RUN['arguments'](root/'ten', 10_000_000_000)
            flags = lambda argv: dict(token.removeprefix('--').split('=', 1)
                                      for token in argv[1:])
            f, t = flags(five), flags(ten)
            self.assertEqual((f['randomSeed'], f['randomRun'], f['islBandwidthBps']),
                             ('1', '11', '5000000000'))
            self.assertEqual((t['randomSeed'], t['randomRun'], t['islBandwidthBps']),
                             ('1', '11', '10000000000'))
            self.assertEqual(f['compfrrInputPolicy'], 'selective')
            self.assertEqual(f['compfrrPlacementPolicy'], 'compfrr')
            self.assertEqual(f['compfrrRecoveryPolicy'], 'relocate')
            self.assertEqual(receipt['normalized_arrival_time_ns'], 1024042825747)
            self.assertEqual(identity['normalized_arrival_time_ns'], 1024682825747)
            derived = json.loads(Path(f['taskTrace']).read_text())
            self.assertEqual(next(row['arrival_time_ns'] for row in derived['tasks']
                                  if row['task_id'] == 120), 1024042825747)
        with self.assertRaises((ValueError, RuntimeError, AssertionError)):
            RUN['arguments'](Path('/tmp/not-authorized'), 2_000_000_000)

    def test_policy_audit_preserves_defer_and_classifies_send_rescue(self):
        base = dict(task_id='1', time_ns='2', decision_trigger='TASK_RUNNING',
                    stage='ANCHOR_SEARCH', local='3', remote='4', candidate_index='1',
                    selective_reason='SER_BREAK_EVEN', selective_pf='0.4',
                    selective_u_ser_pot='0.5', selective_t_ser_s='0.2',
                    legacy_fault_input_s='0.2', legacy_frequency_reason='DEADLINE_INFEASIBLE',
                    policy_aware_frequency_reason='START_BENEFICIAL', anchor_remote='4',
                    final_remote='4', is_anchor='1', is_final_pair='0',
                    final_pair_revalidated='0', fault_hit_same_batch='0',
                    start_committed='0', runtime_prefetch_admission_success='')
        send = dict(base, selective_dryrun_decision='SEND',
                    policy_aware_input_admission_s='0', legacy_deadline_feasible='0',
                    policy_aware_deadline_feasible='1', rescued_by_policy_aware_input='1')
        defer = dict(base, task_id='2', selective_dryrun_decision='DEFER',
                     policy_aware_input_admission_s='0.2', legacy_deadline_feasible='0',
                     policy_aware_deadline_feasible='0', rescued_by_policy_aware_input='0')
        result = AUDIT['_policy']([send, defer])
        self.assertEqual(result['unique_SEND_candidate_snapshots'], 1)
        self.assertEqual(result['unique_DEFER_candidate_snapshots'], 1)
        self.assertEqual(result['rescued_candidate_snapshots'], 1)
        broken = dict(defer, policy_aware_input_admission_s='0')
        with self.assertRaises((ValueError, RuntimeError, AssertionError)):
            AUDIT['_policy']([broken])


if __name__ == '__main__':
    unittest.main()
