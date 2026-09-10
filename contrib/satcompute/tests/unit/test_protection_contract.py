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

    def test_fixed_does_not_silently_enable_fault_recovery_before_g3(self):
        result = self.run_cli("--protectionMode=fixed")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("N5A-G2 fixed requires no-fault network tasks", result.stdout)

    def test_invalid_mode_is_rejected(self):
        result = self.run_cli("--protectionMode=compfrr")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protectionMode has an unsupported value", result.stdout)

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


if __name__ == "__main__":
    unittest.main()
