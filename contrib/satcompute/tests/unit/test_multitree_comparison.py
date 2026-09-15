"""Pure comparison identity and actual replica catch reconstruction."""
from pathlib import Path
import runpy
import unittest

TESTS = Path(__file__).resolve().parents[1]
RUNNER = runpy.run_path(str(TESTS/'integration/regression/run-multitree-comparison.py'))
AUDIT = runpy.run_path(str(TESTS/'support/protection/multitree_comparison_audit.py'))


class MultiTreeComparisonTests(unittest.TestCase):
    def test_six_profiles_share_the_same_non_protection_scene(self):
        controls = {'protectionScheme', 'backupStorageBytesPerNode'}
        common = None
        for name in RUNNER['GROUPS']:
            argv = RUNNER['arguments'](Path('/tmp/same-output'), name)
            flags = dict(t.removeprefix('--').split('=', 1) for t in argv[1:])
            self.assertEqual(len(flags), len(argv)-1)
            scene = {k: v for k, v in flags.items() if k not in controls and not k.startswith('compfrr')}
            if common is None: common = scene
            self.assertEqual(scene, common)
            self.assertEqual(flags['simulationDuration'], '1300')
            self.assertEqual(flags['randomRun'], '11')
            self.assertEqual(flags['faultMode'], 'generate')
            if name.startswith('compfrr'):
                self.assertEqual(flags['compfrrInputPolicy'], 'selective')
                self.assertEqual(flags['compfrrRecoveryPolicy'], 'relocate')
                self.assertEqual('compfrrPressureModel' in flags, name == 'compfrr-p')
            else:
                self.assertFalse(any(k.startswith('compfrr') and k != 'compfrr-shadow' for k in flags))

    def attempt(self, **changes):
        return dict(dict(takeover_time_ns='1000000000', compute_start_time_ns='500000000',
            rate_wu_per_s='100', actual_service_ns='2000000000', actual_work_units='200'), **changes)

    def test_replica_catch_is_observed_service_not_takeover_or_zero_fill(self):
        f = AUDIT['replica_catch_ns']
        # Already ahead at fault: true zero catch. Behind: needs real catchup.
        self.assertEqual(f(10**9, 30, self.attempt()), 0)
        self.assertEqual(f(10**9, 100, self.attempt()), 500000000)
        # No surviving/promoted replica or insufficient executed prefix is missing.
        self.assertIsNone(f(10**9, 100, None))
        self.assertIsNone(f(10**9, 100, self.attempt(takeover_time_ns='')))
        self.assertIsNone(f(10**9, 100, self.attempt(compute_start_time_ns='')))
        self.assertIsNone(f(10**9, 100, self.attempt(actual_service_ns='100000000', actual_work_units='10')))
        with self.assertRaises((AssertionError, ValueError, RuntimeError)):
            f(10**9, 100, self.attempt(takeover_time_ns='1'))


if __name__ == '__main__':
    unittest.main()
