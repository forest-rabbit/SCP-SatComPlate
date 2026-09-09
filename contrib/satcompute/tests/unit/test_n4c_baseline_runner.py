"""Check opt-in delay overrides without launching the simulator."""

import contextlib
import io
import json
from pathlib import Path
import runpy
import shlex
import tempfile
import unittest
from unittest.mock import patch


RUNNER = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                            "integration/regression/run-n4c-baseline.py"))


class BaselineRunnerTest(unittest.TestCase):
    def run_mocked(self, extra):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "run"
            def git_output(command, **kwargs):
                return "test-commit\n" if kwargs.get("text") else b""

            with patch("sys.argv", ["runner", "--output-dir", str(output), *extra]), \
                    patch("subprocess.check_output", side_effect=git_output), \
                    patch("subprocess.run") as launch, contextlib.redirect_stdout(io.StringIO()):
                launch.return_value.returncode = 0
                self.assertEqual(RUNNER["main"](), 0)
                launch.assert_called_once()
            identity = json.loads((output / "execution.json").read_text())
            return identity, shlex.split(identity["command"][-1])

    def test_default_uses_accepted_one_ms(self):
        identity, arguments = self.run_mocked([])
        self.assertEqual(identity["fixed_delay_seconds"], 0.001)
        self.assertIn("--fixedDelay=0.001", arguments)
        for expected in ("--simulationDuration=1300", "--randomRun=11", "--faultMode=generate",
                         "--faultF3Node=62", "--faultF3Time=1027.055770726", "--faultProbabilityAudit=0"):
            self.assertIn(expected, arguments)

    def test_historical_eight_ms_changes_only_delay_among_physical_arguments(self):
        identity, arguments = self.run_mocked(["--fixed-delay-seconds=0.008"])
        _, defaults = self.run_mocked([])
        self.assertEqual(identity["fixed_delay_seconds"], 0.008)
        self.assertIn("--fixedDelay=0.008", arguments)
        normalize = lambda values: [v for v in values if not v.startswith(("--fixedDelay=", "--outputDir=", "--faultTrace="))]
        self.assertEqual(normalize(arguments), normalize(defaults))

    def test_invalid_delays_do_not_launch_or_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "run"
            for value in ("0", "-1", "nan", "inf"):
                with self.subTest(value=value), \
                        patch("sys.argv", ["runner", "--output-dir", str(output), f"--fixed-delay-seconds={value}"]), \
                        patch("subprocess.run") as launch, contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        RUNNER["main"]()
                    self.assertEqual(error.exception.code, 2)
                    launch.assert_not_called()
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
