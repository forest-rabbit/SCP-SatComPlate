"""Canonical production identity and isolated historical evidence compatibility."""
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[2]
SUPPORT = MODULE / 'tests/support/protection'
SCENE = runpy.run_path(str(SUPPORT / 'scenario.py'))
LEGACY = runpy.run_path(str(SUPPORT / 'historical_placement.py'))


class CanonicalPlacementTests(unittest.TestCase):
    def test_recent_policy_has_no_runtime_implementation_or_launcher(self):
        for directory in ('protection', 'metrics'):
            for path in (MODULE / directory).rglob('*'):
                if path.suffix not in ('.h', '.cc'):
                    continue
                with self.subTest(path=str(path.relative_to(MODULE))):
                    for retired in ('RECENT_U', 'recentUtilization', 'recentNormalBusyNs',
                                    'recentRecoveryBusyNs', 'recentExposureNs', 'recent-u-history.csv'):
                        self.assertNotIn(retired, path.read_text())
        self.assertFalse((MODULE / 'tests/integration/regression/run-n5c-recent-u.py').exists())
        history = runpy.run_path(str(SUPPORT / 'historical_recent_evidence.py'))
        self.assertTrue({'arguments', 'verify', 'references', 'equivalent'} <= history.keys())
        self.assertTrue({'execute', 'prepare', 'gates', 'main', 'subprocess'}.isdisjoint(history))

    def test_no_stage_identity_in_owned_production_sources(self):
        files = [MODULE / 'satcompute.cc', MODULE / 'CMakeLists.txt']
        for directory in ('protection', 'metrics'):
            files.extend(p for p in (MODULE / directory).rglob('*') if p.suffix in ('.h', '.cc'))
        for path in files:
            with self.subTest(path=str(path.relative_to(MODULE))):
                self.assertNotIn('n5c', path.name.lower())
                self.assertNotIn('n5c', path.read_text().lower())

    def test_current_runner_does_not_execute_historical_generator(self):
        generate = SCENE['current_arguments']
        with patch.dict(generate.__globals__, arguments=lambda *a, **kw: self.fail('historical generator used')):
            argv = generate(Path('unused'))
        self.assertEqual(argv[0], 'satcompute')
        self.assertFalse(any('n5c' in value.lower() for value in argv))
        self.assertIn('--compfrrPlacementPolicy=compfrr', argv)

    def test_current_cli_help_is_canonical(self):
        result = subprocess.run([sys.executable, str(MODULE / 'tests/integration/regression/run-final-scenario.py'),
                                 '--help'], text=True, capture_output=True, check=True)
        self.assertNotIn('n5c', result.stdout.lower())
        self.assertIn('--pressure-model', result.stdout)
        self.assertIn('--placement-ablation', result.stdout)

    def test_historical_reader_preserves_source_and_rejects_ambiguity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / 'n5c-placement-decisions.csv'
            new = root / 'compfrr-placement-decisions.csv'
            payload = 'placement,bytes\nn5c,123\n'
            old.write_text(payload)
            self.assertEqual(LEGACY['evidence_path'](root, new.name), old)
            self.assertEqual(old.read_text(), payload)
            new.write_text('placement,bytes\ncompfrr,123\n')
            with self.assertRaisesRegex(ValueError, 'ambiguous'):
                LEGACY['evidence_path'](root, new.name)


if __name__ == '__main__':
    unittest.main()
