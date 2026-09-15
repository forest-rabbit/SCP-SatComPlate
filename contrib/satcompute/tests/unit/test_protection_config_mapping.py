"""Historical experiment identity is independent of renamed configuration owners."""
from itertools import product
from pathlib import Path
import runpy
import json
import shlex
import tempfile
import unittest
from unittest.mock import Mock, patch

SUPPORT = Path(__file__).resolve().parents[1] / 'support/protection'
MAP = runpy.run_path(str(SUPPORT / 'config_arguments.py'))
SCENE = runpy.run_path(str(SUPPORT / 'scenario.py'))
translate = MAP['translate_protection_arguments']
execute = MAP['execution_arguments']
normalize = SCENE['canonical_experiment_arguments']


class ConfigIdentityTests(unittest.TestCase):
    def test_omitted_legacy_scheme_is_explicit_off_after_promotion(self):
        old = ['satcompute', '--randomRun=11', '--faultMode=generate']
        self.assertIn('--protectionScheme=off', self.assertMapping(old))
        self.assertIn('--protectionScheme=off', execute(SCENE['arguments'](Path('unused'))))
        scoped_old = execute(['satcompute', '--protectionScheme=compfrr'])
        self.assertIn('--compfrrPlacementPolicy=fa-ffp', scoped_old)
        self.assertIn('--compfrrInputPolicy=eager', scoped_old)
        self.assertEqual(execute(scoped_old), scoped_old)

    def test_formal_runner_selects_compfrr_profile_but_baselines_stay_private(self):
        self.assertEqual(SCENE['execution_profile_options']('compfrr'), ('n5c', 'selective'))
        for scheme in ('recompute', 'one-plus-one', 'checkbullet', 'fixed', 'off'):
            self.assertEqual(SCENE['execution_profile_options'](scheme), ('fa-ffp', 'eager'))
        self.assertEqual(SCENE['execution_profile_options']('compfrr', 'lrl', 'deferred'), ('lrl', 'deferred'))
        # New formal commands serialize all controls; historic omitted values stay FA-FFP/Eager.
        new = execute(SCENE['arguments'](Path('unused'), protection_mode='compfrr',
                                       placement_mode='n5c', input_policy='selective'))
        for flag in ('--protectionScheme=compfrr', '--compfrrCheckpointPolicy=adaptive',
                     '--compfrrPlacementPolicy=compfrr', '--compfrrInputPolicy=selective',
                     '--compfrrRecoveryPolicy=relocate', '--compfrrPressureModel=cumulative',
                     '--compfrrPlacementAblation=none'):
            self.assertIn(flag, new)
        self.assertNotEqual(normalize(new), normalize(['satcompute', '--protectionMode=compfrr']))

    def assertMapping(self, old):
        new = execute(old)
        self.assertEqual(normalize(old), normalize(new))
        self.assertEqual(execute(new), new)
        self.assertEqual(SCENE['historical_comparison_arguments'](old),
                         SCENE['historical_comparison_arguments'](new))
        return new

    def test_complete_historical_matrix_identity(self):
        schemes = [('recompute', 'eager', 'relocate'), ('one-plus-one', 'eager', 'relocate'),
                   ('fixed', 'eager', 'recompute'), ('fixed', 'eager', 'relocate'),
                   ('compfrr', 'eager', 'recompute'), ('compfrr', 'eager', 'relocate'),
                   ('compfrr', 'deferred', 'recompute'), ('compfrr', 'deferred', 'relocate')]
        identities = set()
        for (scheme, staging, busy), placement in product(schemes, MAP['PLACEMENTS']):
            with self.subTest(scheme=scheme, staging=staging, busy=busy, placement=placement):
                old = SCENE['arguments'](Path('unused'), protection_mode=scheme,
                    placement_mode=placement, input_policy=staging, remote_busy_recovery_policy=busy)
                self.assertMapping(old)
                identities.add(tuple(normalize(old)))
        self.assertEqual(len(identities), 32)

    def test_adaptive_placements_input_fallback_and_ablations(self):
        for placement, variant, staging, busy in product(
                ('fa-ffp', 'n5c'), ('full', 'noR', 'noU', 'noM', 'rational-U'),
                ('eager', 'deferred', 'selective'), ('relocate', 'recompute')):
            if placement != 'n5c' and variant != 'full':
                continue
            self.assertMapping(SCENE['arguments'](Path('unused'), protection_mode='compfrr',
                placement_mode=placement, n5c_variant=variant, input_policy=staging,
                remote_busy_recovery_policy=busy))

    def test_cb_historical_default_is_not_new_private_default(self):
        omitted = ['satcompute', '--protectionMode=checkbullet']
        new = self.assertMapping(omitted)
        self.assertEqual(new[0], 'satcompute-protection-config-driver')
        self.assertIn('--testCbSatBusyPolicy=relocate', new)
        self.assertNotEqual(normalize(omitted), normalize(['satcompute', '--protectionScheme=cb-sat']))
        for placement, busy in product(MAP['PLACEMENTS'], ('relocate', 'recompute')):
            old = omitted + [f'--placementMode={placement}', f'--remoteBusyRecoveryPolicy={busy}']
            self.assertMapping(old)
        canonical = omitted + ['--remoteBusyRecoveryPolicy=recompute']
        self.assertEqual(normalize(canonical), normalize(['satcompute', '--protectionScheme=cb-sat']))

    def test_inactive_legacy_settings_remain_explicit_in_conversion_record(self):
        old = ['satcompute', '--protectionMode=recompute', '--fixedProtectionDelta=.1',
               '--fixedProtectionBatchN=2', '--backupStorageBytesPerNode=0',
               '--remoteBusyRecoveryPolicy=relocate', '--inputPolicy=eager']
        record = translate(old)
        self.assertEqual(record['original'], old)
        self.assertEqual(set(record['inactive']), {'fixedProtectionDelta', 'fixedProtectionBatchN',
                         'backupStorageBytesPerNode', 'remoteBusyRecoveryPolicy', 'inputPolicy'})
        self.assertEqual(set(record['inactive']), set(record['inactive_reasons']))
        self.assertMapping(old)
        self.assertEqual(normalize(old), normalize(['satcompute', '--protectionScheme=recompute']))

    def test_nondefault_fixed_pool_and_private_weight_preserved(self):
        old = ['satcompute', '--protectionMode=fixed', '--placementMode=lrl', '--lrlRecoveryWeight=3',
               '--fixedProtectionDelta=.125', '--fixedProtectionBatchN=5', '--backupStorageBytesPerNode=0']
        new = self.assertMapping(old)
        for expected in ('--testLrlRecoveryWeight=3', '--compfrrFixedDelta=.125',
                         '--compfrrFixedBatchN=5', '--backupStorageBytesPerNode=0'):
            self.assertIn(expected, new)
        self.assertNotEqual(normalize(old), normalize([x.replace('Weight=3', 'Weight=1') for x in old]))

    def test_archived_recent_readable_but_not_executable(self):
        old = SCENE['arguments'](Path('unused'), protection_mode='compfrr',
                                 placement_mode='n5c', n5c_variant='recent-U')
        self.assertIn('--compfrrPressureModel=historical-only:recent-U', normalize(old))
        with self.assertRaises(ValueError):
            execute(old)

    def test_old_input_pair_has_same_identity_and_jit_is_rejected(self):
        base = ['satcompute', '--protectionMode=compfrr']
        old = base + ['--inputStagingPolicy=deferred', '--inputAdmissionPolicy=ser-break-even']
        new = execute(base + ['--inputPolicy=selective'])
        self.assertEqual(normalize(old), normalize(new))
        for bad in (['--inputPolicy=jit'], ['--inputStagingPolicy=deferred', '--inputAdmissionPolicy=jit']):
            with self.assertRaises(ValueError):
                normalize(base + bad)

    def test_conflicting_names_duplicates_and_unsupported_capability_rejected(self):
        for argv in (
            ['--protectionMode=fixed', '--protectionScheme=compfrr'],
            ['--protectionMode=off', '--protectionMode=off'],
            ['--protectionMode=fixed', '--inputPolicy=selective'],
            ['--protectionMode=one-plus-one', '--placementMode=n5c'],
            ['--protectionMode=compfrr', '--n5cVariant=noU'],
            ['--protectionMode=multitree'], ['--protectionMode=fixed', '--fixedProtectionDelta=nan'],
            ['--protectionMode=compfrr', '--lrlRecoveryWeight=-1']):
            with self.subTest(argv=argv), self.assertRaises(ValueError):
                execute(['satcompute', *argv])
        for argv in (
            ['--protectionScheme=recompute', '--compfrrInputPolicy=eager'],
            ['--protectionScheme=off', '--protectionScheme=off'],
            ['--protectionScheme=compfrr', '--inputStagingPolicy=deferred']):
            with self.subTest(argv=argv), self.assertRaises(ValueError):
                normalize(['satcompute', *argv])

    def test_split_flags_and_argument_order_do_not_change_mapping(self):
        self.assertMapping(['satcompute', '--protectionMode', 'fixed', '--placementMode', 'lrl',
                            '--fixedProtectionDelta', '.05', '--randomRun=11'])
        a = ['satcompute', '--protectionMode=compfrr', '--inputPolicy=deferred', '--randomRun=11']
        self.assertEqual(normalize(a), normalize([a[0], *reversed(a[1:])]))
        self.assertNotEqual(normalize(a), normalize([x.replace('Run=11', 'Run=12') for x in a]))

    def test_non_platform_fixture_commands_are_not_rewritten(self):
        argv = ['satcompute-frequency-runtime-test', '--outputDir=unused']
        self.assertEqual(execute(argv), argv)

    def test_cb_runner_metadata_keeps_actual_old_identity(self):
        tools = runpy.run_path(str(SUPPORT.parents[2] / 'protection/baseline/checkbullet/tools/cb_tools.py'))
        run = tools['execute']
        for busy in ('recompute', 'relocate'):
            with tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp) / 'cb'
                old = SCENE['arguments'](directory, protection_mode='checkbullet',
                    placement_mode='fa-lrl', remote_busy_recovery_policy=busy)
                # No simulation or source mutation: only exercise execution metadata construction.
                fake = Mock()
                fake.run.return_value.returncode = 0
                with patch.dict(run.__globals__, clean_head=lambda: 'test-only', subprocess=fake):
                    run(directory, old, 'test-only', {})
                evidence = json.loads((directory / 'execution.json').read_text())
                self.assertEqual((evidence['protection_mode'], evidence['placement_mode'],
                                  evidence['remote_busy_recovery_policy']), ('checkbullet', 'fa-lrl', busy))
                self.assertEqual(normalize(old), normalize(shlex.split(evidence['command'][-1])))


if __name__ == '__main__':
    unittest.main()
