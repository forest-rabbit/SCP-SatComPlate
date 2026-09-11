"""Frozen scene integrity, deterministic generator and final-only runner contracts."""
from collections import Counter
from copy import deepcopy
import csv
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
F3_CHECK = runpy.run_path(str(MODULE / "tests/integration/regression/run-f3-protection-check.py"))
SCENE = MODULE / "input/experiments/leo-66"


class FinalScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tasks = json.loads((SCENE / "workload/task-trace.json").read_text())["tasks"]
        cls.profile = json.loads((SCENE / "compute/compute-profile.json").read_text())["compute_nodes"]
        cls.attributes = GEN["attributes"]()

    def test_frozen_counts_exact_budgets_and_window(self):
        self.assertEqual([t["task_id"] for t in self.tasks], list(range(1, 801)))
        self.assertEqual(Counter(t["task_profile"] for t in self.tasks),
                         {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80})
        self.assertEqual(tuple(sum(t[k] for t in self.tasks) for k in
                              ("input_bytes", "output_bytes", "compute_work_units")),
                         (194119753287, 100168131855, 352513119))
        self.assertTrue(all(10**9 <= t["arrival_time_ns"] <= 1050*10**9 for t in self.tasks))
        self.assertEqual(len(self.profile), 66)
        self.assertEqual({p["node_id"] for p in self.profile}, set(range(66)))
        self.assertEqual({p["compute_rate_work_units_per_second"] for p in self.profile}, {100000})
        with (SCENE / "topology/constellation.csv").open() as stream:
            shells = list(csv.DictReader(line for line in stream if line.strip() and not line.startswith("#")))
        self.assertEqual(len(shells), 1)
        self.assertEqual(tuple(float(shells[0][key]) for key in
                         ("altitudeKm", "inclinationDegrees", "numberOfPlanes",
                          "numberOfSatellitesPerPlane", "phasingFactor", "raanSpanDeg")),
                         (780, 86.4, 6, 11, 1, 180))
        manifest = json.loads((SCENE / "fault/f3-manifest.json").read_text())
        self.assertEqual((manifest["simulation_duration_s"], manifest["f3"]["node_id"],
                          manifest["f3"]["time_ns"]), (1300, 62, 1027055770726))
        placement = json.loads((SCENE / "placement/placement-manifest.json").read_text())
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
        ordinary = [a for a in self.attributes if a["task_profile"] != "llm"
                    and not a.get("fixed_tail_anchor") and not a.get("controlled_f3_size")]
        self.assertEqual(len(ordinary), 704)
        self.assertTrue(all(50_000_000 <= a["input_bytes"] < 1_000_000_000 for a in ordinary))
        self.assertEqual(Counter(a["input_bytes"] for a in anchors), {500_000_000: 10, 1_000_000_000: 5})
        expected = json.loads((SCENE / "workload/workload-summary.json").read_text())["truncated_normal"]["fixed_tail_task_ids"]
        self.assertEqual({str(s): [a["task_id"] for a in anchors if a["input_bytes"] == s]
                          for s in (500_000_000, 1_000_000_000)}, expected)

    def test_controlled_task_size_only_and_no_predecessor(self):
        target = self.tasks[119]
        self.assertEqual(target, dict(task_id=120, task_profile="compression", input_bytes=800_000_000,
            output_bytes=433_985_046, compute_work_units=1_200_000, source_node_id=54,
            compute_node_id=62, result_node_id=33, arrival_time_ns=1024682825747))
        self.assertEqual([t["task_id"] for t in self.tasks if t["compute_node_id"] == 62], [120])
        overridden = [a for a in self.attributes if a.get("controlled_f3_size")]
        self.assertEqual(len(overridden), 1)
        self.assertEqual(overridden[0]["original_input_bytes"], 207142024)
        summary = json.loads((SCENE / "workload/workload-summary.json").read_text())
        self.assertNotIn("warmup", summary)
        self.assertEqual(summary["ordinary_image_count"], 704)
        placement = json.loads((SCENE / "placement/placement-manifest.json").read_text())
        self.assertEqual(len(placement["placements"]), 800)
        for key, total in (("task_count", 800), ("input_bytes", 194119753287), ("work_units", 352513119)):
            self.assertEqual(sum(r[key] for r in placement["by_region"].values()), total)

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
                    f"--compute-profile={SCENE / 'compute/compute-profile.json'}", f"--output-task-trace={trace}",
                    f"--output-workload-summary={summary}"], check=True, capture_output=True)
                results.append((trace.read_bytes(), summary.read_bytes()))
            self.assertEqual(results[0], results[1])
            self.assertEqual(results[0][0], (SCENE / "workload/task-trace.json").read_bytes())
            current = json.loads(results[0][1])
            frozen = json.loads((SCENE / "placement/placement-manifest.json").read_text())
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


