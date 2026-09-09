"""Frozen scene integrity, deterministic generator and final-only runner contracts."""
from collections import Counter
import contextlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[2]
ROOT = MODULE.parents[1]
GENERATION = MODULE / "tools/generation"
sys.path.insert(0, str(GENERATION))
GEN = runpy.run_path(str(GENERATION / "generate-task-workload.py"))
RUN = runpy.run_path(str(MODULE / "tests/integration/regression/run-final-scenario.py"))
SCENE = MODULE / "input/examples/leo-66-1300s-n4c-g3-truncnormal-v3"


class FinalScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tasks = json.loads((SCENE / "task-trace.json").read_text())["tasks"]
        cls.profile = json.loads((SCENE / "compute-profile.json").read_text())["compute_nodes"]
        cls.attributes = GEN["attributes"]()

    def test_frozen_counts_exact_budgets_and_window(self):
        self.assertEqual([t["task_id"] for t in self.tasks], list(range(1, 801)))
        self.assertEqual(Counter(t["task_profile"] for t in self.tasks),
                         {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80})
        self.assertEqual(tuple(sum(t[k] for t in self.tasks) for k in
                              ("input_bytes", "output_bytes", "compute_work_units")),
                         (193526895311, 99846517485, 351623833))
        self.assertTrue(all(10**9 <= t["arrival_time_ns"] <= 1050*10**9 for t in self.tasks))
        self.assertEqual(len(self.profile), 66)
        self.assertEqual({p["node_id"] for p in self.profile}, set(range(66)))
        self.assertEqual({p["compute_rate_work_units_per_second"] for p in self.profile}, {100000})
        manifest = json.loads((SCENE / "f3-manifest.json").read_text())
        self.assertEqual((manifest["simulation_duration_s"], manifest["f3"]["node_id"],
                          manifest["f3"]["time_ns"]), (1300, 62, 1027055770726))
        placement = json.loads((SCENE / "placement-manifest.json").read_text())
        self.assertEqual((placement["hotspot_weight"], placement["regional_candidate_limit"]), (64, 1))
        self.assertEqual(placement["arrival_window_s"], [1, 1050])

    def test_attributes_arrivals_and_anchor_identity_match_frozen(self):
        self.assertEqual(self.attributes, GEN["attributes"]())
        arrivals = GEN["arrivals"](range(1, 801), GEN["WORKLOAD_SEED"])
        for actual, attr in zip(self.tasks, self.attributes):
            budget = GEN["budget_for"](attr)
            self.assertEqual((actual["input_bytes"], actual["output_bytes"], actual["compute_work_units"]),
                             (budget.input_bytes, budget.output_bytes, budget.compute_work_units))
            self.assertEqual(actual["task_profile"], attr["task_profile"])
            self.assertEqual(actual["arrival_time_ns"], arrivals[actual["task_id"]])
        anchors = [a for a in self.attributes if a.get("fixed_tail_anchor")]
        ordinary = [a for a in self.attributes if a["task_profile"] != "llm" and not a.get("fixed_tail_anchor")]
        self.assertEqual(len(ordinary), 705)
        self.assertTrue(all(50_000_000 <= a["input_bytes"] < 1_000_000_000 for a in ordinary))
        self.assertEqual(Counter(a["input_bytes"] for a in anchors), {500_000_000: 10, 1_000_000_000: 5})
        expected = json.loads((SCENE / "workload-summary.json").read_text())["truncated_normal"]["fixed_tail_task_ids"]
        self.assertEqual({str(s): [a["task_id"] for a in anchors if a["input_bytes"] == s]
                          for s in (500_000_000, 1_000_000_000)}, expected)

    def test_synthetic_placement_deterministic_and_missing_slices_rejected(self):
        positions = {t*10**9: {n: (40., (-95., 15., 120., 70.)[n % 4]) for n in range(66)}
                     for t in range(1051)}
        args = (list(range(66)), self.profile, positions)
        first = GEN["build_final_workload"](*args)
        self.assertEqual(first, GEN["build_final_workload"](*args))
        for t in first[0]["tasks"]:
            self.assertNotEqual(t["source_node_id"], t["compute_node_id"])
            self.assertNotEqual(t["result_node_id"], t["compute_node_id"])
        with self.assertRaisesRegex(ValueError, "slice"):
            GEN["build_final_workload"](list(range(66)), self.profile, {0: positions[0]})
        with self.assertRaises(ValueError):
            GEN["build_final_workload"](list(range(65)), self.profile, positions)

    def test_native_generator_twice_byte_identical_to_frozen(self):
        supplied = os.environ.get("SATCOMPUTE_POSITION_SLICES")
        if not supplied:
            self.skipTest("set SATCOMPUTE_POSITION_SLICES to existing native 0..1050s 1-second slices")
        slices = Path(supplied).resolve()
        self.assertTrue((slices / "nodes_0s.json").is_file())
        with tempfile.TemporaryDirectory(prefix="satcompute-final-generator-") as temporary:
            results = []
            for index in (1, 2):
                trace, summary = (Path(temporary) / f"{name}-{index}.json" for name in ("trace", "summary"))
                subprocess.run([sys.executable, str(GENERATION / "generate-task-workload.py"),
                    f"--nodes-file={slices / 'nodes_0s.json'}", f"--position-slices={slices}",
                    f"--compute-profile={SCENE / 'compute-profile.json'}", f"--output-task-trace={trace}",
                    f"--output-workload-summary={summary}"], check=True, capture_output=True)
                results.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(results[0], results[1])
            self.assertEqual(results[0][0], (SCENE / "task-trace.json").read_bytes())
            current = json.loads(results[0][1])
            frozen = json.loads((SCENE / "placement-manifest.json").read_text())
            self.assertEqual(current["placement"]["placements"], frozen["placements"])

    def test_generator_rejects_existing_output_and_obsolete_cli(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "trace.json"
            out.write_text("preserve me")
            args = ["generator", "--nodes-file=unused", "--position-slices=unused", "--compute-profile=unused",
                    f"--output-task-trace={out}", f"--output-workload-summary={out.parent / 'summary.json'}"]
            with patch("sys.argv", args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    GEN["main"]()
            self.assertEqual(out.read_text(), "preserve me")
            for flag in ("--profile=n4c-final", "--seed=old", "--workload-candidate=old"):
                with patch("sys.argv", [*args, flag]), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        GEN["main"]()


class FinalRunnerTests(unittest.TestCase):
    def test_fixed_defaults_and_modes_without_running_simulation(self):
        defaults = RUN["arguments"](Path("unused"))
        for flag in ("--simulationDuration=1300", "--fixedDelay=0.001", "--randomSeed=1", "--randomRun=11",
                     "--faultF3Node=62", "--faultF3Time=1027.055770726", "--faultProbabilityAudit=0",
                     "--compfrr-shadow=0", "--taskCompletionPolicy=report"):
            self.assertIn(flag, defaults)
        self.assertIn("--compfrr-shadow=1", RUN["arguments"](Path("unused"), "generate", True, True))
        none = RUN["arguments"](Path("unused"), "none")
        self.assertIn("--taskCompletionPolicy=strict", none)
        self.assertFalse(any(v.startswith("--faultTrace=") for v in none))
        for mode, audit, shadow in (("none", True, False), ("none", False, True), ("replay", False, False)):
            with self.assertRaises(ValueError):
                RUN["arguments"](Path("unused"), mode, audit, shadow)

    def test_runner_records_command_and_refuses_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "run"
            with patch("sys.argv", ["runner", "--output-dir", str(out)]), \
                    patch("subprocess.check_output", return_value="test-commit"), \
                    patch("subprocess.run") as launch, contextlib.redirect_stdout(io.StringIO()):
                launch.return_value.returncode = 0
                self.assertEqual(RUN["main"](), 0)
                launch.assert_called_once()
            self.assertEqual(json.loads((out / "execution.json").read_text())["simulation_duration_s"], 1300)
            with patch("sys.argv", ["runner", "--output-dir", str(out)]), \
                    patch("subprocess.run") as launch, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    RUN["main"]()
                launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
