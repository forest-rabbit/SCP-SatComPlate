"""Cross-check the C++ shadow mapping against the frozen G1 Python implementation."""
import bisect
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
from task_workload_model import image_budget, llm_budget
PREVIEW = runpy.run_path(str(GEN / "preview-n4c-workload.py"))
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
                    ends, _ = PREVIEW["preview_unit_ends"](budget)
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


if __name__ == "__main__":
    unittest.main()
