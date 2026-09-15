"""Protection entry-point guards; no network simulation or fault-recovery activation."""
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[4]


class ProtectionConfigTests(unittest.TestCase):
    def test_production_help_and_private_options_do_not_leak(self):
        result = self.run_cli('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('protectionScheme', result.stdout)
        forbidden = ('protectionMode', 'placementMode', 'n5cVariant', 'inputPolicy',
                     'remoteBusyRecoveryPolicy', 'fixedProtectionDelta', 'fixedProtectionBatchN',
                     'lrlRecoveryWeight', 'testBaselinePlacement', 'testCbSatBusyPolicy', 'testLrlRecoveryWeight')
        for name in forbidden:
            self.assertNotIn(f'--{name}', result.stdout)
            rejected = self.run_cli(f'--{name}=invalid --islBandwidthBps=0')
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(f'Invalid command-line argument: --{name}', rejected.stdout)

    def run_cli(self, options):
        return subprocess.run(
            [sys.executable, str(ROOT / "ns3"), "run", "--no-build", f"satcompute {options}"],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=30, check=False,
        )

    def test_fixed_rejects_shadow_execution(self):
        result = self.run_cli("--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --compfrr-shadow=1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protection requires network tasks and shadow off", result.stdout)

    def test_fixed_generate_passes_mode_guard_without_running_default_scene(self):
        result = self.run_cli("--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --faultMode=generate --compfrrFixedDelta=0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compfrrFixedDelta", result.stdout)

    def test_invalid_mode_is_rejected(self):
        result = self.run_cli("--protectionScheme=unsupported")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protectionScheme has an unsupported value", result.stdout)

    def test_placement_guards(self):
        for options in ("--compfrrPlacementPolicy=unsupported",
                        "--compfrrPlacementPolicy=lrl --protectionScheme=off"):
            with self.subTest(options=options):
                result = self.run_cli(options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("compfrrPlacementPolicy", result.stdout)

    def test_fixed_lrl_passes_placement_guard(self):
        result = self.run_cli("--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --compfrrPlacementPolicy=lrl --compfrrFixedDelta=0")
        self.assertIn("compfrrFixedDelta", result.stdout)
        self.assertNotIn("lrl requires", result.stdout)

    def test_n5c_is_only_compfrr_designated_remote_placement(self):
        for options in ("--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --compfrrPlacementPolicy=compfrr",
                        "--protectionScheme=one-plus-one --compfrrPlacementPolicy=compfrr"):
            with self.subTest(options=options):
                result = self.run_cli(options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("compfrrPlacementPolicy", result.stdout)
        for pressure, ablation in (("cumulative", "none"), ("cumulative", "noR"),
                                  ("cumulative", "noU"), ("cumulative", "noM"), ("idle-aware", "none")):
            # Stop on a deliberately invalid platform parameter, after successful config validation.
            result = self.run_cli(f"--protectionScheme=compfrr --compfrrPlacementPolicy=compfrr --compfrrPressureModel={pressure} --compfrrPlacementAblation={ablation} --islBandwidthBps=0")
            self.assertIn("islBandwidthBps", result.stdout)
        result = self.run_cli("--protectionScheme=compfrr --compfrrPlacementPolicy=fa-ffp --compfrrPlacementAblation=noM")
        self.assertIn("requires adaptive CompFRR-P placement", result.stdout)

    def test_recent_u_is_historical_only_not_a_production_policy(self):
        result = self.run_cli("--protectionScheme=compfrr --compfrrPlacementPolicy=compfrr --compfrrPressureModel=recent-U")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("recent-U is historical-only", result.stdout)

    def test_full_baselines_guard_and_four_placements(self):
        for mode in ("recompute", "one-plus-one"):
            result = self.run_cli(f"--protectionScheme={mode} --islBandwidthBps=0")
            self.assertIn("islBandwidthBps", result.stdout)
            self.assertNotIn("NOT_IMPLEMENTED", result.stdout)
            for placement in ("ffp", "lrl", "fa-ffp", "fa-lrl"):
                result = subprocess.run([str(ROOT/'build/contrib/satcompute/tests/ns3.48-satcompute-protection-config-driver-default'),
                    f'--protectionScheme={mode}', f'--testBaselinePlacement={placement}', '--islBandwidthBps=0'],
                    cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                self.assertIn("islBandwidthBps", result.stdout)

    def test_busy_policy_guard_and_off_rejects_explicit_private_values(self):
        result = self.run_cli("--compfrrRecoveryPolicy=unsupported")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compfrrRecoveryPolicy has an unsupported value", result.stdout)
        for mode in ("recompute", "relocate"):
            result = self.run_cli(f"--protectionScheme=off --compfrrRecoveryPolicy={mode}")
            self.assertIn("requires protectionScheme=compfrr", result.stdout)

    def test_frequency_requires_generate_and_compute_sources(self):
        for options in ("--faultMode=none", "--faultMode=generate --faultEnableF1=0 --faultEnableF2=0"):
            with self.subTest(options=options):
                result = self.run_cli(f"--protectionScheme=compfrr {options}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("protectionScheme", result.stdout)

    def test_validation_replay_requires_explicit_input_and_no_online_audit(self):
        for options in ("--faultMode=validation-replay",
                        "--faultMode=generate --validationFaultTrace=unused.json",
                        "--faultMode=validation-replay --validationFaultTrace=unused.json --faultProbabilityAudit=1"):
            with self.subTest(options=options):
                result = self.run_cli(options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("validationFaultTrace", result.stdout)

    def test_delta_domain_and_precision(self):
        for value in ("0", "-0.01", "1.1", "0.0001", "nan", "inf"):
            with self.subTest(value=value):
                result = self.run_cli(f"--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --compfrrFixedDelta={value}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("compfrrFixedDelta", result.stdout)

    def test_batch_domain(self):
        for value in ("0", "21", "4294967295"):
            with self.subTest(value=value):
                result = self.run_cli(f"--protectionScheme=compfrr --compfrrCheckpointPolicy=fixed --compfrrFixedBatchN={value}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("compfrrFixedBatchN", result.stdout)

    def test_production_has_no_shadow_dependency(self):
        for path in (ROOT / "contrib/satcompute/protection").rglob("*"):
            if path.suffix not in (".h", ".cc"):
                continue
            with self.subTest(path=path.name):
                # CLI diagnostics live here now, but production never includes the shadow evaluator.
                self.assertNotIn('#include "ns3/compfrr-shadow-', path.read_text())
                self.assertNotIn('#include "compfrr-shadow-', path.read_text())

    def test_n5b_probability_interface_keeps_canonical_causal_inputs(self):
        frequency = ROOT / "contrib/satcompute/protection/policy/compfrr/frequency"
        implementation = (frequency / "compfrr-frequency-policy.cc").read_text()
        self.assertIn("PredictComputeFailureBeforeFinish(in)", implementation)
        self.assertIn("currentSamplerQ != prediction.combinedStepFailureProbability", implementation)
        for path in frequency.glob("*.*"):
            if path.suffix not in (".h", ".cc"):
                continue
            for forbidden in ("QueryComputeRisk", "FaultTrace", "F3FaultParameters",
                              "GetValue(", "ReadValidationFaultTrace"):
                with self.subTest(path=path.name, forbidden=forbidden):
                    self.assertNotIn(forbidden, path.read_text())


if __name__ == "__main__":
    unittest.main()
