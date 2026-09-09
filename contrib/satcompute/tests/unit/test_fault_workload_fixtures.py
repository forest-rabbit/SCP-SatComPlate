"""Maintain fixed fault-test workloads without retaining obsolete generator profiles."""
from collections import Counter
import json
from pathlib import Path
import unittest

EXAMPLES = Path(__file__).resolve().parents[2] / "input/examples"


class FaultWorkloadFixtures(unittest.TestCase):
    def read(self, name, count):
        directory = EXAMPLES / name
        tasks = json.loads((directory / "task-trace.json").read_text())["tasks"]
        summary = json.loads((directory / "workload-summary.json").read_text())
        self.assertEqual(len(tasks), count)
        self.assertEqual(len({t["task_id"] for t in tasks}), count)
        self.assertTrue(all(t["input_bytes"] > 0 and t["compute_work_units"] > 0 for t in tasks))
        self.assertTrue(all(0 <= t[k] < 66 for t in tasks for k in ("source_node_id", "compute_node_id", "result_node_id")))
        return tasks, summary

    def test_f1_roles(self):
        tasks, summary = self.read("leo-66-120s-f1", 20)
        self.assertEqual(Counter(t["compute_node_id"] for t in tasks), {0: 5, 11: 5, 22: 5, 33: 3, 44: 1, 55: 1})
        self.assertEqual(summary["post_recovery_task_ids"], [5, 10, 15])
        self.assertEqual(summary["risk_only_task_ids"], [16, 17, 18])

    def test_f2_roles(self):
        _, summary = self.read("leo-66-1000s-f2", 8)
        self.assertEqual(summary["orbit_start_offset_s"], 302)
        self.assertEqual(summary["long_task_compute_node_ids"], [17, 16])
        self.assertEqual(summary["follow_up_task_ids"], [2, 4])
        self.assertEqual(summary["control_task_ids"], [7, 8])

    def test_joint_roles(self):
        tasks, summary = self.read("leo-66-1000s-n4b-joint", 100)
        counts = Counter(t["compute_node_id"] for t in tasks)
        self.assertEqual({n: counts[n] for n in (0, 11, 22, 33, 44)}, {0: 7, 11: 7, 22: 7, 33: 5, 44: 4})
        self.assertEqual(summary["post_recovery_task_ids"], [7, 14, 21])
        self.assertEqual(len(summary["distributed_control_task_ids"]), 62)
        reserved = {0, 4, 5, 11, 16, 17, 22, 28, 33, 44}
        self.assertTrue(all(t["compute_node_id"] not in reserved for t in tasks[38:]))


if __name__ == "__main__":
    unittest.main()
