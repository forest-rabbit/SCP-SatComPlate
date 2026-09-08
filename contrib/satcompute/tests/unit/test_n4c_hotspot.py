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
        # All northern: controlled worker is naturally outside SAA before its first task.
        cls.positions = {t * 10**9: {n: (40., (-95., 15., 120., 70.)[n % 4])
                                    for n in range(66)} for t in range(602)}

    def build(self, **kwargs):
        return MODULE["build_hotspot"](self.base, self.positions, "g3-test", **kwargs)

    def test_repeat_conservation_and_f3(self):
        trace, manifest = self.build()
        self.assertEqual(json.dumps((trace, manifest)), json.dumps(self.build()))
        base = {t["task_id"]: t for t in self.base["tasks"]}
        f3 = manifest["f3"]
        for task in trace["tasks"]:
            self.assertEqual({k: task[k] for k in (*MODULE["BUSINESS"], "arrival_time_ns")},
                             {k: base[task["task_id"]][k] for k in (*MODULE["BUSINESS"], "arrival_time_ns")})
            self.assertNotEqual(task["compute_node_id"], task["source_node_id"])
            self.assertNotEqual(task["compute_node_id"], task["result_node_id"])
            if task["task_id"] != f3["victim_task_id"]:
                self.assertNotIn(f3["node_id"], [task[k] for k in (
                    "source_node_id", "compute_node_id", "result_node_id")])
        self.assertLess(manifest["maximum_position_age_ns"], 10**9)

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


if __name__ == "__main__":
    unittest.main()
