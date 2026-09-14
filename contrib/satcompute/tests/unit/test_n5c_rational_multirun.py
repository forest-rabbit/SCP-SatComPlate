"""Bounded matrix and symmetric tail sensitivity; no new simulation parameters."""
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1] / "integration/regression"
RUN = runpy.run_path(str(HERE / "run-n5c-rational-multirun.py"))
AUDIT = runpy.run_path(str(HERE / "analyze-n5c-rational-multirun.py"))


class RationalMultirunTests(unittest.TestCase):
    def test_only_four_new_runs_and_frozen_flags(self):
        plan = dict(runs=[11, 12, 13, 14, 15], new_runs=[12, 13, 14, 15],
                    variant="rational-U", new_executions=4, reused_executions=11)
        RUN["validate_plan"](plan)
        for field, bad in (("new_runs", [11, 12, 13, 14, 15]), ("runs", [11]),
                           ("variant", "recent-U"), ("new_executions", 5), ("reused_executions", 10)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                RUN["validate_plan"](dict(plan, **{field: bad}))
        root = Path("unused").resolve()
        flags = RUN["OLD"]["flags"]
        for r in RUN["RUNS"]:
            observed = flags(RUN["RAT"]["arguments"](root, r))
            original = flags(RUN["OLD"]["arguments"](root, "full", r))
            self.assertEqual(dict(observed, n5cVariant="full"), original)
        for invalid in (10, 16, True, 12.0):
            with self.assertRaises(ValueError):
                RUN["RAT"]["arguments"](root, invalid)

    def test_scope_rejects_runtime_or_inputs(self):
        for path in ("contrib/satcompute/para.cc", "contrib/satcompute/protection/runtime/foo.cc",
                     "contrib/satcompute/input/foo.csv"):
            with patch.dict(RUN["OLD"], git=lambda *args: path), self.assertRaises(ValueError):
                RUN["source_scope"]("candidate")
        def only_tests(*args):
            return "contrib/satcompute/tests/unit/test.py\nAGENTS.md" if len(args) == 4 else ""
        with patch.dict(RUN["OLD"], git=only_tests):
            self.assertTrue(RUN["source_scope"]("candidate")["production_and_inputs_identical"])

    def test_byte_dedup_preserves_partial_and_rejects_conflict(self):
        f = dict(task_id="1", kind="RECOVERY_INPUT", sent_bytes="10", transfer_id="5")
        self.assertEqual(AUDIT["task_network"]([f, f]), {"1": 10})
        with self.assertRaises(ValueError):
            AUDIT["task_network"]([f, dict(f, sent_bytes="11")])

    def test_guard_names_actual_recovery_sources(self):
        calls = []
        def unchanged(*args):
            calls.append(args)
            return ""
        with patch.dict(RUN["OLD"], git=unchanged):
            RUN["source_scope"]("candidate")
        protected = calls[1][calls[1].index("--")+1:]
        self.assertIn("contrib/satcompute/protection/runtime/recovery-controller.cc", protected)
        self.assertIn("contrib/satcompute/protection/baseline/recompute/recompute-runtime.cc", protected)
        self.assertIn("contrib/satcompute/protection/runtime/transfer-only-recovery-ledger.cc", protected)
        self.assertTrue(all((RUN["ROOT"] / p).exists() for p in protected))

    def test_three_way_uses_one_common_observed_population(self):
        def r(task, catch):
            return dict(task_id=task, fault_time_ns="1", fault_type="compute", actual_T_catch_ns=catch)
        values = {g: [r("11:1", "1000000000"), r("11:2", "0")] for g in AUDIT["GROUPS"]}
        values["rational-U"][0]["actual_T_catch_ns"] = ""
        result = AUDIT["three_way_catch"](values)
        self.assertEqual((result["common_faults"], result["paired_catch_count"]), (2, 1))
        self.assertEqual([v["mean"] for v in result["groups"].values()], [0, 0, 0])

    def test_numeric_ties_and_harm_not_hidden(self):
        values = {"12:1": 4, "11:10": 4, "11:2": 4, "11:3": -9}
        self.assertEqual(AUDIT["largest"](values, "largest_benefit"), "11:2")
        self.assertEqual(AUDIT["largest"](values, "largest_absolute"), "11:3")
        self.assertIsNone(AUDIT["largest"]({"11:1": -2}, "largest_benefit"))
        self.assertIsNone(AUDIT["largest"]({"11:1": 0}, "largest_absolute"))

    def test_exclusion_is_symmetric_one_run_task_not_all_ids(self):
        def task(cost):
            return dict(completed=True, failed=False, **{k: cost for k in AUDIT["FIELDS"]})
        def recovery(identity, catch, time="10"):
            return dict(task_id=identity, fault_time_ns=time, fault_type="compute", actual_T_catch_ns=catch,
                remote_node="1", phase_at_fault="ON", remote_busy_at_fault="0", chosen_path="TAIL",
                terminal_state="COMPLETED", checkpoint_relocation_bytes="0", attempt_generation="1")
        a = {"11:1": task(10), "12:1": task(10), "12:2": task(10)}
        b = {"11:1": task(2), "12:1": task(9), "12:2": task(10)}
        ra = [recovery("11:1", "9000000000"), recovery("12:1", "1000000000"), recovery("12:2", "")]
        rb = [recovery("11:1", "1000000000"), recovery("12:1", "2000000000"), recovery("12:2", "0")]
        result = AUDIT["leave_one_out"](a, b, ra, rb, ("full", "rational-U"))
        self.assertTrue(all(r["selected_instance"] == "11:1" for r in result))
        p = result[0]["result"]
        self.assertEqual((p["reference_cohort"]["tasks"], p["candidate_cohort"]["tasks"]), (2, 2))
        self.assertEqual(p["paired_catch"]["paired_catch_count"], 1)
        self.assertEqual(p["paired_catch"]["common_without_two_catches"], 1)
        self.assertEqual(p["paired_catch"]["delta_rational-U_minus_full_seconds"]["mean"], 1)
        self.assertEqual(p["delta_candidate_minus_reference"]["w_waste_actual"], -1)
        # A different actual fault timestamp is not a paired observation, nor a zero catch.
        rb[1]["fault_time_ns"] = "11"
        q = AUDIT["contrast"](a, b, ra, rb, ("full", "rational-U"))
        self.assertEqual(q["paired_catch"]["paired_catch_count"], 1)
        self.assertEqual(q["paired_catch"]["full_only_faults"], 1)


if __name__ == "__main__":
    unittest.main()
