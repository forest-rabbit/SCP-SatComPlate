"""Formal C800 packaging must preserve G1 business attributes exactly."""

from collections import Counter
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2]
GENERATION = MODULE / "tools/generation"
sys.path.insert(0, str(GENERATION))
GENERATOR = runpy.run_path(str(GENERATION / "generate-task-workload.py"))
PREVIEW = runpy.run_path(str(GENERATION / "preview-n4c-workload.py"))


class FormalWorkloadTests(unittest.TestCase):
    def setUp(self):
        self.profile_path = MODULE / "input/examples/leo-66-1000s-n4c/compute-profile.json"
        self.profile = json.loads(self.profile_path.read_text())["compute_nodes"]

    def test_exact_g1_attributes_and_valid_placement(self):
        build = GENERATOR["build_n4c_c800_workload"]
        trace, summary = build(list(range(66)), self.profile, "n4c-g1-66")
        self.assertEqual((trace, summary), build(list(reversed(range(66))),
                                               list(reversed(self.profile)), "n4c-g1-66"))
        _, rows = PREVIEW["summarize_attributes"](
            PREVIEW["preview_attributes"]("n4c-g1-66", "C800"))
        fields = ("task_id", "task_profile", "input_bytes", "output_bytes", "compute_work_units")
        self.assertEqual([{k: t[k] for k in fields} for t in trace["tasks"]],
                         [{k: t[k] for k in fields} for t in rows])
        self.assertEqual(Counter(t["task_profile"] for t in trace["tasks"]),
                         {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80})
        self.assertEqual(sum(t["input_bytes"] for t in trace["tasks"]), 81_750_000_000)
        self.assertEqual(sum(t["compute_work_units"] for t in trace["tasks"]), 183_958_466)
        self.assertEqual(sum(t["output_bytes"] for t in trace["tasks"]), 44_076_569_084)
        counts = Counter(t["compute_node_id"] for t in trace["tasks"])
        self.assertEqual(set(counts.values()), {12, 13})
        for task in trace["tasks"]:
            self.assertNotEqual(task["source_node_id"], task["compute_node_id"])
            self.assertNotEqual(task["result_node_id"], task["compute_node_id"])
            self.assertTrue(1_000_000_000 <= task["arrival_time_ns"] <= 600_000_000_000)
            self.assertTrue(all(0 <= task[k] < 66 for k in
                                ("source_node_id", "compute_node_id", "result_node_id")))
            self.assertFalse(any("deadline" in key for key in task))

    def test_wrong_compute_profile_rejected(self):
        bad = [dict(node) for node in self.profile]
        bad[0]["compute_rate_work_units_per_second"] = 1_500_000
        with self.assertRaises(ValueError):
            GENERATOR["build_n4c_c800_workload"](list(range(66)), bad, "n4c-g1-66")

    def test_cli_determinism(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nodes = root / "nodes.json"
            nodes.write_text(json.dumps({"nodes": [{"node_id": i, "node_type": "sat"}
                                                   for i in range(66)]}))
            outputs = []
            for index in range(2):
                trace, summary = root / f"trace{index}.json", root / f"summary{index}.json"
                subprocess.run([sys.executable, str(GENERATION / "generate-task-workload.py"),
                                "--profile=n4c-c800", f"--nodes-file={nodes}",
                                f"--compute-profile={self.profile_path}", "--seed=n4c-g1-66",
                                f"--output-task-trace={trace}", f"--output-workload-summary={summary}"],
                               check=True, capture_output=True)
                outputs.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(*outputs)


if __name__ == "__main__":
    unittest.main()
