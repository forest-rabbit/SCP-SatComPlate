"""Offline composition candidates; no network, fault or N5 execution."""

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
        cls.cases = {}
        for candidate in ("V2-1500", "C1000", "C800", "C600"):
            attributes = PREVIEW["preview_attributes"]("n4c-g1-66", candidate)
            summary, rows = PREVIEW["summarize_attributes"](attributes)
            cls.cases[candidate] = (attributes, summary, rows)
        cls.attributes, cls.summary, cls.rows = cls.cases["V2-1500"]

    def test_exact_class_input_tail_and_raw_array_budgets(self):
        for candidate, count in (("V2-1500", 1500), ("C1000", 1000), ("C800", 800), ("C600", 600)):
            with self.subTest(candidate=candidate):
                _, summary, rows = self.cases[candidate]
                self.assertEqual(len(rows), count)
                self.assertEqual(Counter(row["task_profile"] for row in rows),
                                 dict(zip(model.TASK_PROFILES, (count // 10 * n for n in (3, 3, 3, 1)))))
                self.assertEqual(sum(row["input_bytes"] for row in rows), 81_750_000_000)
                tail_counts = (15, 30) if candidate == "V2-1500" else (10, 20)
                self.assertEqual(summary["tail_counts"], dict(zip(("1000000000", "500000000"), tail_counts)))
                by_type = summary["tail_counts_by_profile"]
                self.assertEqual(by_type["dense-image"], dict(zip(("1000000000", "500000000"),
                                                                            (4, 7) if count == 1500 else (2, 5))))
                self.assertEqual(by_type["compression"], dict(zip(("1000000000", "500000000"),
                                                                            (11, 23) if count == 1500 else (8, 15))))
                self.assertEqual(by_type["sparse-inference"], {"1000000000": 0, "500000000": 0})
                self.assertEqual(summary["large_image_tasks"]["input_bytes"],
                                 30_000_000_000 if count == 1500 else 20_000_000_000)
                self.assertTrue(all(row["input_bytes"] % 8 == 0 for row in rows
                                    if row["task_profile"] in ("dense-image", "compression")))
                self.assertTrue(all(row["output_bytes"] > 0 for row in rows))
                for row in rows:
                    if row["task_profile"] != "llm" and row["input_bytes"] < 500_000_000:
                        self.assertTrue(1 << 20 <= row["input_bytes"] <= 300_000_000)

    def test_llm_small_serialized_inputs_time_and_kv_ledgers(self):
        self.assertTrue(self.summary["llm_5_to_10_seconds_target_met"])
        llm_pairs = []
        for attributes, summary, rows in self.cases.values():
            self.assertTrue(summary["llm_5_to_10_seconds_target_met"])
            requests = {task["task_id"]: task for task in attributes if task["task_profile"] == "llm"}
            llm_pairs.extend((row, requests[row["task_id"]]["request_json"])
                             for row in rows if row["task_profile"] == "llm")
        for row, request in llm_pairs:
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
        for task in (task for attributes, _, _ in self.cases.values() for task in attributes):
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

    def test_candidates_preserve_mapping_and_are_deterministic_across_seeds(self):
        for candidate, (attributes, summary, rows) in self.cases.items():
            self.assertEqual(PREVIEW["preview_attributes"]("n4c-g1-66", candidate), attributes)
            alternate = PREVIEW["preview_attributes"]("alternate-composition-seed", candidate)
            self.assertNotEqual(alternate, attributes)
            other, _ = PREVIEW["summarize_attributes"](alternate)
            self.assertEqual(other["total_input_bytes"], 81_750_000_000)
            self.assertEqual(other["task_count"], summary["task_count"])
            self.assertEqual(other["tail_counts_by_profile"], summary["tail_counts_by_profile"])
            self.assertTrue(other["llm_5_to_10_seconds_target_met"])
            self.assertEqual(PREVIEW["summarize_attributes"](list(reversed(attributes))), (summary, rows))
            for row in rows:
                if row["task_profile"] != "llm":
                    budget = model.image_budget(row["task_profile"], row["input_bytes"], str(row["task_id"]))
                    for key, expected in PREVIEW["budget_row"](row["task_id"], budget).items():
                        self.assertEqual(row[key], expected)
        for bad in ("C700", "", 800, None, []):
            with self.assertRaises(ValueError):
                PREVIEW["preview_attributes"]("n4c-g1-66", bad)

    def test_population_statistics_separate_ordinary_and_large_images(self):
        for candidate, (_, summary, rows) in self.cases.items():
            images = [row for row in rows if row["task_profile"] != "llm"]
            ordinary = [row for row in images if row["input_bytes"] < 500_000_000]
            large = [row for row in images if row["input_bytes"] >= 500_000_000]
            self.assertEqual(summary["image_population"], PREVIEW["population_summary"](images))
            self.assertEqual(summary["ordinary_images"]["all"], PREVIEW["population_summary"](ordinary))
            self.assertEqual(len(ordinary), len(images) - len(large))
            self.assertEqual(summary["large_image_tasks"]["count"], len(large))
            self.assertEqual(summary["large_image_tasks"]["input_ratio"],
                             sum(row["input_bytes"] for row in large) / 81_750_000_000)
            for profile, group in summary["classes"].items():
                selected = [row for row in rows if row["task_profile"] == profile]
                for key, expected in PREVIEW["population_summary"](selected).items():
                    self.assertEqual(group[key], expected)
                self.assertEqual(group["total_variable_state_bytes"], sum(row["k_variable_bytes"] for row in selected))
                self.assertEqual(group["output_bytes"], sum(row["output_bytes"] for row in selected))
                if profile != "llm":
                    self.assertEqual(summary["ordinary_images"]["by_profile"][profile],
                                     PREVIEW["population_summary"]([row for row in ordinary
                                                                    if row["task_profile"] == profile]))
            demand = summary["service_demand"]
            self.assertEqual(demand["all"]["total_work_units"],
                             sum(group["total_work_units"] for group in summary["classes"].values()))
            self.assertAlmostEqual(demand["llm_share"], demand["llm"]["total_service_demand_seconds"] /
                                   demand["all"]["total_service_demand_seconds"])
        empty = PREVIEW["population_summary"]([])
        self.assertIsNone(empty["input_size_bytes"])
        self.assertIsNone(empty["service_time_seconds"])

    def test_v2_reference_default_and_frozen_totals_remain_unchanged(self):
        self.assertEqual(PREVIEW["preview_attributes"]("n4c-g1-66"), self.attributes)
        self.assertEqual(self.summary["total_compute_work_units"], 236_714_917)
        self.assertEqual(self.summary["total_output_bytes"], 45_768_636_818)
        self.assertEqual(self.summary["total_variable_state_bytes"], 176_611_226_134)

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
            for candidate, (_, summary, _) in self.cases.items():
                first, second = root / candidate / "first", root / candidate / "second"
                options = [] if candidate == "V2-1500" else ["--candidate", candidate]
                for path in (first, second):
                    result = subprocess.run([sys.executable, str(SCRIPT), *options, "--output-dir", str(path)],
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("G1 approval still required", result.stdout)
                expected = {"summary.json", "task-budgets.csv", "representative-budgets.csv",
                            "state-budget-checks.csv", "llm-requests.json", "execution.json"}
                self.assertEqual({item.name for item in first.iterdir()}, expected)
                self.assertEqual({item.name for item in second.iterdir()}, expected)
                for name in expected - {"execution.json"}:
                    self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
                self.assertEqual(json.loads((first / "summary.json").read_text())["workload_candidate"], candidate)
                rows = list(csv.DictReader(io.StringIO((first / "task-budgets.csv").read_text())))
                self.assertEqual(len(rows), summary["task_count"])
                for forbidden in ("source_node_id", "result_node_id", "arrival_time_ns", "deadline_ns"):
                    self.assertNotIn(forbidden, rows[0])
                saved = (first / "summary.json").read_bytes()
                result = subprocess.run([sys.executable, str(SCRIPT), *options, "--output-dir", str(first)],
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(saved, (first / "summary.json").read_bytes())
            invalid = root / "invalid"
            result = subprocess.run([sys.executable, str(SCRIPT), "--candidate", "C700", "--output-dir", str(invalid)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(invalid.exists())


if __name__ == "__main__":
    unittest.main()
