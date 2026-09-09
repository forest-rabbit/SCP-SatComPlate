"""Fast G3 workload/horizon/isolation checks, with no full simulation or fault search."""
from collections import Counter
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[2]
GEN = MODULE / "tools/generation"
sys.path.insert(0, str(GEN))
PREVIEW = runpy.run_path(str(GEN / "preview-n4c-workload.py"))
GENERATOR = runpy.run_path(str(GEN / "generate-task-workload.py"))
HOTSPOT = runpy.run_path(str(GEN / "n4c_hotspot.py"))
AUDIT = runpy.run_path(str(MODULE / "tools/validation/summarize-n4c-g3.py"))


class TruncNormalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = json.loads((MODULE / "input/examples/leo-66-1000s-n4c/compute-profile.json").read_text())["compute_nodes"]
        cls.attributes = PREVIEW["preview_attributes"]("n4c-g1-66", "C800-TruncNormal")
        cls.trace, cls.summary = GENERATOR["build_n4c_c800_workload"](
            list(range(66)), cls.profile, "n4c-g1-66", "C800-TruncNormal")

    def test_counts_bounds_repeat_and_distribution(self):
        self.assertEqual(self.attributes, PREVIEW["preview_attributes"]("n4c-g1-66", "C800-TruncNormal"))
        self.assertEqual(Counter(t["task_profile"] for t in self.attributes),
                         {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80})
        ordinary = [t for t in self.attributes if t["task_profile"] != "llm" and t["input_bytes"] < 500_000_000]
        self.assertEqual(len(ordinary), 700)
        for t in ordinary:
            self.assertTrue(50_000_000 <= t["input_bytes"] < 500_000_000)
            if t["task_profile"] in ("dense-image", "compression"):
                self.assertEqual(t["input_bytes"] % 8, 0)
        for size in (500_000_000, 1_000_000_000):
            self.assertEqual(Counter(t["task_profile"] for t in self.attributes if t["input_bytes"] == size),
                             {"compression": 8, "dense-image": 2})
        dist = self.summary["truncated_normal"]["input_size_bytes"]
        self.assertLess(abs(dist["mean"]/1e6 - 188.98034743515714), 15)
        self.assertGreater(dist["p95"], dist["median"])
        self.assertEqual(sum(self.summary["service_time_partition"].values()), 800)

    def test_inverse_cdf_extreme_hashes_and_alignment(self):
        fn = PREVIEW["truncated_normal_size"]
        for value in (0, (1 << 64)-1):
            with patch.dict(fn.__globals__, STABLE_VALUE=lambda *args: value):
                for profile in ("dense-image", "compression", "sparse-inference"):
                    size = fn("boundary", 1, profile)
                    self.assertTrue(50_000_000 <= size < 500_000_000)
                    if profile != "sparse-inference":
                        self.assertEqual(size % 8, 0)

    def test_mapping_state_and_llm_unchanged(self):
        import task_workload_model as model
        old = PREVIEW["preview_attributes"]("n4c-g1-66", "C800")
        self.assertEqual([t for t in old if t["task_profile"] == "llm"],
                         [t for t in self.attributes if t["task_profile"] == "llm"])
        for attr, task in zip(self.attributes, self.trace["tasks"]):
            budget = PREVIEW["task_budget"](attr, model.LlmParameters())
            self.assertEqual(task["compute_work_units"], budget.compute_work_units)
            self.assertEqual(task["output_bytes"], budget.output_bytes)
            if task["task_profile"] != "llm":
                self.assertEqual(task["compute_work_units"], (3*task["input_bytes"]+1999)//2000)
            ends, _ = PREVIEW["preview_unit_ends"](budget)
            for step in (50, 100, 200):
                points = model.state_budget_points(budget, ends, step)
                self.assertEqual(sum(p.delta_variable_bytes for p in points), budget.k_variable_bytes)
                self.assertEqual(sum(p.delta_work_units for p in points), budget.compute_work_units)
                self.assertEqual(sum(p.delta_total_bytes for p in points), budget.k_variable_bytes + len(points)*budget.header_bytes)

    def test_arrival_ranking_and_horizon(self):
        old = json.loads((MODULE / "input/examples/leo-66-1000s-n4c/task-trace.json").read_text())["tasks"]
        order = lambda tasks: [t["task_id"] for t in sorted(tasks, key=lambda t: t["arrival_time_ns"])]
        self.assertEqual(order(old), order(self.trace["tasks"]))
        self.assertTrue(all(10**9 <= t["arrival_time_ns"] <= 900*10**9 for t in self.trace["tasks"]))
        self.assertEqual(self.summary["simulation_duration_s"], 1200)
        self.assertEqual(self.summary["arrival_window_s"], [1, 900])
        for node in self.summary["node_demand_preview"]:
            self.assertEqual(node["demand_over_1200_second_capacity"], node["service_demand_seconds"]/1200)
            self.assertNotIn("demand_over_1000_second_capacity", node)
        check = AUDIT["check_horizon"]
        self.assertEqual(check({"simulation_duration_s": 1200}, {"simulation_duration_s": 1200}, [{"window_end_s": 1200}]), 1200)
        with self.assertRaises(ValueError):
            check({"simulation_duration_s": 1200}, {"simulation_duration_s": 1200}, [{"window_end_s": 1000}])
        with self.assertRaises(ValueError):
            check({"simulation_duration_s": 1000}, {"simulation_duration_s": 1200}, [{"window_end_s": 1200}])

    def test_hotspot_frozen_placement_and_shares(self):
        positions = {t*10**9: {n: (40., (-95., 15., 120., 70.)[n % 4]) for n in range(66)} for t in range(901)}
        args = (self.trace, positions, "n4c-g3-hotspot")
        trace, manifest = HOTSPOT["build_hotspot"](*args, hot_weight=64, regional_limit=1, workload_candidate="C800-TruncNormal")
        self.assertEqual((trace, manifest), HOTSPOT["build_hotspot"](*args, hot_weight=64, regional_limit=1, workload_candidate="C800-TruncNormal"))
        self.assertEqual(manifest["simulation_duration_s"], 1200)
        self.assertTrue(all(0 < manifest["hotspot_shares"][k] < 1 for k in ("task_count", "input_bytes", "work_units_and_service_demand")))
        for before, after in zip(self.trace["tasks"], trace["tasks"]):
            self.assertEqual({k: v for k, v in before.items() if not k.endswith("node_id")},
                             {k: v for k, v in after.items() if not k.endswith("node_id")})
        with self.assertRaises(ValueError):
            HOTSPOT["build_hotspot"](*args, workload_candidate="C800-TruncNormal", f3_plan={})

    @staticmethod
    def task(tid, node, start, finish, size, source=1, result=2):
        return {"task_id": tid, "source_node_id": source, "compute_node_id": node, "result_node_id": result,
            "arrival_time_ns": int((start-1)*1e9), "compute_start_time_ns": int(start*1e9),
            "compute_complete_time_ns": int(finish*1e9), "result_transfer_complete_time_ns": int((finish+.1)*1e9),
            "input_transfer_complete_time_ns": int((start-.5)*1e9), "compute_work_units": int((finish-start)*100000),
            "compute_rate_work_units_per_second": 100000, "input_bytes": size, "final_state": "COMPLETED"}

    def test_isolated_f3_margin_future_obligations_and_determinism(self):
        ordinary = self.task(1, 4, 1, 2, 100)
        victim = self.task(2, 4, 40, 55, 1_000_000_000)
        transfers = [{"transfer_id": 1, "arrival_time_ns": 0, "completion_time_ns": 10**9, "terminal_state": "COMPLETED"}]
        select = HOTSPOT["select_isolated_f3_from_none"]
        plan = select([ordinary, victim], transfers, "test")
        self.assertEqual(plan, select([victim, ordinary], transfers, "test"))
        self.assertEqual(plan["none_progress"], .7)
        self.assertEqual(plan["required_endpoint_margin_s"], 30)
        self.assertEqual(plan["transit_quiet"]["status"], "proven-global-quiet")
        for extra in (self.task(3, 4, 60, 61, 100), self.task(3, 6, 60, 61, 100, result=4),
                      self.task(3, 6, 43, 44, 100, result=4)):
            with self.assertRaisesRegex(ValueError, "no isolated"):
                select([ordinary, victim, extra], transfers, "test")

    def test_transit_uses_receiver_completion_and_discloses_unknown(self):
        tasks = [self.task(1, 4, 1, 2, 100), self.task(2, 4, 40, 55, 1_000_000_000)]
        transfer = {"transfer_id": 9, "arrival_time_ns": 0, "last_send_time_ns": 10**9,
                    "completion_time_ns": 100*10**9, "terminal_state": "COMPLETED"}
        plan = HOTSPOT["select_isolated_f3_from_none"](tasks, [transfer], "test")
        self.assertEqual(plan["transit_quiet"]["status"], "unknown")
        self.assertEqual(plan["transit_quiet"]["active_transfer_ids"], [9])

    def test_f3_twenty_second_fallback_and_tail_preference(self):
        transfers = [{"transfer_id": 1, "arrival_time_ns": 0, "completion_time_ns": 10**9, "terminal_state": "COMPLETED"}]
        select = HOTSPOT["select_isolated_f3_from_none"]
        ordinary = self.task(1, 4, 1, 30, 100)
        victim = self.task(2, 4, 40, 55, 1_000_000_000)
        self.assertEqual(select([ordinary, victim], transfers, "test")["required_endpoint_margin_s"], 20)
        late = self.task(1, 4, 1, 36, 100)
        with self.assertRaisesRegex(ValueError, "no isolated"):
            select([late, victim], transfers, "test")
        early = self.task(1, 4, 1, 2, 100)
        other = [self.task(3, 5, 1, 2, 100), self.task(4, 5, 40, 47.5, 500_000_000)]
        self.assertEqual(select([early, victim, *other], transfers, "test")["victim_task_id"], 2)

    def test_cli_byte_identical(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            nodes = root / "nodes.json"
            nodes.write_text(json.dumps({"nodes": [{"node_id": n, "node_type": "sat"} for n in range(66)]}))
            outputs = []
            for i in range(2):
                trace, summary = root/f"tasks{i}.json", root/f"summary{i}.json"
                subprocess.run([sys.executable, str(GEN/"generate-task-workload.py"), "--profile=n4c-c800-truncnormal",
                    f"--nodes-file={nodes}", f"--compute-profile={MODULE}/input/examples/leo-66-1000s-n4c/compute-profile.json",
                    "--seed=n4c-g1-66", f"--output-task-trace={trace}", f"--output-workload-summary={summary}"],
                    check=True, capture_output=True)
                outputs.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(*outputs)

    def test_pressure_diagnostics_exclude_unavailable_draws(self):
        base = {"sampling_eligible": "1", "busy": "1", "temperature_c": "28", "continuous_busy_s": "20",
                "p_f1": ".2", "p_f2": ".001", "p_compute": ".2008", "in_saa": "1"}
        result = AUDIT["fault_pressure_diagnostics"]([base, {**base, "busy": "0"},
                                                     {**base, "sampling_eligible": "0"}])
        self.assertEqual(result["eligible_node_checks"], 2)
        self.assertEqual(result["eligible_busy_node_checks"], 1)
        self.assertEqual(result["eligible_probability_sums"]["p_f1"], .4)
        self.assertEqual(result["eligible_busy_probability_sums"]["p_f2"], .001)
        self.assertEqual(result["busy_f1_p_at_least_10_percent_checks"], 1)


class TruncNormalV3Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = json.loads((MODULE / "input/examples/leo-66-1000s-n4c/compute-profile.json").read_text())["compute_nodes"]
        cls.attributes = PREVIEW["preview_attributes"]("n4c-g1-66", "C800-TruncNormal-v3")
        cls.trace, cls.summary = GENERATOR["build_n4c_c800_workload"](
            list(range(66)), cls.profile, "n4c-g1-66", "C800-TruncNormal-v3")

    def test_anchor_identity_counts_and_natural_tail(self):
        old = PREVIEW["preview_attributes"]("n4c-g1-66", "C800-TruncNormal")
        ids = self.summary["truncated_normal"]["fixed_tail_task_ids"]
        self.assertEqual(ids["500000000"], [t["task_id"] for t in old if t["input_bytes"] == 500_000_000])
        self.assertEqual(ids["1000000000"], [102, 191, 352, 531, 605])
        ordinary = [t for t in self.attributes if t["task_profile"] != "llm" and not t.get("fixed_tail_anchor")]
        self.assertEqual(len(ordinary), 705)
        self.assertEqual(Counter(t["task_profile"] for t in self.attributes),
                         {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80})
        for t in ordinary:
            self.assertTrue(50_000_000 <= t["input_bytes"] < 1_000_000_000)
            if t["task_profile"] != "sparse-inference":
                self.assertEqual(t["input_bytes"] % 8, 0)
        self.assertTrue(any(t["input_bytes"] > 500_000_000 and t["task_profile"] == "sparse-inference" for t in ordinary))
        self.assertEqual(self.summary["truncated_normal"]["size_counts"]["gt_500MB"], 22)
        self.assertEqual(sum(self.summary["service_time_partition"].values()), 800)
        self.assertEqual(self.summary["service_time_partition"]["eq_15s"], 5)

    def test_retained_quantiles_and_extreme_hashes(self):
        from statistics import NormalDist
        normal = NormalDist()
        fn = PREVIEW["truncated_normal_size"]
        for t in self.attributes:
            if t["task_profile"] == "llm" or t.get("fixed_tail_anchor"):
                continue
            u = ((PREVIEW["STABLE_VALUE"]("n4c-g1-66", t["task_id"], "n4c-input-truncnorm") >> 12)+.5)/2**52
            a, b = normal.cdf((50-240)/130), normal.cdf((1000-240)/130)
            expected = int((240+130*normal.inv_cdf(a+u*(b-a)))*1e6)
            if t["task_profile"] != "sparse-inference":
                expected -= expected % 8
            self.assertEqual(t["input_bytes"], expected)
        for value in (0, (1 << 64)-1):
            with patch.dict(fn.__globals__, STABLE_VALUE=lambda *args: value):
                for profile in ("dense-image", "compression", "sparse-inference"):
                    self.assertTrue(50_000_000 <= fn("edge", 1, profile, v3=True) < 1_000_000_000)

    def test_natural_exact_500mb_is_not_an_anchor(self):
        attributes = [dict(t) for t in self.attributes]
        ordinary = next(t for t in attributes if t["task_profile"] == "sparse-inference")
        ordinary["input_bytes"] = 500_000_000
        summary, _ = PREVIEW["summarize_attributes"](attributes, simulation_seconds=1300)
        self.assertEqual(summary["tail_counts"]["500000000"], 10)
        self.assertEqual(summary["tail_counts_by_profile"]["sparse-inference"]["500000000"], 0)
        self.assertEqual(summary["ordinary_images"]["all"]["count"], 705)

    def test_mapping_conservation_and_llm_unchanged(self):
        import task_workload_model as model
        old = PREVIEW["preview_attributes"]("n4c-g1-66", "C800-TruncNormal")
        self.assertEqual([t for t in old if t["task_profile"] == "llm"],
                         [t for t in self.attributes if t["task_profile"] == "llm"])
        for attr, task in zip(self.attributes, self.trace["tasks"]):
            self.assertNotIn("fixed_tail_anchor", task)
            budget = PREVIEW["task_budget"](attr, model.LlmParameters())
            self.assertEqual(task["compute_work_units"], budget.compute_work_units)
            self.assertEqual(task["output_bytes"], budget.output_bytes)
            if task["task_profile"] != "llm":
                self.assertEqual(task["compute_work_units"], (3*task["input_bytes"]+1999)//2000)
            ends, _ = PREVIEW["preview_unit_ends"](budget)
            for step in (50, 100, 200):
                points = model.state_budget_points(budget, ends, step)
                self.assertEqual(sum(p.delta_variable_bytes for p in points), budget.k_variable_bytes)
                self.assertEqual(sum(p.delta_work_units for p in points), budget.compute_work_units)
                self.assertEqual(sum(p.delta_total_bytes for p in points), budget.k_variable_bytes+len(points)*budget.header_bytes)

    def test_arrival_rank_horizon_and_historical_bytes(self):
        old_dir = MODULE / "input/examples/leo-66-1200s-n4c-g3-truncnormal"
        old_trace, old_summary = GENERATOR["build_n4c_c800_workload"](
            list(range(66)), self.profile, "n4c-g1-66", "C800-TruncNormal")
        self.assertEqual(old_trace, json.loads((old_dir / "base-task-trace.json").read_text()))
        self.assertEqual(old_summary, json.loads((old_dir / "workload-summary.json").read_text()))
        order = lambda ts: [t["task_id"] for t in sorted(ts, key=lambda t: t["arrival_time_ns"])]
        self.assertEqual(order(old_trace["tasks"]), order(self.trace["tasks"]))
        self.assertTrue(all(10**9 <= t["arrival_time_ns"] <= 1050*10**9 for t in self.trace["tasks"]))
        self.assertEqual(self.summary["arrival_window_s"], [1, 1050])
        for n in self.summary["node_demand_preview"]:
            self.assertEqual(n["demand_over_1300_second_capacity"], n["service_demand_seconds"]/1300)
        self.assertEqual(AUDIT["check_horizon"]({"simulation_duration_s": 1300}, self.summary,
                                             [{"window_end_s": 1300}]), 1300)
        with self.assertRaises(ValueError):
            AUDIT["check_horizon"]({"simulation_duration_s": 1300}, self.summary, [{"window_end_s": 1200}])

    def test_hotspot_metadata_bounds_and_no_remap(self):
        positions = {t*10**9: {n: (40., (-95., 15., 120., 70.)[n % 4]) for n in range(66)} for t in range(1051)}
        args = (self.trace, positions, "n4c-g3-hotspot")
        kwargs = dict(hot_weight=64, regional_limit=1, workload_candidate="C800-TruncNormal-v3",
                      fixed_tail_task_ids=self.summary["truncated_normal"]["fixed_tail_task_ids"])
        trace, manifest = HOTSPOT["build_hotspot"](*args, **kwargs)
        self.assertEqual((trace, manifest), HOTSPOT["build_hotspot"](*args, **kwargs))
        self.assertEqual(manifest["simulation_duration_s"], 1300)
        self.assertEqual(manifest["arrival_window_s"], [1, 1050])
        self.assertEqual(manifest["hotspot_weight"], 64)
        self.assertTrue(all(0 < manifest["hotspot_shares"][k] < 1 for k in ("task_count", "input_bytes", "work_units_and_service_demand")))
        for a, b in zip(self.trace["tasks"], trace["tasks"]):
            self.assertEqual({k:v for k,v in a.items() if not k.endswith("node_id")},
                             {k:v for k,v in b.items() if not k.endswith("node_id")})
        with self.assertRaisesRegex(ValueError, "no endpoint remapping"):
            HOTSPOT["build_hotspot"](*args, **kwargs, f3_plan={})
        with self.assertRaisesRegex(ValueError, "explicit fixed-tail"):
            HOTSPOT["build_hotspot"](*args, **{**kwargs, "fixed_tail_task_ids": None})

    def test_f3_anchor_priority_before_natural_size(self):
        task = TruncNormalTests.task
        tasks = [task(1, 4, 1, 2, 100), task(2, 4, 40, 55, 1_000_000_000),
                 task(3, 5, 1, 2, 100), task(4, 5, 40, 47.5, 500_000_000),
                 task(5, 6, 1, 2, 100), task(6, 6, 40, 49, 600_000_000)]
        transfers = [{"transfer_id": 1, "arrival_time_ns": 0, "completion_time_ns": 10**9, "terminal_state": "COMPLETED"}]
        select = HOTSPOT["select_isolated_f3_from_none"]
        anchors = {"1000000000": [2], "500000000": [4]}
        self.assertEqual(select(tasks, transfers, "test", anchors)["victim_task_id"], 2)
        self.assertEqual(select(tasks[2:], transfers, "test", anchors)["victim_task_id"], 4)
        self.assertEqual(select(tasks[4:], transfers, "test", anchors)["victim_task_id"], 6)

    def test_cli_byte_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            nodes = root / "nodes.json"
            nodes.write_text(json.dumps({"nodes": [{"node_id": n, "node_type": "sat"} for n in range(66)]}))
            outputs = []
            for i in range(2):
                trace, summary = root/f"tasks{i}.json", root/f"summary{i}.json"
                subprocess.run([sys.executable, str(GEN/"generate-task-workload.py"), "--profile=n4c-c800-truncnormal-v3",
                    f"--nodes-file={nodes}", f"--compute-profile={MODULE}/input/examples/leo-66-1000s-n4c/compute-profile.json",
                    "--seed=n4c-g1-66", f"--output-task-trace={trace}", f"--output-workload-summary={summary}"],
                    check=True, capture_output=True)
                outputs.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(*outputs)


if __name__ == "__main__":
    unittest.main()
