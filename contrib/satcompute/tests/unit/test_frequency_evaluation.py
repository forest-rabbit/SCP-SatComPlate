"""G3 descriptive accounting: exact hand anchors, not algorithm efficacy assertions."""
from pathlib import Path
import runpy
import unittest
from copy import deepcopy

ROOT = Path(__file__).resolve().parents[4]
REGRESSION = ROOT / "contrib/satcompute/tests/integration/regression"
EVAL = runpy.run_path(str(REGRESSION / "analyze-frequency-evaluation.py"))
RUN = runpy.run_path(str(REGRESSION / "run-final-scenario.py"))


class FrequencyEvaluationTests(unittest.TestCase):
    def test_pair_counts_and_same_time_retry_guards(self):
        row = dict(pair_candidates_total="6", pair_node_feasible="4", pair_skip_node="2",
                   pair_path_feasible="3", pair_skip_no_route="0", pair_skip_no_capacity="1",
                   pair_skip_other="0", pair_hard_checked="2", pair_hard_feasible="1",
                   pair_skip_storage="1", pair_skip_deadline="0", task_id="1",
                   fault_epoch_time_ns="5000000", phase_before="OFF", actual_fault_sampled="0",
                   actual_fault_hit="0", capacity_retry_success="1", decision_committed="1",
                   proposed_action="START", decision_trigger="CAPACITY_RELEASE")
        check = EVAL["verify_pair_retries"]
        check([row], [])
        with self.assertRaises(ValueError):
            check([row, row], [])
        for key, value in (("pair_path_feasible", "4"), ("actual_fault_sampled", "1"),
                           ("phase_before", "ON"), ("pair_hard_checked", "3")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                check([{**row, key: value}], [])

    def test_on_capacity_resume_without_off_pair_counts(self):
        row = dict(task_id="1", fault_epoch_time_ns="5000000", phase_before="ON",
                   actual_fault_sampled="0", actual_fault_hit="0", capacity_retry_success="1",
                   decision_committed="1", proposed_action="UPDATE", decision_trigger="CAPACITY_RELEASE")
        check = EVAL["verify_pair_retries"]
        check([row], [])
        check([{**row, "proposed_action": "PAUSE", "capacity_retry_success": "0"}], [])
        for changed in ([row, row], [{**row, "actual_fault_sampled": "1"}],
                        [{**row, "proposed_action": "START"}],
                        [{**row, "capacity_wait_start_ns": "4000000"}]):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                check(changed, [])

    def test_bc_pair_identity_requires_same_scene_and_clean_same_code(self):
        runs = []
        for placement in ("ffp", "lrl"):
            runs.append({"execution": dict(protection_mode="compfrr", placement_mode=placement,
                fault_mode="generate", lrl_recovery_weight=1, worktree_dirty=False, audit=False,
                shadow=False, command=["satcompute --seed=1"], commit="same", simulation_duration_s=1300),
                "execution_result": {"returncode": 0}, "summary": {"tasks": 801}})
        self.assertEqual(EVAL["fairness"](runs)["groups"], "B/C")
        changed = deepcopy(runs)
        changed[1]["summary"]["tasks"] = 800
        with self.assertRaises(ValueError):
            EVAL["fairness"](changed)
        changed[0]["summary"]["tasks"] = 800
        self.assertEqual(EVAL["fairness"](changed)["task_count"], 800)

    def test_percentiles_include_zeros(self):
        s = EVAL["stats"]([0, 0, 10, 30])
        self.assertEqual((s["p10"], s["p50"], s["p90"]), (0, 5, 24.000000000000004))
        self.assertEqual(s["sum"], 40)
        self.assertIsNone(EVAL["stats"]([])["p50"])

    def test_weight_uses_physical_on_and_excludes_pause(self):
        # START at 0, ON at 2; UPDATE at 10, STOP at 20; pause [5,12].
        w, d, n = EVAL["active_weight"]([(0, .05, 4), (10, .1, 8)], 2, 20, [(5, 12)])
        self.assertEqual(w, 11)
        self.assertAlmostEqual(d, .95)
        self.assertEqual(n, 76)

    def test_weight_ignores_initialization_failure(self):
        self.assertEqual(EVAL["active_weight"]([(0, .05, 4)], 20, 10, []), (0, 0, 0))

    def test_runner_freezes_weight_and_formal_modes(self):
        for kwargs in ({"lrl_weight": 2}, {"placement_mode": "lrl"},
                       {"protection_mode": "compfrr", "fault_mode": "none"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                RUN["arguments"](Path("output/not-run"), **kwargs)
        a = RUN["arguments"](Path("output/not-run"), protection_mode="compfrr", placement_mode="lrl")
        self.assertIn("--lrlRecoveryWeight=1", a)
        self.assertIn("--simulationDuration=1300", a)
        self.assertIn("--faultMode=generate", a)
        self.assertIn("--remoteBusyRecoveryPolicy=relocate", a)
        fixed = RUN["arguments"](Path("output/not-run"), protection_mode="fixed",
                                 placement_mode="lrl", remote_busy_recovery_policy="recompute")
        self.assertIn("--placementMode=lrl", fixed)
        self.assertIn("--remoteBusyRecoveryPolicy=recompute", fixed)
        with self.assertRaises(ValueError):
            RUN["arguments"](Path("output/not-run"), remote_busy_recovery_policy="other")


if __name__ == "__main__":
    unittest.main()
