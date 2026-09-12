"""CB pooled exposure accounting and frozen runner isolation."""
from copy import deepcopy
from pathlib import Path
import runpy
import sys
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2]
TOOLS = MODULE / "protection/policy/baseline/checkbullet/tools"
sys.path.insert(0, str(TOOLS))
CAL = runpy.run_path(str(TOOLS / "calibrate-cb-sat-mtbf.py"))
MATRIX = runpy.run_path(str(TOOLS / "analyze-cb-sat-matrix.py"))
from cb_tools import SCENE_HELPER, GROUPS, flags, replace_flag, scene_identity


class CbCalibrationTests(unittest.TestCase):
    @staticmethod
    def data():
        probabilities, states = [], []
        for time, node, task in ((1,0,1),(2,0,1),(2,1,2)):
            probabilities.append(dict(simulation_time_ns=str(time*10**9), node_id=str(node),
                task_id=str(task), remaining_compute_time_ns="1000000000",
                f1_step_failure_probability="0.2", f2_step_failure_probability="0.1",
                combined_step_failure_probability="0.28", failure_before_finish_probability="0.99"))
            states.append(dict(simulation_time_ns=str(time*10**9), node_id=str(node),
                               sampling_eligible="1", p_compute="0.28"))
        return probabilities, states

    @staticmethod
    def fault(time=2, node=0, both=False):
        return dict(start_time_ns=time*10**9, node_id=node, fault_id=node+1, fault_occurred=True,
                    fault_type="compute", f1_occurred=True, f2_occurred=both)

    def estimate(self, faults=()):
        return CAL["estimate"](*self.data(), list(faults), 10**9)

    def test_union_one_event_and_heterogeneous_node_exposure(self):
        r = self.estimate([self.fault(both=True)])
        self.assertEqual((r["joint_failure_count"], r["eligible_exposure_seconds"], r["mtbf_seconds"]), (1,3,3))
        self.assertEqual(r["nodes"][0]["eligible_checks"], 2)
        self.assertEqual(r["nodes"][1]["eligible_checks"], 1)
        self.assertAlmostEqual(r["nodes"][0]["q_sum"], .56)  # Not .99 P_finish.

    def test_zero_events_explicit_infinite_not_arbitrary_constant(self):
        self.assertIsNone(self.estimate()["mtbf_seconds"])

    def test_idle_events_do_not_count(self):
        r = self.estimate([self.fault(node=8)])
        self.assertEqual((r["joint_failure_count"], r["outside_scope_fault_count"]), (0,1))

    def test_identical_duplicate_audits_and_union_events_deduplicated(self):
        p,s = self.data(); f = self.fault(both=True)
        r = CAL["estimate"](p+p, s+s, [f,f], 10**9)
        self.assertEqual((r["eligible_check_count"], r["joint_failure_count"], r["duplicate_probability_rows"]), (3,1,3))

    def test_conflicting_duplicate_and_ineligible_state_rejected(self):
        p,s = self.data(); bad = deepcopy(p[0]); bad["task_id"] = "99"
        with self.assertRaisesRegex(ValueError, "conflicting"):
            CAL["estimate"](p+[bad], s, [], 10**9)
        s[0]["sampling_eligible"] = "0"
        with self.assertRaisesRegex(ValueError, "eligible"):
            CAL["estimate"](p, s, [], 10**9)

    def test_completed_task_check_not_at_risk(self):
        p,s = self.data(); p[0]["remaining_compute_time_ns"] = "0"
        r = CAL["estimate"](p,s,[self.fault(time=1)],10**9)
        self.assertEqual((r["eligible_check_count"], r["joint_failure_count"]), (2,0))

    def test_f3_and_invalid_probability_rejected(self):
        f = self.fault(); f["fault_type"] = "satellite"
        with self.assertRaisesRegex(ValueError, "F3"):
            self.estimate([f])
        p,s = self.data(); p[0]["combined_step_failure_probability"] = ".99"
        with self.assertRaisesRegex(ValueError, "q mismatch"):
            CAL["estimate"](p,s,[],10**9)

    def test_mode_only_registration_and_pilot_overrides(self):
        directory = Path(tempfile.gettempdir()) / "cb-command-unit-only"
        old = SCENE_HELPER["arguments"](directory, protection_mode="fixed")
        cb = SCENE_HELPER["arguments"](directory, protection_mode="checkbullet")
        self.assertEqual(replace_flag(old,"protectionMode","checkbullet"),
                         replace_flag(cb,"protectionMode","checkbullet"))
        pilot = replace_flag(replace_flag(SCENE_HELPER["arguments"](directory,audit=True),
                                         "randomRun",101),"faultEnableF3",0)
        self.assertEqual(flags(pilot)["randomRun"], "101")
        self.assertEqual(flags(pilot)["faultEnableF3"], "0")
        self.assertEqual(scene_identity()["work_units"], 352513119)

    def test_formal_matrix_missing_duplicate_and_mixed_snapshot_rejected(self):
        runs = [dict(group=f"CB-{p}-{b}",commit="execution",mtbf_seconds=41) for p,b in GROUPS]
        self.assertEqual(MATRIX["coverage"](runs)["status"], "PASS")
        for bad in (runs[:-1], runs+[runs[0]], []):
            self.assertEqual(MATRIX["coverage"](bad)["status"], "INCOMPLETE")
        bad = deepcopy(runs); bad[0]["commit"] = "changed"
        self.assertEqual(MATRIX["coverage"](bad)["status"], "INCOMPLETE")
        bad = deepcopy(runs); bad[0]["mtbf_seconds"] = 1
        self.assertEqual(MATRIX["coverage"](bad)["status"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
