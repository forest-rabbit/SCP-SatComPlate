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

    def test_109g_paired_conservation_and_state_remapping(self):
        import task_workload_model as model
        build = GENERATOR["build_n4c_c800_workload"]
        old, _ = build(list(range(66)), self.profile, "n4c-g1-66")
        frozen = json.loads((MODULE / "input/examples/leo-66-1000s-n4c/task-trace.json").read_text())
        self.assertEqual(old, frozen)
        new, summary = build(list(range(66)), self.profile, "n4c-g1-66", "C800-109G")
        self.assertEqual((new, summary), build(list(range(66)), self.profile, "n4c-g1-66", "C800-109G"))
        self.assertEqual(sum(t["input_bytes"] for t in new["tasks"]), 109_000_000_000)
        self.assertEqual(summary["ordinary_image_input_bytes"],
                         109_000_000_000 - 20_000_000_000 - summary["frozen_llm_input_bytes"])
        self.assertEqual(len(new["tasks"]), 800)
        self.assertEqual(Counter(t["task_profile"] for t in new["tasks"]),
                         Counter(t["task_profile"] for t in old["tasks"]))
        self.assertEqual(Counter(t["input_bytes"] for t in new["tasks"] if t["input_bytes"] >= 500_000_000),
                         {500_000_000: 20, 1_000_000_000: 10})
        allowed = {"input_bytes", "output_bytes", "compute_work_units"}
        for before, after in zip(old["tasks"], new["tasks"]):
            self.assertEqual(set(before), set(after))
            self.assertEqual({k: v for k, v in before.items() if k not in allowed},
                             {k: v for k, v in after.items() if k not in allowed})
            if before["task_profile"] == "llm" or before["input_bytes"] >= 500_000_000:
                self.assertEqual(before, after)
            else:
                self.assertTrue(1 << 20 <= after["input_bytes"] <= 300_000_000)
                self.assertEqual(after["compute_work_units"], (3 * after["input_bytes"] + 1999) // 2000)
            for key, value in after.items():
                if key != "task_profile":
                    self.assertIs(type(value), int)
                    self.assertTrue(0 <= value <= (1 << 64) - 1)
        attributes = PREVIEW["preview_attributes"]("n4c-g1-66", "C800-109G")
        old_attributes = PREVIEW["preview_attributes"]("n4c-g1-66", "C800")
        self.assertEqual([t for t in attributes if t["task_profile"] == "llm"],
                         [t for t in old_attributes if t["task_profile"] == "llm"])
        state_total = 0
        for attribute, task in zip(attributes, new["tasks"]):
            budget = PREVIEW["task_budget"](attribute, model.LlmParameters())
            state_total += budget.k_variable_bytes
            self.assertEqual(task["output_bytes"], budget.output_bytes)
            self.assertEqual(task["compute_work_units"], budget.compute_work_units)
            ends, _ = PREVIEW["preview_unit_ends"](budget)
            for step in (50, 100, 200):
                points = model.state_budget_points(budget, ends, step)
                self.assertEqual(sum(p.delta_variable_bytes for p in points), budget.k_variable_bytes)
                self.assertEqual(sum(p.delta_work_units for p in points), budget.compute_work_units)
                self.assertEqual(sum(p.delta_total_bytes for p in points),
                                 budget.k_variable_bytes + len(points) * budget.header_bytes)
        self.assertEqual(summary["total_variable_state_bytes"], state_total)

    def test_cli_determinism(self):
        for profile in ("n4c-c800", "n4c-c800-109g"):
            with self.subTest(profile=profile):
                self.check_cli_determinism(profile)

    def check_cli_determinism(self, profile):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nodes = root / "nodes.json"
            nodes.write_text(json.dumps({"nodes": [{"node_id": i, "node_type": "sat"}
                                                   for i in range(66)]}))
            outputs = []
            for index in range(2):
                trace, summary = root / f"trace{index}.json", root / f"summary{index}.json"
                subprocess.run([sys.executable, str(GENERATION / "generate-task-workload.py"),
                                f"--profile={profile}", f"--nodes-file={nodes}",
                                f"--compute-profile={self.profile_path}", "--seed=n4c-g1-66",
                                f"--output-task-trace={trace}", f"--output-workload-summary={summary}"],
                               check=True, capture_output=True)
                outputs.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(*outputs)


if __name__ == "__main__":
    unittest.main()
