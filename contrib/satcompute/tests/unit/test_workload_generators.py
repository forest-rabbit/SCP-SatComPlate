"""Determinism and closed-world checks for independent workload generators."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


MODULE_ROOT = Path(__file__).resolve().parents[2]
GENERATION_ROOT = MODULE_ROOT / "tools" / "generation"
NODES = MODULE_ROOT / "tests" / "fixtures" / "topology" / "nodes_0s.json"
COMPUTE = MODULE_ROOT / "tests" / "fixtures" / "task" / "compute-profile-single.json"


def run_tool(*arguments):
    return subprocess.run(
        [sys.executable, *map(str, arguments)],
        cwd=MODULE_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class WorkloadGeneratorTest(unittest.TestCase):
    def test_task_generator_is_deterministic_and_budget_exact(self):
        script = GENERATION_ROOT / "generate-task-workload.py"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace_first = root / "tasks-first.json"
            trace_second = root / "tasks-second.json"
            summary_first = root / "summary-first.json"
            summary_second = root / "summary-second.json"
            common = (
                script,
                "--nodes-file",
                NODES,
                "--compute-profile",
                COMPUTE,
                "--task-count",
                "12",
                "--total-input-bytes",
                str(24 * (1 << 20)),
                "--seed",
                "unit-seed",
                "--arrival-start-ns",
                "0",
                "--arrival-end-ns",
                "11000000",
                "--arrival-mode",
                "uniform",
            )
            for trace, summary in (
                (trace_first, summary_first),
                (trace_second, summary_second),
            ):
                result = run_tool(
                    *common,
                    "--output-task-trace",
                    trace,
                    "--output-workload-summary",
                    summary,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("PASS:", result.stdout)
            self.assertEqual(trace_first.read_bytes(), trace_second.read_bytes())
            self.assertEqual(summary_first.read_bytes(), summary_second.read_bytes())

            trace = json.loads(trace_first.read_text(encoding="utf-8"))
            summary = json.loads(summary_first.read_text(encoding="utf-8"))
            self.assertEqual(set(trace), {"tasks"})
            self.assertEqual(len(trace["tasks"]), 12)
            self.assertEqual(sum(task["input_bytes"] for task in trace["tasks"]), 24 * (1 << 20))
            self.assertTrue(all(task["output_bytes"] > 0 for task in trace["tasks"]))
            self.assertEqual(summary["task_count"], 12)
            self.assertEqual(summary["total_input_bytes"], 24 * (1 << 20))
            self.assertNotIn("generator_version", summary)
            self.assertNotIn("rules_version", summary)
            self.assertNotIn("task_trace_sha256", summary)


if __name__ == "__main__":
    unittest.main()
