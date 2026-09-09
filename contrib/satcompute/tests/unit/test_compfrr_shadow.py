"""Cross-check the C++ shadow mapping against the frozen G1 Python implementation."""
import bisect
import csv
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
GEN = ROOT / "contrib/satcompute/tools/generation"
sys.path.insert(0, str(GEN))
from task_workload_model import image_budget, llm_budget, legal_unit_ends
SCENE = ROOT / "contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3/task-trace.json"


class ShadowLayoutTest(unittest.TestCase):
    def test_all_800_frozen_task_budgets_and_legal_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="satcompute-g4-layout-") as tmp:
            path = Path(tmp) / "layouts.json"
            subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join([
                "satcompute-compfrr-shadow-model-test", f"--layoutInput={SCENE}",
                f"--layoutOutput={path}"])], cwd=ROOT, check=True, capture_output=True, text=True)
            actual = {row["task_id"]: row for row in json.loads(path.read_text())}
            tasks = json.loads(SCENE.read_text())["tasks"]
            self.assertEqual(len(actual), len(tasks))
            for task in tasks:
                with self.subTest(task=task["task_id"]):
                    if task["task_profile"] == "llm":
                        tokens = task["compute_work_units"] // 100
                        budget = llm_budget(task["input_bytes"], tokens - 1, 1)
                    else:
                        budget = image_budget(task["task_profile"], task["input_bytes"], str(task["task_id"]))
                    ends, _ = legal_unit_ends(budget)
                    ends_by_work = {budget.work_at_extent(end): end for end in ends}
                    works = [0, *sorted(ends_by_work)]
                    record = actual[task["task_id"]]
                    self.assertEqual(record["K"], budget.k_variable_bytes)
                    self.assertEqual(record["boundaries"], works)
                    self.assertEqual(record["state_bytes"], [budget.state_at_work(w) for w in works])
                    legal_ends = [ends_by_work[w] for w in works[1:]]
                    targets = [budget.work_at_extent(legal_ends[bisect.bisect_left(
                        legal_ends, (budget.extent * p + 999) // 1000)]) for p in (50, 100, 200)]
                    self.assertEqual(record["first_targets"], targets)


CHECK = runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/validation/compfrr-shadow/summarize.py"))


class ShadowEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.left, self.right = [Path(self.temporary.name) / s for s in ("left", "right")]
        for path in (self.left, self.right):
            path.mkdir()
            for name in CHECK["REQUIRED"]:
                (path / name).write_text('{"outcome": 1}' if name.endswith(".json") else "id,state\n1,FAILED\n")

    def test_only_wall_clock_is_exempt(self):
        for path, wall in ((self.left, 1), (self.right, 2)):
            (path / "run-summary.json").write_text(json.dumps({"outcome": 1, "wall_clock_ns": wall, "wall_clock_s": wall / 1e9}))
        self.assertTrue(CHECK["compare_business"](self.left, self.right)["pass"])
        (self.right / "run-summary.json").write_text('{"outcome": 2}')
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])

    def test_csv_mutation_missing_file_and_audit_presence_fail(self):
        self.assertTrue(CHECK["compare_business"](self.left, self.right)["pass"])
        victim = self.right / "fault-events.csv"
        victim.write_text("id,state\n1,COMPLETED\n")
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])
        victim.unlink()
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])
        victim.write_bytes((self.left / victim.name).read_bytes())
        (self.right / "fault-model-probabilities.csv").write_text("id,p\n1,0.5\n")
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])

    def test_distribution_uses_linear_quantiles_not_success_accuracy(self):
        result = CHECK["distribution"]([1, 2, 3, 4])
        self.assertEqual(result["P50"], 2.5)
        self.assertAlmostEqual(result["P90"], 3.7)
        self.assertEqual(CHECK["distribution"]([]), {"count": 0})

    def test_start_workload_and_full_k_tiers(self):
        tasks = [dict(task_id=str(i), task_profile="dense-image", input_bytes="1000",
                      K_variable=str(k), ever_start="true", init_complete="true", normal_waste_wu="10")
                 for i, k in enumerate((0, 100000000, 100000001, 500000000, 500000001))]
        decisions = [dict(task_id=t["task_id"], start_triggered="true", remaining_compute_s=str(i+1))
                     for i, t in enumerate(tasks)]
        result = CHECK["start_workload_and_tiers"](tasks, decisions)
        self.assertEqual([t["task_count"] for t in result["cost_tiers"]], [2, 2, 1])
        self.assertEqual(sum(t["normal_waste_wu"] for t in result["cost_tiers"]), 50)
        self.assertEqual(result["start_workload"]["remaining_compute_s"]["P50"], 3)
        with self.assertRaises(ValueError):
            CHECK["start_workload_and_tiers"](tasks, decisions[:-1])

    def test_virtual_byte_serialization_and_normal_cost_ledger(self):
        def write(name, records):
            keys = sorted({key for row in records for key in row}) or ["task_id"]
            with (self.left / name).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=keys)
                writer.writeheader()
                writer.writerows(records)
        task = dict(task_id="1", task_profile="dense-image", initial_legal_work=0,
                    K_variable=100, W=1000, cL_s=.0001, cR_s=.0005, N_L=1, N_R=1,
                    init_complete="true", normal_cost_s=.0012, normal_waste_wu=120,
                    compute_rate_wu_per_s=100000)
        events = [dict(task_id=1, event="ON", time_ns=0, local_work=0, remote_work=0),
                  dict(task_id=1, event="L1_TRIGGER", time_ns=100000, target_work=100),
                  dict(task_id=1, event="L1_DONE", time_ns=200001, trigger_time_ns=100000,
                       local_work=100, previous_local_work=0, delta_state_bytes_actual=10, delta_progress_actual=.1),
                  dict(task_id=1, event="REMOTE_BATCH", time_ns=200001, n_at_creation=1,
                       batch_id=1, batch_bytes=10, batch_end_work=100, start_time_ns=200001, done_time_ns=700009),
                  dict(task_id=1, event="REMOTE_DONE", time_ns=700009, batch_id=1, remote_work=100),
                  dict(task_id=1, event="REAL_COMPUTE_COMPLETE", time_ns=1000000)]
        write("shadow-task-summary.csv", [task])
        write("shadow-events.csv", events)
        write("shadow-decisions.csv", [])
        self.assertTrue(CHECK["audit_virtual"](self.left)["pass"])
        for index, field, bad in ((2, "delta_state_bytes_actual", 11), (3, "start_time_ns", 200002),
                                  (4, "remote_work", 101)):
            mutated = [dict(e) for e in events]
            mutated[index][field] = bad
            write("shadow-events.csv", mutated)
            with self.assertRaises(ValueError):
                CHECK["audit_virtual"](self.left)
        write("shadow-events.csv", events)
        write("shadow-task-summary.csv", [{**task, "normal_cost_s": .0018}])
        with self.assertRaises(ValueError):
            CHECK["audit_virtual"](self.left)


if __name__ == "__main__":
    unittest.main()
