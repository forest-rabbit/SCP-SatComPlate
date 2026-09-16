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
    "analyze-compfrr-placement.py": "placement_audit.py",
    "analyze-riskweighted-start.py": "risk_start_audit.py",
    "analyze-input-deferred.py": "input_staging_audit.py",
    "run-final-scenario.py": "scenario.py",
}


class ProtectionArchitectureTests(unittest.TestCase):
    def test_baseline_and_shared_placement_have_one_canonical_owner(self):
        self.assertFalse((PROTECTION / "policy/baseline").exists())
        for name in ("checkbullet", "recompute", "one-plus-one", "multitree"):
            self.assertTrue(list((PROTECTION / "baseline" / name).glob("*.cc")))
        for name in ("first-feasible", "least-recovery-load", "fa-first-feasible", "fa-least-recovery-load"):
            self.assertTrue(list((PROTECTION / "policy/placement" / name).glob("*.h")))
        multitree = PROTECTION / "baseline/multitree"
        for stem in ("multitree-feature-adapter", "multitree-published-rule", "multitree-decision-log", "multitree-controller"):
            self.assertTrue((multitree / (stem + ".cc")).is_file())
            self.assertTrue((multitree / (stem + ".h")).is_file())
        self.assertTrue((multitree / "calibration/published-ft-scale.json").is_file())

    def test_cmake_sources_and_public_headers_are_unique(self):
        cmake = (MODULE / "CMakeLists.txt").read_text().split("  LIBRARIES_TO_LINK", 1)[0]
        sources, headers = cmake.split("  HEADER_FILES")
        paths = re.findall(r"^    (protection/[^\s]+)", sources, re.M)
        self.assertEqual(len(paths), len(set(paths)))
        self.assertTrue(all((MODULE / p).is_file() for p in paths))
        self.assertFalse(any("policy/baseline" in p for p in paths))
        self.assertTrue(all(p.startswith("protection/baseline/multitree/")
                            for p in paths if "multitree" in p))
        public = re.findall(r"^    (protection/[^\s]+)", headers, re.M)
        self.assertEqual(len(public), len({Path(p).name for p in public}))
        self.assertTrue(all((MODULE / p).is_file() for p in public))
        for part in ("policy", "state", "manager", "recovery", "config", "controller"):
            self.assertIn(f"protection/baseline/checkbullet/cb-sat-{part}.h", public)
            exported = MODULE.parents[1] / "build/include/ns3" / f"cb-sat-{part}.h"
            self.assertEqual(exported.read_text(),
                f'#include "{PROTECTION / "baseline/checkbullet" / f"cb-sat-{part}.h"}"\n')
        self.assertTrue((PROTECTION / "baseline/checkbullet/calibration/frozen-mtbf-profile.json").is_file())

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
        roots = [*(PROTECTION / "common").glob("*.h"), *(PROTECTION / "common").glob("*.cc")]
        for stem in ("runtime/recovery-controller", "runtime/placement-resource-tracker",
                     "runtime/transfer-only-recovery-ledger", "policy/fixed/fixed-controller",
                     "mechanism/relocation/checkpoint-relocation-executor"):
            roots.extend(PROTECTION / (stem + suffix) for suffix in (".h", ".cc"))
        for path in roots:
            dependencies(path, set())

    def test_cb_runtime_and_replica_do_not_acquire_compfrr_checkpoint_semantics(self):
        for path in [*(PROTECTION / "baseline/checkbullet").glob("*.h"),
                     *(PROTECTION / "baseline/checkbullet").glob("*.cc"),
                     PROTECTION / "mechanism/replication/replica-manager.h",
                     PROTECTION / "mechanism/replication/replica-manager.cc"]:
            includes = re.findall(r'^#include "([^"]+)"', path.read_text(), re.M)
            self.assertFalse(any("compfrr" in p or "checkpoint-manager.h" in p for p in includes))
        for path in (PROTECTION / 'mechanism/replication').glob('*'):
            if path.suffix in ('.h', '.cc'):
                self.assertNotIn('one-plus-one-policy.h', path.read_text())

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
                "--output-dir", str(output), "--protection-mode", "compfrr", "--placement-mode", "compfrr",
                "--pressure-model", "recent-U"], text=True, capture_output=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid choice", result.stderr)
            self.assertFalse(output.exists())
            api = runpy.run_path(str(HELPERS / "scenario.py"))
            historical = api["arguments"](output, protection_mode="compfrr", placement_mode="n5c",
                                          n5c_variant="recent-U")
            self.assertIn("--n5cVariant=recent-U", historical)


if __name__ == "__main__":
    unittest.main()
