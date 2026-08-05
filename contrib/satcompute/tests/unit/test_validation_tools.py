"""Contract checks for the restored read-only validation entrypoints."""

from pathlib import Path
import subprocess
import sys
import unittest


MODULE_ROOT = Path(__file__).resolve().parents[2]
VALIDATION_ROOT = MODULE_ROOT / "tools" / "validation"
TOOLS = (
    "check-capacity-aware-output.py",
    "check-ecmp-output.py",
    "check-flow-drop-reasons.py",
    "check-size-aware-output.py",
    "check-size-aware-replay.py",
    "check-task-output.py",
    "preflight-task-workload.py",
)


class ValidationToolContractTest(unittest.TestCase):
    def test_tools_are_python3_entrypoints(self):
        for name in TOOLS:
            with self.subTest(tool=name):
                path = VALIDATION_ROOT / name
                source = path.read_text(encoding="utf-8")
                self.assertTrue(source.startswith("#!/usr/bin/env python3"))
                compile(source, str(path), "exec")

    def test_help_is_available_without_optional_dependencies(self):
        for name in TOOLS:
            with self.subTest(tool=name):
                result = subprocess.run(
                    [sys.executable, str(VALIDATION_ROOT / name), "--help"],
                    cwd=MODULE_ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("usage:", result.stdout.lower())


if __name__ == "__main__":
    unittest.main()