class F3SelectionTests(unittest.TestCase):
    def test_completion_alone_or_start_without_checkpoint_never_passes(self):
        tables = {
            "task-summary.csv": [dict(task_id="120", input_bytes="800000000", compute_work_units="1200000",
                compute_start_time_ns="1025000000000", final_state="COMPLETED", compute_deadline_met="1")],
            "recovery-summary.csv": [dict(task_id="120", fault_type="satellite", fault_time_ns=str(F3_CHECK["F3_NS"]),
                phase_at_fault="ON", chosen_path="REMOTE_REDO", checkpoint_state_exists="1", remote_work_units="100")],
            "frequency-decisions.csv": [dict(task_id="120", proposed_action="START", decision_committed="1",
                j_off="0.03", j_start="0.02")],
            "protection-events.csv": [dict(task_id="120", event="INIT_COST_COMMITTED", attempt_generation="0",
                time_ns="1026600000000")]}
        assess = F3_CHECK["assess"]
        cases = [(None, None, None), ("recovery-summary.csv", "phase_at_fault", "INITIALIZING"),
                 ("recovery-summary.csv", "chosen_path", "RECOMPUTE"),
                 ("recovery-summary.csv", "remote_work_units", "0"),
                 ("frequency-decisions.csv", "j_start", "0.03"),
                 ("protection-events.csv", "time_ns", str(F3_CHECK["F3_NS"])),
                 ("task-summary.csv", "compute_deadline_met", "0")]
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            fault_file = directory / "fault-trace.json"
            fault_file.write_text('{"faults": []}')
            for name, key, value in cases:
                data = deepcopy(tables)
                if name:
                    data[name][0][key] = value
                with self.subTest(name=name, key=key), patch.dict(assess.__globals__, rows=lambda d, n: data[n]):
                    self.assertEqual(assess(directory)["eligible"], name is None)
            fault_file.write_text(json.dumps({"faults": [dict(node_id=62, fault_type="compute",
                fault_occurred=True, start_time_ns=1026000000000)]}))
            with patch.dict(assess.__globals__, rows=lambda d, n: tables[n]):
                self.assertFalse(assess(directory)["eligible"])


class FinalRunnerTests(unittest.TestCase):
    def test_deferred_is_explicit_compfrr_only(self):
        output = Path("output/controlled-test")
        eager = RUN["arguments"](output, protection_mode="compfrr")
        deferred = RUN["arguments"](output, protection_mode="compfrr", input_staging_policy="deferred")
        self.assertEqual(deferred, eager + ["--inputStagingPolicy=deferred"])
        for mode in ("off", "recompute", "one-plus-one", "fixed"):
            with self.assertRaisesRegex(ValueError, "CompFRR"):
                RUN["arguments"](output, protection_mode=mode, input_staging_policy="deferred")

    def test_complete_baselines_use_frozen_scene_and_ffp_only(self):
        for mode in ("recompute", "one-plus-one"):
            command = RUN["arguments"](Path("unused"), protection_mode=mode)
            self.assertIn(f"--protectionMode={mode}", command)
            self.assertIn("--faultMode=generate", command)
            self.assertIn("--placementMode=ffp", command)
            with self.assertRaises(ValueError):
                RUN["arguments"](Path("unused"), protection_mode=mode, placement_mode="lrl")

    def test_fixed_defaults_and_modes_without_running_simulation(self):
        defaults = RUN["arguments"](Path("unused"))
        for flag in ("--simulationDuration=1300", "--fixedDelay=0.001", "--randomSeed=1", "--randomRun=11",
                     "--islBandwidthBps=10000000000", "--computeDeadlineFactor=1.3",
                     f"--constellationConfig={RUN['SCENE']}/topology/constellation.csv",
                     f"--computeProfile={RUN['SCENE']}/compute/compute-profile.json",
                     f"--taskTrace={RUN['SCENE']}/workload/task-trace.json",
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
