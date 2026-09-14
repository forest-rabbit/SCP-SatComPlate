"""Small offline gates for N5C scope, normalization and corruption rejection."""
import csv
import json
from pathlib import Path
import runpy
import tempfile
import unittest

HERE=Path(__file__).resolve().parents[1]/"integration/regression"
RUN=runpy.run_path(str(HERE/"run-n5c-placement.py"))
AUDIT=runpy.run_path(str(HERE/"analyze-n5c-placement.py"))
FINAL=runpy.run_path(str(HERE/"run-final-scenario.py"))


class N5cTests(unittest.TestCase):
    def test_scope_is_two_main_and_three_deferred_ablations(self):
        self.assertEqual(len(RUN["PHASES"]["main"]),2)
        self.assertEqual(len(RUN["PHASES"]["ablation"]),3)
        for group in RUN["PHASES"]["ablation"]:
            self.assertEqual(RUN["GROUPS"][group][0],"deferred")
            self.assertIn("relocate",RUN["command"](Path("unused"),group))

    def test_honest_zero_nodes_and_p95(self):
        d=AUDIT["distribution"]([0,0,0,4])
        self.assertEqual(d["gini"],.75)
        self.assertEqual(d["hhi"],1)
        self.assertEqual(d["top1_share"],1)
        self.assertEqual(d["max_to_mean"],4)
        self.assertAlmostEqual(d["p95"],3.4)
        self.assertIsNone(AUDIT["distribution"]([0]*66)["hhi"])

    def test_ablation_requires_both_matching_main_audits(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            audit={}
            for group in RUN["PHASES"]["main"]:
                directory=root/group
                directory.mkdir()
                execution=dict(commit="execution",worktree_dirty=False,group=group)
                (directory/"execution.json").write_text(json.dumps(execution))
                audit[group]=dict(execution=execution,n5c=dict(proposals=1))
            def save():
                (root/"comparison.json").write_text(json.dumps(audit))
            save()
            RUN["verify_main_audit"](root,"execution")
            for group in RUN["PHASES"]["main"]:
                kept=audit.pop(group)
                save()
                with self.assertRaisesRegex(ValueError,"audit both"):
                    RUN["verify_main_audit"](root,"execution")
                audit[group]=kept
            audit["R7-n5c"]["execution"]["commit"]="stale"
            save()
            with self.assertRaisesRegex(ValueError,"audit both"):
                RUN["verify_main_audit"](root,"execution")

    def test_migration_actual_bytes_and_failed_action_overlap(self):
        recoveries=[dict(task_id=str(i),attempt_generation="1",chosen_path=path,
            terminal_state=status,checkpoint_relocation_bytes=str(size))
            for i,path,status,size in ((1,"MIGRATE_TAIL","FAILED",100),
                (2,"TAIL","COMPLETED",0),(3,"RECOMPUTE","COMPLETED",0))]
        transfers=[dict(task_id=str(task),attempt_generation=str(attempt),transfer_id=str(flow),
            kind=kind,sent_bytes=str(sent),bytes="1000",state="CANCELLED")
            for task,attempt,flow,kind,sent in ((1,1,10,"RECOVERY_STATE",40),
                (1,1,11,"RECOVERY_TAIL",3),(1,1,12,"RECOVERY_INPUT",7),
                (1,0,13,"INIT_BASE",90),(2,1,14,"RECOVERY_INPUT",60))]
        transfers.append(dict(transfers[0]))
        r=AUDIT["recovery_composition"](recoveries,transfers)
        self.assertEqual(r["relocated_state_logical_bytes"],100)
        self.assertEqual(r["migration_total_sent_bytes"],50)
        self.assertEqual(r["migration_sent_bytes_by_kind"],dict(RECOVERY_INPUT=7,RECOVERY_STATE=40,RECOVERY_TAIL=3))
        for key in ("direct","relocate","recompute","recovery_failed"):
            self.assertEqual(r[key+"_count"],1)
            self.assertAlmostEqual(r["action_ratios"][key],1/3)
        transfers[-1]["sent_bytes"]="41"
        with self.assertRaisesRegex(ValueError,"duplicate migration"):
            AUDIT["recovery_composition"](recoveries,transfers)

    def test_empty_and_local_migration_do_not_invent_network_bytes(self):
        r=AUDIT["recovery_composition"]([],[])
        self.assertTrue(all(v is None for v in r["action_ratios"].values()))
        r=AUDIT["recovery_composition"]([dict(task_id="1",attempt_generation="1",
            chosen_path="MIGRATE_REDO",terminal_state="COMPLETED",checkpoint_relocation_bytes="0")],[])
        self.assertEqual(r["migration_total_sent_bytes"],0)
        self.assertEqual(r["action_ratios"]["relocate"],1)

    def test_frozen_scene_argument_contract(self):
        for variant in ("full","noR","noU","noM"):
            command=FINAL["arguments"](Path("unused"),protection_mode="compfrr",placement_mode="n5c",n5c_variant=variant)
            self.assertIn("--simulationDuration=1300",command)
            self.assertIn(f"--n5cVariant={variant}",command)
            self.assertIn("--randomRun=11",command)
        with self.assertRaises(ValueError):
            FINAL["arguments"](Path("unused"),protection_mode="fixed",placement_mode="n5c")
        with self.assertRaises(ValueError):
            FINAL["arguments"](Path("unused"),protection_mode="compfrr",n5c_variant="noM")

    def test_resource_audit_rejects_invented_integral(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            def write(name,values):
                with (root/name).open("w") as stream:
                    w=csv.DictWriter(stream,fieldnames=list(values[0]))
                    w.writeheader(); w.writerows(values)
            row=dict(node_id="1",backup_assignment_count="1",peak_active_backups="1",
                backup_assignment_time_integral_ns="3",active_backups="0",storage_bytes="0",
                peak_backup_storage_bytes="10",backup_storage_time_integral_byte_ns="20",
                mean_backup_storage_bytes="2",mean_active_backups=".3",normal_busy_ns="3",
                recovery_busy_ns="2",survival_exposure_ns="10",historical_utilization=".5")
            write("placement-resource-summary.csv",[row])
            write("protection-node-storage-summary.csv",[dict(node_id="1",peak_total_bytes="10")])
            write("placement-load-events.csv",[dict(node_id="1",task_id="1",time_ns=t,event=e) for t,e in
                (("2","ASSIGNMENT_ESTABLISHED"),("5","ASSIGNMENT_RELEASED"))])
            (root/"run-summary.json").write_text(json.dumps(dict(simulation_duration_ns=10)))
            self.assertEqual(AUDIT["resources"](root)["backup_assignment_count"]["sum"],1)
            row["backup_assignment_time_integral_ns"]="4"
            write("placement-resource-summary.csv",[row])
            with self.assertRaisesRegex(ValueError,"assignment integral"):
                AUDIT["resources"](root)
