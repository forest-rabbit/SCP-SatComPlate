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
F1_EXAMPLE = MODULE_ROOT / "input" / "examples" / "leo-66-120s-f1"


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

    def test_f1_validation_profile_has_expected_roles_and_is_deterministic(self):
        script = GENERATION_ROOT / "generate-task-workload.py"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nodes = root / "nodes.json"
            compute = root / "compute.json"
            nodes.write_text(
                json.dumps(
                    {
                        "nodes": [
                            {"node_id": node_id, "node_type": "sat"}
                            for node_id in range(66)
                        ]
                    }
                ),
                encoding="utf-8",
            )
            compute.write_text(
                json.dumps(
                    {
                        "compute_nodes": [
                            {
                                "node_id": node_id,
                                "compute_rate_work_units_per_second": 1_500_000,
                            }
                            for node_id in range(66)
                        ]
                    }
                ),
                encoding="utf-8",
            )
            outputs = []
            for suffix in ("first", "second"):
                trace = root / f"tasks-{suffix}.json"
                summary = root / f"summary-{suffix}.json"
                result = run_tool(
                    script,
                    "--profile",
                    "f1-validation",
                    "--nodes-file",
                    nodes,
                    "--compute-profile",
                    compute,
                    "--seed",
                    "n4b-f1-66",
                    "--output-task-trace",
                    trace,
                    "--output-workload-summary",
                    summary,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append((trace, summary))
            self.assertEqual(outputs[0][0].read_bytes(), outputs[1][0].read_bytes())
            self.assertEqual(outputs[0][1].read_bytes(), outputs[1][1].read_bytes())
            self.assertEqual(
                outputs[0][0].read_bytes(),
                (F1_EXAMPLE / "task-trace.json").read_bytes(),
            )
            self.assertEqual(
                outputs[0][1].read_bytes(),
                (F1_EXAMPLE / "workload-summary.json").read_bytes(),
            )

            trace = json.loads(outputs[0][0].read_text(encoding="utf-8"))
            summary = json.loads(outputs[0][1].read_text(encoding="utf-8"))
            self.assertEqual(len(trace["tasks"]), 20)
            self.assertEqual(summary["profile"], "f1-validation")
            self.assertEqual(summary["hotspot_compute_node_ids"], [0, 11, 22])
            self.assertEqual(summary["expected_critical_failure_task_ids"], [4, 9, 14])
            self.assertEqual(summary["post_recovery_task_ids"], [5, 10, 15])
            self.assertEqual(summary["risk_only_compute_node_id"], 33)
            self.assertEqual(summary["risk_only_task_ids"], [16, 17, 18])
            self.assertEqual(summary["control_compute_node_ids"], [44, 55])
            counts = {
                node_id: sum(
                    task["compute_node_id"] == node_id for task in trace["tasks"]
                )
                for node_id in (0, 11, 22, 33, 44, 55)
            }
            self.assertEqual(counts, {0: 5, 11: 5, 22: 5, 33: 3, 44: 1, 55: 1})


if __name__ == "__main__":
    unittest.main()
