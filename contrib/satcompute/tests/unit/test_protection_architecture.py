"""N5R boundaries and legacy-entry forwarding; no simulation or formal matrix."""
import ast
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2]
TESTS = MODULE / "tests"
PROTECTION = MODULE / "protection"
HELPERS = TESTS / "support/protection"
ENTRIES = {
    "analyze-protection-accounting.py": "accounting.py",
    "analyze-frequency-evaluation.py": "frequency_audit.py",
    "analyze-baseline-evaluation.py": "baseline_audit.py",
    "analyze-n5c-placement.py": "placement_audit.py",
    "analyze-riskweighted-start.py": "risk_start_audit.py",
    "analyze-input-deferred.py": "input_staging_audit.py",
    "run-final-scenario.py": "scenario.py",
}


class ProtectionArchitectureTests(unittest.TestCase):
    def test_common_recovery_observer_and_fixed_do_not_include_frequency_or_p(self):
        def dependencies(path, seen):
            path = path.resolve()
            if path in seen:
                return
            seen.add(path)
            self.assertNotIn("/policy/compfrr/", str(path))
            for include in re.findall(r'^#include "([^"]+)"', path.read_text(), re.M):
                child = (path.parent / include).resolve()
                if child.is_file() and PROTECTION in child.parents:
                    dependencies(child, seen)
        for relative in ("runtime/recovery-controller.h", "runtime/placement-resource-tracker.h",
                         "runtime/transfer-only-recovery-ledger.h", "policy/fixed/fixed-controller.h",
                         "mechanism/relocation/checkpoint-relocation-executor.h"):
            dependencies(PROTECTION / relative, set())

    def test_cb_runtime_and_replica_do_not_acquire_compfrr_checkpoint_semantics(self):
        for path in [*(PROTECTION / "baseline/checkbullet").glob("*.h"),
                     PROTECTION / "mechanism/replication/replica-manager.h"]:
            includes = re.findall(r'^#include "([^"]+)"', path.read_text(), re.M)
            self.assertFalse(any("compfrr" in p or "checkpoint-manager.h" in p for p in includes))

    def test_legacy_entries_forward_to_one_helper_implementation(self):
        for old, new in ENTRIES.items():
            wrapper = TESTS / "integration/regression" / old
            api = runpy.run_path(str(wrapper))
            helper = runpy.run_path(str(HELPERS / new))
            own_functions = {node.name for node in ast.parse((HELPERS / new).read_text()).body
                             if isinstance(node, ast.FunctionDef)}
            for name in own_functions:
                self.assertEqual(api[name].__code__.co_filename, str(HELPERS / new))
                self.assertEqual(api[name].__code__.co_code, helper[name].__code__.co_code)
            self.assertFalse(any(isinstance(node, ast.FunctionDef) for node in ast.parse(wrapper.read_text()).body))

    def test_final_scene_cli_rejects_recent_but_description_keeps_historical_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "must-not-exist"
            result = subprocess.run([sys.executable, str(TESTS / "integration/regression/run-final-scenario.py"),
                "--output-dir", str(output), "--protection-mode", "compfrr", "--placement-mode", "n5c",
                "--n5c-variant", "recent-U"], text=True, capture_output=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid choice", result.stderr)
            self.assertFalse(output.exists())
            api = runpy.run_path(str(HELPERS / "scenario.py"))
            historical = api["arguments"](output, protection_mode="compfrr", placement_mode="n5c",
                                          n5c_variant="recent-U")
            self.assertIn("--n5cVariant=recent-U", historical)


if __name__ == "__main__":
    unittest.main()
