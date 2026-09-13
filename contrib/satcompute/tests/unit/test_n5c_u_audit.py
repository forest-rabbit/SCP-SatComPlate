"""Gate A: bounded matrix, causal offline history and honest paired populations."""
import json
from pathlib import Path
import runpy
import shlex
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1] / "integration/regression"
RUN = runpy.run_path(str(HERE / "run-n5c-u-audit.py"))
AUDIT = runpy.run_path(str(HERE / "analyze-n5c-u-audit.py"))


class UAuditTests(unittest.TestCase):
    def test_matrix_and_only_random_run_changes(self):
        self.assertEqual(RUN["RUNS"], (11,12,13,14,15))
        self.assertEqual(list(RUN["GROUPS"]), ["fa-ffp","full","noU"])
        self.assertEqual(len(RUN["RUNS"][1:])*len(RUN["GROUPS"]), 12)
        for group in RUN["GROUPS"]:
            reference = RUN["flags"](RUN["arguments"](Path("unused"), group, 11))
            for run in RUN["RUNS"]:
                actual = RUN["flags"](RUN["arguments"](Path("unused"), group, run))
                self.assertEqual(actual.pop("randomRun"), str(run))
                self.assertEqual(actual, {k:v for k,v in reference.items() if k != "randomRun"})
        for run in (0,16):
            with self.assertRaises(ValueError):
                RUN["arguments"](Path("unused"), "full", run)
        for run in (0,-1,True,1.5,2**63):
            with self.assertRaises(ValueError):
                RUN["FINAL"]["arguments"](Path("unused"), random_run=run)
        self.assertIn("--randomRun=11", RUN["FINAL"]["arguments"](Path("unused")))

    def test_execution_rejects_wrong_identity_or_physics(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            value = dict(seed=1,run=12,simulation_duration_s=1300,commit="fixed",worktree_dirty=False,
                fault_mode="generate",protection_mode="compfrr",input_staging_policy="deferred",
                remote_busy_recovery_policy="relocate",placement_mode="n5c",n5c_variant="full",
                audit=False,shadow=False,command=["ns3","run",shlex.join(RUN["arguments"](root,"full",12))])
            (root/"execution-result.json").write_text('{"returncode":0}')
            def verify(v):
                (root/"execution.json").write_text(json.dumps(v))
                return RUN["verify_execution"](root,"full",12,"fixed")
            self.assertEqual(verify(value),value)
            for key,bad in (("seed",2),("run",11),("commit","stale"),("worktree_dirty",True),
                            ("n5c_variant","noU"),("fault_mode","validation-replay"),("audit",True)):
                with self.subTest(key=key), self.assertRaises(ValueError):
                    verify(dict(value, **{key:bad}))
            wrong = dict(value,command=[value["command"][-1].replace("--fixedDelay=0.001","--fixedDelay=0.008")])
            with self.assertRaisesRegex(ValueError,"frozen command"):
                verify(wrong)
            (root/"execution-result.json").write_text('{"returncode":1}')
            with self.assertRaisesRegex(ValueError,"incomplete"):
                verify(value)

    def test_history_clips_future_and_uses_actual_service_only(self):
        history = AUDIT["History"]({1:[(0,10),(80,90)]},{1:[(20,25)]},{1:100})
        q = history.query(1,50)
        self.assertEqual((q["normal_busy_ns"],q["recovery_busy_ns"],q["exposure_ns"]),(10,5,50))
        self.assertEqual(q["cumulative_U"],.3)
        self.assertEqual(q["idle_duration_ns"],25)
        self.assertEqual(q["last_busy_end_ns"],25)
        self.assertEqual(history.query(1,200)["exposure_ns"],100)
        self.assertEqual(history.query(1,200)["cumulative_U"],.25)
        self.assertEqual(history.query(1,22)["recovery_busy_ns"],2)
        self.assertIsNone(history.query(1,22)["idle_duration_ns"])
        self.assertTrue(history.query(1,25)["same_ns_boundary"])
        self.assertIsNone(history.query(1,25)["idle_duration_ns"])
        self.assertIsNone(history.query(3,0)["cumulative_U"])
        # A candidate's old busy interval is not evidence of recent busy service.
        self.assertGreater(q["cumulative_U"],0)
        self.assertGreaterEqual(q["idle_duration_ns"],20)
        for normal,recovery,f3 in (({1:[(0,10)]},{1:[(9,11)]},{}),({1:[(0,10)]},{},{1:9})):
            with self.assertRaises(ValueError):
                AUDIT["History"](normal,recovery,f3)

    def test_counterfactual_is_same_snapshot_without_new_frequency(self):
        def candidate(node,R,M,U,prop,feasible="1"):
            return dict(candidate_node=str(node),recovery_conflict=str(R),storage_pressure=str(M),
                historical_utilization=str(U),propagation_ns=str(prop),feasible=feasible)
        old = candidate(13,0,.001,.6,1)
        idle = candidate(15,0,.001,0,3)
        illegal = candidate(12,0,0,0,0,"0")
        self.assertEqual(AUDIT["snapshot_no_u"]([illegal,idle,old]),old)
        self.assertEqual(AUDIT["snapshot_no_u"]([old,candidate(10,0,.001,0,1)])["candidate_node"],"10")
        self.assertIsNone(AUDIT["snapshot_no_u"]([illegal]))
        self.assertEqual(old["historical_utilization"],"0.6")

    def test_remaining_time_is_not_deadline_slack(self):
        task=dict(compute_work_units="550722",compute_rate_work_units_per_second="100000",
                  compute_start_time_ns="100",compute_deadline_time_ns="9999999999999")
        self.assertEqual(AUDIT["remaining_ns"](task,100),5507220000)
        self.assertEqual(AUDIT["remaining_ns"](task,1000000100),4507220000)

    def test_paired_faults_missing_catch_and_signed_difference(self):
        def row(task,time,catch):
            return dict(task_id=str(task),fault_time_ns=str(time),fault_type="compute",actual_T_catch_ns=str(catch))
        a=[row(1,10,2000000000),row(2,20,""),row(3,30,1)]
        b=[row(1,10,1000000000),row(2,20,1),row(3,31,1)]
        result=AUDIT["paired_catch"](a,b)
        self.assertEqual(result["paired_catch_count"],1)
        self.assertEqual(result["common_without_two_catches"],1)
        self.assertEqual(result["full_only_faults"],1)
        self.assertEqual(result["noU_only_faults"],1)
        self.assertEqual(result["delta_noU_minus_full_seconds"]["mean"],-1)
        self.assertNotIn("hhi",result["delta_noU_minus_full_seconds"])
        self.assertIsNone(AUDIT["paired_catch"]([],[])["full"]["mean"])
        with self.assertRaisesRegex(ValueError,"duplicate recovery"):
            AUDIT["paired_catch"]([a[0],a[0]],b)

    def test_task_diff_preserves_missing_and_shifted_starts(self):
        def commit(time,node=13):
            return dict(row={k:str(v) for k,v in dict(time_ns=time,candidate_node=node,reference_local_node=11,
                delta_permille=50,batch_n=4,historical_utilization=.1,recovery_conflict=0,
                storage_pressure=.1,propagation_ns=1).items()}, detail=dict(same_snapshot_noU_remote=13,
                same_snapshot_remote_changed=True,historical_inertia_diagnostic=True))
        tasks={str(i):dict(final_state="COMPLETED") for i in range(1,6)}
        full=dict(tasks=tasks,committed={"1":commit(1),"3":commit(2,15),"4":commit(3)})
        nou=dict(tasks=tasks,committed={"2":commit(1),"3":commit(4),"4":commit(3)})
        result=AUDIT["task_diff"](full,nou,11)
        self.assertEqual([r["match"] for r in result],
            ["FULL_ONLY","NOU_ONLY","BOTH_DIFFERENT_TIME","BOTH_SAME_TIME","NEITHER_COMMITTED"])
        self.assertIsNone(result[0]["actual_remote_changed"])
        self.assertTrue(result[2]["actual_remote_changed"])

    def test_fixture_comparison_does_not_ignore_business_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/"a",Path(tmp)/"b"
            a.mkdir(); b.mkdir()
            for root in (a,b):
                (root/"values.csv").write_text("count\n1\n")
            self.assertEqual(RUN["fixture_equivalence"](a,b)["files"],1)
            (b/"values.csv").write_text("count\n2\n")
            with self.assertRaisesRegex(ValueError,"fixture differs"):
                RUN["fixture_equivalence"](a,b)

    def test_source_guard_rejects_production_edits(self):
        with patch.dict(RUN["runtime_equivalence"].__globals__,git=lambda *a:"contrib/satcompute/para.cc"):
            with self.assertRaisesRegex(ValueError,"production"):
                RUN["runtime_equivalence"]("candidate")


if __name__ == "__main__":
    unittest.main()
