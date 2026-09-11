"""Protection entry-point guards; no network simulation or fault-recovery activation."""
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[4]


class ProtectionConfigTests(unittest.TestCase):
    def run_cli(self, options):
        return subprocess.run(
            [sys.executable, str(ROOT / "ns3"), "run", "--no-build", f"satcompute {options}"],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=30, check=False,
        )

    def test_fixed_rejects_shadow_execution(self):
        result = self.run_cli("--protectionMode=fixed --compfrr-shadow=1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protection requires network tasks and shadow off", result.stdout)

    def test_fixed_generate_passes_mode_guard_without_running_default_scene(self):
        result = self.run_cli("--protectionMode=fixed --faultMode=generate --fixedProtectionDelta=0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fixedProtectionDelta", result.stdout)

    def test_invalid_mode_is_rejected(self):
        result = self.run_cli("--protectionMode=unsupported")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protectionMode has an unsupported value", result.stdout)

    def test_placement_guards(self):
        for options in ("--placementMode=unsupported",
                        "--placementMode=lrl --protectionMode=off"):
            with self.subTest(options=options):
                result = self.run_cli(options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("placementMode", result.stdout)

    def test_fixed_lrl_passes_placement_guard(self):
        result = self.run_cli("--protectionMode=fixed --placementMode=lrl --fixedProtectionDelta=0")
        self.assertIn("fixedProtectionDelta", result.stdout)
        self.assertNotIn("lrl requires", result.stdout)

    def test_future_baselines_and_placement_are_explicitly_unimplemented(self):
        for options in ("--protectionMode=one-plus-one",
                        "--protectionMode=fixed --placementMode=n5c"):
            with self.subTest(options=options):
                result = self.run_cli(options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("NOT_IMPLEMENTED", result.stdout)

    def test_full_recompute_guard_and_ffp_only(self):
        result = self.run_cli("--protectionMode=recompute --fixedProtectionDelta=0")
        self.assertIn("fixedProtectionDelta", result.stdout)
        self.assertNotIn("NOT_IMPLEMENTED", result.stdout)
        result = self.run_cli("--protectionMode=recompute --placementMode=lrl")
        self.assertIn("lrl requires fixed or compfrr", result.stdout)

    def test_busy_policy_guard_and_off_ignores_valid_values(self):
        result = self.run_cli("--remoteBusyRecoveryPolicy=unsupported")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("remoteBusyRecoveryPolicy has an unsupported value", result.stdout)
        for mode in ("recompute", "relocate"):
            result = self.run_cli(f"--protectionMode=off --remoteBusyRecoveryPolicy={mode} --fixedProtectionDelta=0")
            self.assertIn("fixedProtectionDelta", result.stdout)

    def test_frequency_requires_generate_and_compute_sources(self):
        for options in ("--faultMode=none", "--faultMode=generate --faultEnableF1=0 --faultEnableF2=0"):
            with self.subTest(options=options):
                result = self.run_cli(f"--protectionMode=compfrr {options}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("protectionMode", result.stdout)

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
                result = self.run_cli(f"--fixedProtectionDelta={value}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("fixedProtectionDelta", result.stdout)

    def test_batch_domain(self):
        for value in ("0", "21", "4294967295"):
            with self.subTest(value=value):
                result = self.run_cli(f"--fixedProtectionBatchN={value}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("fixedProtectionBatchN", result.stdout)

    def test_production_has_no_shadow_dependency(self):
        for path in (ROOT / "contrib/satcompute/protection").rglob("*"):
            if path.suffix not in (".h", ".cc"):
                continue
            with self.subTest(path=path.name):
                self.assertNotIn("compfrr-shadow-", path.read_text())

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
