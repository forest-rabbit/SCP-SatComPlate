"""Small placement-contract tests; geometry is synthetic, not an orbit implementation."""
import json
from pathlib import Path
import runpy
import unittest

PLATFORM = Path(__file__).resolve().parents[2]
MODULE = runpy.run_path(str(PLATFORM / "tools/generation/n4c_hotspot.py"))


class HotspotTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = json.loads((PLATFORM / "input/examples/leo-66-1000s-n4c/task-trace.json").read_text())
        # Synthetic coordinates exercise placement, never thermal/radiation selection.
        cls.positions = {t * 10**9: {n: (40., (-95., 15., 120., 70.)[n % 4])
                                    for n in range(66)} for t in range(602)}

    def build(self, **kwargs):
        return MODULE["build_hotspot"](self.base, self.positions, "g3-test", **kwargs)

    def test_repeat_conservation_without_reserved_f3(self):
        trace, manifest = self.build()
        self.assertEqual(json.dumps((trace, manifest)), json.dumps(self.build()))
        base = {t["task_id"]: t for t in self.base["tasks"]}
        self.assertIsNone(manifest["f3"])
        for task in trace["tasks"]:
            self.assertEqual({k: task[k] for k in (*MODULE["BUSINESS"], "arrival_time_ns")},
                             {k: base[task["task_id"]][k] for k in (*MODULE["BUSINESS"], "arrival_time_ns")})
            self.assertNotEqual(task["compute_node_id"], task["source_node_id"])
            self.assertNotEqual(task["compute_node_id"], task["result_node_id"])
        self.assertLess(manifest["maximum_position_age_ns"], 10**9)

    def test_final_placement_preserves_none_prefix(self):
        trace, _ = self.build()
        none = []
        free = {n: 0 for n in range(66)}
        for task in sorted(trace["tasks"], key=lambda t: (t["arrival_time_ns"], t["task_id"])):
            arrived = task["arrival_time_ns"] + 50_000_000
            start = max(arrived, free[task["compute_node_id"]])
            end = start + (task["compute_work_units"] * 10**9 + 99999) // 100000
            free[task["compute_node_id"]] = end
            none.append({**task, "final_state": "COMPLETED", "compute_rate_work_units_per_second": 100000,
                         "input_transfer_complete_time_ns": arrived, "compute_start_time_ns": start,
                         "compute_complete_time_ns": end, "result_transfer_complete_time_ns": end+50_000_000})
        plan = MODULE["select_f3_from_none"](none, "g3-test")
        self.assertEqual(plan, MODULE["select_f3_from_none"](list(reversed(none)), "g3-test"))
        final, manifest = self.build(f3_plan=plan, none_tasks=none)
        original = {t["task_id"]: t for t in trace["tasks"]}
        self.assertGreater(plan["input_bytes"], 200_000_000)
        self.assertGreater(plan["none_progress"], .5)
        self.assertLess(plan["ordinary_release_time_ns"], plan["time_ns"])
        for task in final["tasks"]:
            if task["arrival_time_ns"] < plan["time_ns"]:
                self.assertEqual(task, original[task["task_id"]])
            else:
                self.assertNotIn(plan["node_id"], [task[k] for k in
                    ("source_node_id", "compute_node_id", "result_node_id")])
        self.assertEqual(plan, manifest["f3"])
        with self.assertRaisesRegex(ValueError, "wrong none/seed/weight"):
            self.build(hot_weight=128, f3_plan=plan, none_tasks=none)

    def test_large_f3_blocks_pending_result_and_queue(self):
        def task(task_id, source, compute, result, arrival, start, finish, size=100):
            return {"task_id": task_id, "source_node_id": source, "compute_node_id": compute,
                    "result_node_id": result, "arrival_time_ns": int(arrival*1e9),
                    "input_bytes": size, "compute_work_units": int((finish-start)*100000),
                    "compute_rate_work_units_per_second": 100000, "final_state": "COMPLETED",
                    "input_transfer_complete_time_ns": int((arrival+.1)*1e9),
                    "compute_start_time_ns": int(start*1e9), "compute_complete_time_ns": int(finish*1e9),
                    "result_transfer_complete_time_ns": int((finish+.1)*1e9)}
        ordinary = task(1, 4, 3, 2, .1, 1, 2)
        victim = task(2, 1, 4, 2, 9, 10, 17.5, 500_000_000)
        plan = MODULE["select_f3_from_none"]([ordinary, victim], "test")
        self.assertEqual(plan["victim_task_id"], 2)
        self.assertAlmostEqual(plan["none_progress"], .7)
        self.assertEqual(plan["ordinary_role"], "source")
        # Ordinary images >200 MB must not displace an eligible frozen tail,
        # regardless of deterministic tie-break ordering or input row order.
        alternative = task(4, 1, 3, 2, 19, 20, 23.5, 240_000_000)
        alternative["task_profile"] = "dense-image"
        victim["task_profile"] = "compression"
        for seed in ("test", "another", "third"):
            selected = MODULE["select_f3_from_none"]([alternative, victim, ordinary], seed)
            self.assertEqual(selected["victim_task_id"], 2)
        # RESULT is not active at F3 yet, but its already-arrived owner still needs node 4.
        future_result = task(3, 1, 5, 4, 10, 20, 22)
        queued = task(3, 1, 4, 2, 10, 18, 19)
        for unsafe in (future_result, queued):
            with self.assertRaisesRegex(ValueError, "no large single-victim"):
                MODULE["select_f3_from_none"]([ordinary, victim, unsafe], "test")
        victim["input_bytes"] = 200_000_000
        with self.assertRaises(ValueError):
            MODULE["select_f3_from_none"]([ordinary, victim], "test")

    def test_weight_limit_and_fallback(self):
        _, low = self.build(hot_weight=4, regional_limit=1)
        _, high = self.build(hot_weight=64, regional_limit=1)
        self.assertGreater(sum(p["weighted_hot_candidate"] for p in high["placements"]),
                           sum(p["weighted_hot_candidate"] for p in low["placements"]))
        _, empty = self.build(regions=(("empty", -5, 5, -20, -10),))
        self.assertEqual(empty["empty_region_fallback_counts"], {"empty": 800})
        self.assertEqual(empty["by_region"]["background"]["task_count"], 800)

    def test_missing_slices_and_invalid_weight(self):
        with self.assertRaises(ValueError):
            MODULE["build_hotspot"](self.base, {0: self.positions[0]}, "g3-test")
        with self.assertRaises(ValueError):
            self.build(hot_weight=0)

    def test_109g_requires_explicit_budget_variant(self):
        import sys
        sys.path.insert(0, str(PLATFORM / "tools/generation"))
        generator = runpy.run_path(str(PLATFORM / "tools/generation/generate-task-workload.py"))
        profile = json.loads((PLATFORM / "input/examples/leo-66-1000s-n4c/compute-profile.json").read_text())["compute_nodes"]
        base, _ = generator["build_n4c_c800_workload"](list(range(66)), profile, "n4c-g1-66", "C800-109G")
        with self.assertRaisesRegex(ValueError, "business budgets"):
            MODULE["build_hotspot"](base, self.positions, "g3-test")
        trace, manifest = MODULE["build_hotspot"](base, self.positions, "g3-test", workload_candidate="C800-109G")
        self.assertEqual(sum(t["input_bytes"] for t in trace["tasks"]), 109_000_000_000)
        self.assertEqual(manifest["workload_candidate"], "C800-109G")
        for before, after in zip(base["tasks"], trace["tasks"]):
            self.assertEqual({k: v for k, v in before.items() if not k.endswith("node_id")},
                             {k: v for k, v in after.items() if not k.endswith("node_id")})


if __name__ == "__main__":
    unittest.main()
