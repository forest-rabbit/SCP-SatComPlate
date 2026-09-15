"""Pure comparison identity and actual replica catch reconstruction."""
from pathlib import Path
import copy
import runpy
import shlex
import unittest

TESTS = Path(__file__).resolve().parents[1]
RUNNER = runpy.run_path(str(TESTS/'integration/regression/run-multitree-comparison.py'))
AUDIT = runpy.run_path(str(TESTS/'support/protection/multitree_comparison_audit.py'))
ROUNDS = runpy.run_path(str(TESTS/'integration/regression/summarize-multitree-rounds.py'))


class MultiTreeComparisonTests(unittest.TestCase):
    def test_bandwidth_contrasts_only_change_link_capacity(self):
        for group in RUNNER['GROUPS']:
            original = RUNNER['arguments'](Path('/tmp/same-output'), group)
            for bandwidth in (1000000000, 10000000000, 100000000000):
                actual = RUNNER['arguments'](Path('/tmp/same-output'), group, isl_bandwidth_bps=bandwidth)
                expected = [f'--islBandwidthBps={bandwidth}' if x == '--islBandwidthBps=10000000000' else x
                            for x in original]
                self.assertEqual(actual, expected)
                self.assertIn('--randomSeed=1', actual)
                self.assertIn('--randomRun=11', actual)
                self.assertIn('--ecmpHashSeed=1', actual)
        for bandwidth, run in ((0, 11), (2000000000, 11), (1000000000, 12), (100000000000, 13)):
            with self.assertRaises(ValueError):
                RUNNER['arguments'](Path('/tmp/same-output'), 'multitree',
                                    random_run=run, isl_bandwidth_bps=bandwidth)

    def test_round_report_rejects_mixed_seed_bandwidth_and_algorithm(self):
        reports = {}
        for label, run in zip('ABC', (11, 12, 13)):
            reports[label] = dict(groups={group: dict(execution=dict(seed=1, run=run, command=[shlex.join(
                RUNNER['arguments'](Path('/tmp')/label/group, group, random_run=run))]))
                for group in RUNNER['GROUPS']})
        ROUNDS['verify_round_identity'](reports)
        for before, after in (('--randomSeed=1', '--randomSeed=2'),
                              ('--ecmpHashSeed=1', '--ecmpHashSeed=2'),
                              ('--islBandwidthBps=10000000000', '--islBandwidthBps=1000000000'),
                              ('--compfrrInputPolicy=selective', '--compfrrInputPolicy=eager')):
            bad = copy.deepcopy(reports)
            execution = bad['B']['groups']['compfrr-p']['execution']
            execution['command'][-1] = execution['command'][-1].replace(before, after)
            with self.assertRaises((ValueError, RuntimeError, AssertionError)):
                ROUNDS['verify_round_identity'](bad)

    def test_rounds_only_change_random_run_not_seeds_or_algorithm(self):
        for group in RUNNER['GROUPS']:
            original = RUNNER['arguments'](Path('/tmp/same-output'), group)
            for run in (11, 12, 13):
                actual = RUNNER['arguments'](Path('/tmp/same-output'), group, random_run=run)
                expected = [f'--randomRun={run}' if x == '--randomRun=11' else x for x in original]
                self.assertEqual(actual, expected)
                self.assertIn('--randomSeed=1', actual)
                self.assertIn('--ecmpHashSeed=1', actual)
        for invalid in (0, 1, 10, 14):
            with self.assertRaises(ValueError):
                RUNNER['arguments'](Path('/tmp/same-output'), 'multitree', random_run=invalid)

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

    def test_paired_faults_require_primary_time_type_and_cause(self):
        equal = AUDIT['same_primary_fault']
        reference = dict(fault_signature=[3, 1000000000, 'compute', True, False])
        self.assertTrue(equal(reference, dict(reference)))
        for signature in ([4, 1000000000, 'compute', True, False],
                          [3, 2000000000, 'compute', True, False],
                          [3, 1000000000, 'compute', False, True],
                          [3, 1000000000, 'satellite', False, False]):
            self.assertFalse(equal(reference, dict(fault_signature=signature)))


if __name__ == '__main__':
    unittest.main()
