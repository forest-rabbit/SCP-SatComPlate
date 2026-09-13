"""Small offline gates for N5C scope, normalization and corruption rejection."""
import csv
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
        import json
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
