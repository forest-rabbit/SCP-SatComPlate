"""Offline 1500-task preview invariants; no network or fault simulation."""

from collections import Counter
import csv
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest


GENERATION = Path(__file__).resolve().parents[2] / "tools/generation"
sys.path.insert(0, str(GENERATION))
import task_workload_model as model

SCRIPT = GENERATION / "preview-n4c-workload.py"
PREVIEW = runpy.run_path(str(SCRIPT))


class WorkloadPreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.attributes = PREVIEW["preview_attributes"]("n4c-g1-66")
        cls.summary, cls.rows = PREVIEW["summarize_attributes"](cls.attributes)

    def test_exact_class_input_tail_and_raw_array_budgets(self):
        self.assertEqual(len(self.rows), 1500)
        self.assertEqual(Counter(row["task_profile"] for row in self.rows),
                         {"dense-image": 450, "sparse-inference": 450, "compression": 450, "llm": 150})
        self.assertEqual(sum(row["input_bytes"] for row in self.rows), 81_750_000_000)
        self.assertEqual(self.summary["tail_counts"], {"1000000000": 15, "500000000": 30})
        self.assertTrue(all(row["input_bytes"] % 8 == 0 for row in self.rows
                            if row["task_profile"] in ("dense-image", "compression")))
        self.assertTrue(all(row["output_bytes"] > 0 for row in self.rows))
        for row in self.rows:
            if row["task_profile"] != "llm" and row["input_bytes"] < 500_000_000:
                self.assertTrue(1 << 20 <= row["input_bytes"] <= 300_000_000)

    def test_llm_small_serialized_inputs_time_and_kv_ledgers(self):
        self.assertTrue(self.summary["llm_5_to_10_seconds_target_met"])
        llm_rows = [row for row in self.rows if row["task_profile"] == "llm"]
        requests = {task["task_id"]: task for task in self.attributes if task["task_profile"] == "llm"}
        for row in llm_rows:
            request = requests[row["task_id"]]["request_json"]
            self.assertEqual(row["input_bytes"], len(request.encode("utf-8")))
            self.assertTrue(0 < row["input_bytes"] < 4096)
            self.assertEqual(json.loads(request)["max_new_tokens"], row["generation_tokens"])
            self.assertEqual(row["cached_tokens"], row["prompt_tokens"] + row["generation_tokens"])
            self.assertTrue(5000 <= row["cached_tokens"] <= 10000)
            self.assertEqual(row["k_variable_bytes"], row["cached_tokens"] * 114_688)
            self.assertEqual(row["output_bytes"], 4 * row["generation_tokens"])
            self.assertEqual(row["compute_work_units"], 100 * row["cached_tokens"])
            self.assertTrue(5e9 <= row["reference_service_time_ns"] <= 10e9)
            self.assertIsNone(row["rho_variable"])
        # Rate sensitivity must expose a failed target, not silently retune WU.
        faster, _ = PREVIEW["summarize_attributes"](self.attributes, 200_000, 100)
        self.assertFalse(faster["llm_5_to_10_seconds_target_met"])
        paired, _ = PREVIEW["summarize_attributes"](self.attributes, 200_000, 200)
        self.assertTrue(paired["llm_5_to_10_seconds_target_met"])
        for seed in ("n4c-g1-66-alternate", "another-seed"):
            summary, rows = PREVIEW["summarize_attributes"](PREVIEW["preview_attributes"](seed))
            self.assertTrue(summary["llm_5_to_10_seconds_target_met"])
            self.assertEqual(sum(row["input_bytes"] for row in rows), 81_750_000_000)

    def test_reordering_and_task_set_changes_do_not_change_fixed_attributes_work(self):
        self.assertEqual(PREVIEW["summarize_attributes"](list(reversed(self.attributes))),
                         (self.summary, self.rows))
        _, subset = PREVIEW["summarize_attributes"](self.attributes[::29])
        original = {row["task_id"]: row for row in self.rows}
        self.assertTrue(all(row == original[row["task_id"]] for row in subset))
        self.assertEqual(self.attributes, PREVIEW["preview_attributes"]("n4c-g1-66"))
        self.assertNotEqual(self.attributes, PREVIEW["preview_attributes"]("another-seed"))
        with self.assertRaises(ValueError):
            PREVIEW["summarize_attributes"]([self.attributes[0], self.attributes[0]])

    def test_every_preview_task_conserves_state_for_three_budget_partitions(self):
        parameters = model.LlmParameters()
        for task in self.attributes:
            budget = PREVIEW["task_budget"](task, parameters)
            ends, _ = PREVIEW["preview_unit_ends"](budget)
            for interval in (50, 100, 200):
                records = model.state_budget_points(budget, ends, interval)
                self.assertEqual(sum(row.delta_variable_bytes for row in records), budget.k_variable_bytes)
                self.assertEqual(sum(row.delta_work_units for row in records), budget.compute_work_units)
                self.assertEqual(sum(row.delta_total_bytes for row in records),
                                 budget.k_variable_bytes + len(records) * budget.header_bytes)
                self.assertEqual(records[-1].completed_extent, budget.extent)
                self.assertEqual(len({row.completed_extent for row in records}), len(records))
                self.assertTrue(all(row.delta_work_units > 0 and row.completed_extent in ends for row in records))

    def test_only_three_state_checks_and_node_demand(self):
        checks = PREVIEW["state_budget_check_rows"](model.LlmParameters())
        self.assertEqual(len(checks), 4 * 3)
        self.assertEqual({row["check_step_percent"] for row in checks}, {5, 10, 20})
        self.assertNotIn("checkpoint_grid_rows", PREVIEW)
        for row in checks:
            self.assertEqual(row["accounted_variable_plus_repeated_h_bytes"], row["k_variable_bytes"] +
                             row["budget_point_count"] * row["h_bytes"])
            self.assertGreaterEqual(row["max_alignment_overshoot_percentage_points"], -1e-12)
            self.assertLess(row["max_work_rounding_error_wu"], 1)
        self.assertEqual(sum(node["task_count"] for node in self.summary["node_demand_preview"]), 1500)
        demand = sum(node["service_demand_seconds"] for node in self.summary["node_demand_preview"])
        self.assertAlmostEqual(demand, sum(row["reference_service_time_ns"] for row in self.rows) / 1e9)

    def test_service_demand_shares_and_short_task_band_endpoints(self):
        groups = self.summary["service_demand"]
        self.assertEqual(groups["all"]["total_work_units"], self.summary["total_compute_work_units"])
        self.assertEqual(groups["all"]["total_work_units"], groups["images"]["total_work_units"] +
                         groups["llm"]["total_work_units"])
        self.assertAlmostEqual(groups["llm_share"], groups["llm"]["total_service_demand_seconds"] /
                               groups["all"]["total_service_demand_seconds"])
        for profile, group in self.summary["classes"].items():
            self.assertAlmostEqual(group["total_service_demand_seconds"], group["total_work_units"] / 100_000)
            self.assertEqual(group["service_bands"], PREVIEW["service_bands"](
                [row for row in self.rows if row["task_profile"] == profile]))
        images = [row for row in self.rows if row["task_profile"] != "llm"]
        self.assertEqual(self.summary["image_service_bands"], PREVIEW["service_bands"](images))
        self.assertGreater(self.summary["image_service_bands"]["lt_0_5s"]["count"], 0)
        boundaries = [499_999_999, 500_000_000, 999_999_999, 1_000_000_000,
                      1_999_999_999, 2_000_000_000, 4_999_999_999,
                      5_000_000_000, 10_000_000_000, 10_000_000_001]
        bins = PREVIEW["service_bands"]([{"reference_service_time_ns": t} for t in boundaries])
        self.assertEqual([entry["count"] for entry in bins.values()], [1, 3, 5, 2, 2, 1])
        self.assertEqual([entry["ratio"] for entry in bins.values()], [.1, .3, .5, .2, .2, .1])

    def test_cli_reproducibility_no_runtime_trace_and_existing_output_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first", root / "second"
            for path in (first, second):
                result = subprocess.run([sys.executable, str(SCRIPT), "--output-dir", str(path)],
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("G1 approval still required", result.stdout)
            expected = {"summary.json", "task-budgets.csv", "representative-budgets.csv",
                        "state-budget-checks.csv", "llm-requests.json", "execution.json"}
            self.assertEqual({item.name for item in first.iterdir()}, expected)
            for name in expected - {"execution.json"}:
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
            rows = list(csv.DictReader(io.StringIO((first / "task-budgets.csv").read_text())))
            self.assertEqual(len(rows), 1500)
            self.assertNotIn("source_node_id", rows[0])
            self.assertNotIn("arrival_time_ns", rows[0])
            self.assertNotIn("deadline_ns", rows[0])
            saved = (first / "summary.json").read_bytes()
            result = subprocess.run([sys.executable, str(SCRIPT), "--output-dir", str(first)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(saved, (first / "summary.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
