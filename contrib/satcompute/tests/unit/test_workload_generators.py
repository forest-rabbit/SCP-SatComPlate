"""Determinism and closed-world checks for independent workload generators."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


MODULE_ROOT = Path(__file__).resolve().parents[2]
GENERATION_ROOT = MODULE_ROOT / "tools" / "generation"
NODES = MODULE_ROOT / "input" / "topology" / "examples" / "xw-66sat" / "nodes_0s.json"
COMPUTE = (
    MODULE_ROOT
    / "input"
    / "topology"
    / "resources"
    / "workload"
    / "xw-66sat-static-2g-compute-profile.json"
)


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
    def test_transfer_generator_is_deterministic_and_closed_world(self):
        script = GENERATION_ROOT / "generate-transfer-workload.py"
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.json"
            second = Path(directory) / "second.json"
            common = (
                script,
                "--nodes-file",
                NODES,
                "--count",
                "5",
                "--min-size-bytes",
                "1024",
                "--max-size-bytes",
                "2048",
                "--arrival-start-ns",
                "100",
                "--arrival-step-ns",
                "10",
            )
            for output in (first, second):
                result = run_tool(*common, "--output", output)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            document = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(set(document), {"schema_version", "transfers"})
            self.assertEqual(document["schema_version"], "0.1")
            self.assertEqual(len(document["transfers"]), 5)
            self.assertEqual(
                set(document["transfers"][0]),
                {
                    "transfer_id",
                    "source_node_id",
                    "destination_node_id",
                    "size_bytes",
                    "arrival_time_ns",
                },
            )

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
                "--rules-version",
                "unit-v1",
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
            self.assertEqual(set(trace), {"schema_version", "tasks"})
            self.assertEqual(len(trace["tasks"]), 12)
            self.assertEqual(sum(task["input_bytes"] for task in trace["tasks"]), 24 * (1 << 20))
            self.assertEqual(summary["task_count"], 12)
            self.assertEqual(summary["total_input_bytes"], 24 * (1 << 20))


if __name__ == "__main__":
    unittest.main()
