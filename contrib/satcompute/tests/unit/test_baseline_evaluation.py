"""Physical flow de-duplication and planned/actual baseline comparison gates."""
import csv
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

ANALYSIS = runpy.run_path(str(Path(__file__).resolve().parents[1] /
    "integration/regression/analyze-baseline-evaluation.py"))


def table(root, name, records):
    with (root / name).open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


class BaselineEvaluationTests(unittest.TestCase):
    def test_unified_execution_waste_examples_and_failed_attempts(self):
        # A-E from the audit, plus failed checkpoint post-catchup and failed 1+1.
        cases = [
            ("recompute success", True, 60, 100, 0, 0, 2, 60, 62),
            ("recompute refused", False, 60, 0, 0, 0, 0, 60, 60),
            ("recovery interrupted", False, 60, 20, 0, 0, 0, 80, 80),
            ("checkpoint success", True, 60, 45, 0, 8, 2, 5, 15),
            ("replica success", True, 80, 0, 100, 0, 3, 80, 83),
            ("failed after catchup", False, 60, 35, 0, 8, 2, 95, 105),
            ("both replicas failed", False, 20, 0, 30, 0, 3, 50, 53),
            ("ordinary success", True, 100, 0, 0, 0, 0, 0, 0),
        ]
        for label, success, primary, recovery, replica, normal, idle, waste, total in cases:
            with self.subTest(label=label):
                r = ANALYSIS["execution_waste"](100, success, primary, recovery, replica, normal, idle)
                self.assertEqual(r["task_execution_waste_wu"], waste)
                self.assertEqual(r["w_waste_actual"], total)
                self.assertEqual(r["successful_extra_execution_wu"], waste if success else 0)
                self.assertEqual(r["failed_task_executed_wu"], 0 if success else primary+recovery+replica)
                self.assertEqual(r["total_executed_wu"]-r["useful_work_wu"], waste)

    @staticmethod
    def task(success=True):
        return dict(task_id="7", compute_work_units="100", final_state="COMPLETED" if success else "FAILED",
            compute_deadline_met="1" if success else "0", task_success="1" if success else "0",
            compute_start_time_ns="0", compute_complete_time_ns="1000000000" if success else "-1",
            compute_service_time_ns="1000000000" if success else "-1",
            compute_rate_work_units_per_second="100", failure_time_ns="-1" if success else "600000000")

    @staticmethod
    def recovery(executed=45, accepted=True, catchup=5):
        return dict(fault_time_ns="600000000", actual_work_units="60",
            recovery_accept_time_ns="600000001" if accepted else "",
            recovery_rate_wu_per_s="100", actual_total_recovery_wu=str(executed),
            actual_recovery_service_ns=str(executed*10000000), actual_catchup_redo_wu=str(catchup))

    @staticmethod
    def impact():
        return dict(progress_valid="1", task_state_before_fault="RUNNING", fault_time_ns="600000000",
                    compute_start_time_ns="0", completed_work_units_at_fault="60")

    def test_fault_snapshot_not_overwritten_logical_service_reconstructs_primary(self):
        task = self.task()
        task["compute_service_time_ns"] = "450000000"  # Logical record contains recovery service only.
        r = ANALYSIS["task_execution"](task, self.recovery(), {}, [], [self.impact()], 8, 2)
        self.assertEqual((r["primary_actual_wu"], r["recovery_actual_wu"], r["task_execution_waste_wu"]), (60, 45, 5))
        self.assertEqual((r["primary_actual_service_ns"], r["recovery_actual_service_ns"]), (600000000, 450000000))

    def test_failed_reconstruction_counts_primary_and_all_recovery_including_post(self):
        for executed, catchup in ((0, 0), (20, 20), (35, 5)):
            with self.subTest(executed=executed):
                r = ANALYSIS["task_execution"](self.task(False),
                    self.recovery(executed, executed > 0, catchup), {}, [], [self.impact()], 8, 2)
                self.assertEqual(r["failed_task_executed_wu"], 60+executed)
                self.assertEqual(r["w_waste_actual"], 70+executed)

    def test_actual_reconstruction_never_uses_planned_or_estimated_work(self):
        recovery = self.recovery()
        expected = ANALYSIS["task_execution"](self.task(), recovery, {}, [], [self.impact()], 8, 2)
        recovery.update(planned_catchup_redo_wu="999999", planned_total_recovery_wu="999999",
                        estimated_recompute_ns="999999999999", planned_reserved_idle_eq_wu="123456")
        self.assertEqual(ANALYSIS["task_execution"](self.task(), recovery, {}, [], [self.impact()], 8, 2), expected)
        del recovery["actual_total_recovery_wu"]
        with self.assertRaisesRegex(ValueError, "task 7: missing actual actual_total_recovery_wu"):
            ANALYSIS["task_execution"](self.task(), recovery, {}, [], [self.impact()], 8, 2)

    def test_missing_or_conflicting_actual_evidence_stops_with_task_id(self):
        reconstruct = ANALYSIS["task_execution"]
        for impacts in ([], [self.impact(), self.impact()], [{**self.impact(), "completed_work_units_at_fault": "59"}]):
            with self.subTest(impacts=impacts), self.assertRaisesRegex(ValueError, "task 7:"):
                reconstruct(self.task(), self.recovery(), {}, [], impacts, 8, 2)
        with self.assertRaisesRegex(ValueError, "task 7: successful recovery catchup conservation"):
            reconstruct(self.task(), self.recovery(catchup=4), {}, [], [self.impact()], 8, 2)
        with self.assertRaisesRegex(ValueError, "less than useful"):
            ANALYSIS["execution_waste"](100, True, 60, 0, 0, 0, 0)

    def test_success_uses_compute_deadline_not_result_delivery_time(self):
        task = self.task()
        task.update(compute_deadline_time_ns="1100000000", result_transfer_complete_time_ns="9000000000")
        r = ANALYSIS["task_execution"](task, {}, {}, [], [], 0, 0)
        self.assertEqual(r["useful_work_wu"], 100)
        task.update(compute_deadline_met="0", task_success="0")
        r = ANALYSIS["task_execution"](task, {}, {}, [], [], 0, 0)
        self.assertEqual((r["useful_work_wu"], r["failed_task_executed_wu"]), (0, 100))

    def test_unstarted_and_uninterrupted_failed_primary(self):
        task = self.task(False)
        self.assertEqual(ANALYSIS["task_execution"](task, {}, {}, [], [], 0, 0)["failed_task_executed_wu"], 60)
        task["compute_start_time_ns"] = "-1"
        self.assertEqual(ANALYSIS["task_execution"](task, {}, {}, [], [], 0, 0)["failed_task_executed_wu"], 0)

    def test_replica_reconstruction_sums_physical_attempts_for_success_and_failure(self):
        for success, primary, replica in ((True, 80, 100), (False, 20, 30)):
            with self.subTest(success=success):
                summary = dict(replica_admitted="1", primary_executed_wu=str(primary), replica_executed_wu=str(replica),
                    total_executed_wu=str(primary+replica), redundant_actual_wu=str(primary+replica-100 if success else 0),
                    failed_raw_executed_wu=str(0 if success else primary+replica))
                attempts = [dict(role=role, actual_work_units=str(wu), actual_service_ns=str(wu*10000000), rate_wu_per_s="100")
                            for role, wu in (("primary", primary), ("replica", replica))]
                r = ANALYSIS["task_execution"](self.task(success), {}, summary, attempts, [], 0, 3)
                self.assertEqual(r["task_execution_waste_wu"], primary+replica-(100 if success else 0))
                attempts[1]["actual_work_units"] = "0"
                with self.assertRaisesRegex(ValueError, "task 7: replica service"):
                    ANALYSIS["task_execution"](self.task(success), {}, summary, attempts, [], 0, 3)

    def test_frozen_audit_anchors_reject_incomplete_failure_accounting(self):
        # Recorded aggregate inputs exercise the gate separately from the raw-data CLI run.
        values = [
            (0, 328159513, 13472155, 0, 317760919, 2197227, 21673522, 0, 522006.8628, 742),
            (1, 328177528, 0, 345912166, 352513119, 321576575, 0, 0, 15721457.8606, 800),
            (3, 328177528, 25730075, 0, 352513119, 1394484, 0, 1523120, 368068.7436, 800),
            (5, 328177528, 25690904, 0, 352513119, 1355313, 0, 441510, 547675.5932, 800),
        ]
        runs = {}
        names = list(ANALYSIS["GROUPS"])
        for group, primary, recovery, replica, useful, extra, failed, normal, idle, completed in values:
            total = primary+recovery+replica
            s = dict(primary_actual_wu=primary, replica_actual_wu=replica, total_executed_wu=total,
                useful_work_wu=useful, task_execution_waste_wu=total-useful,
                successful_extra_execution_wu=extra, failed_task_executed_wu=failed,
                normal_protection_eq_wu=normal, reserved_idle_eq_wu=idle,
                w_waste_actual=total-useful+normal+idle, completed=completed, on_time=completed,
                actual_catchup_wu=extra)
            runs[names[group]] = dict(summary=s, profiles=dict(aggregate=s))
        for group, ids in ((2, ("114", "252")), (4, ("114", "252", "475"))):
            failed = [dict(task_id=i, primary_actual_wu=60, recovery_actual_wu=20,
                replica_actual_wu=0, failed_task_executed_wu=80) for i in ids]
            s = dict(task_execution_waste_wu=80*len(ids), successful_extra_execution_wu=0,
                failed_task_executed_wu=80*len(ids), total_executed_wu=80*len(ids), useful_work_wu=0,
                w_waste_actual=80*len(ids), normal_protection_eq_wu=0, reserved_idle_eq_wu=0)
            runs[names[group]] = dict(summary=s, profiles=dict(aggregate=s), failed_task_execution=failed)
        self.assertTrue(ANALYSIS["accounting_gates"](runs)["passed"])
        # A coherent but wrong omitted-WU result must still fail the exact R0 anchor.
        s = runs[names[0]]["summary"]
        for key in ("task_execution_waste_wu", "failed_task_executed_wu", "total_executed_wu", "w_waste_actual"):
            s[key] -= 1
        with self.assertRaisesRegex(ValueError, "R0 exact"):
            ANALYSIS["accounting_gates"](runs)

    def test_cli_refuses_to_overwrite_original_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = [(["--root", str(root)], root / "R0-recompute-ffp/task-summary.csv"),
                     (["--reference", str(root/"old"), "--candidate", str(root/"new")], root/"new/run-summary.json")]
            for options, output in cases:
                with self.subTest(options=options), patch("sys.argv", ["analyzer", *options, "--output", str(output)]):
                    with self.assertRaisesRegex(ValueError, "must not overwrite raw"):
                        ANALYSIS["main"]()
                    self.assertFalse(output.exists())

    def test_real_flow_union_counts_winner_once_and_keeps_loser_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table(root, "transfer-summary.csv", [dict(transfer_id="10", sent_application_bytes=100,
                received_application_bytes=100, declared_size_bytes=100, terminal_state="COMPLETED"),
                dict(transfer_id="31", sent_application_bytes=50, received_application_bytes=50,
                     declared_size_bytes=50, terminal_state="COMPLETED")])
            table(root, "protection-transfers.csv", [dict(transfer_id="30", kind="REPLICA_INPUT",
                sent_bytes=100, received_bytes=100, bytes=100, state="COMPLETED")])
            flows = [dict(transfer_id="30", kind="REPLICA_INPUT", sent_bytes=100, received_bytes=100,
                     declared_bytes=100, business_result="0", state="COMPLETED"),
                     dict(transfer_id="31", kind="REPLICA_RESULT", sent_bytes=50, received_bytes=50,
                     declared_bytes=50, business_result="1", state="COMPLETED"),
                     dict(transfer_id="11", kind="PRIMARY_RESULT", sent_bytes=4, received_bytes=0,
                     declared_bytes=50, business_result="0", state="CANCELLED")]
            table(root, "replica-transfers.csv", flows)
            result = ANALYSIS["physical_network"](root, [dict(input_transfer_id="10")])
            self.assertEqual((result["business_sent_bytes"], result["extra_sent_bytes"],
                              result["physical_flow_count"]), (150, 104, 4))
            self.assertEqual(result["by_kind"]["RESULT"]["sent_bytes"], 4)
            flows[0]["sent_bytes"] = 99
            table(root, "replica-transfers.csv", flows)
            with self.assertRaisesRegex(ValueError, "duplicate flow"):
                ANALYSIS["physical_network"](root, [dict(input_transfer_id="10")])

    def test_planned_wait_never_uses_actual_wait(self):
        planned = ANALYSIS["planned_wait"]
        self.assertEqual(planned(dict(planned_input_wait_ns="200", reserved_idle_ns="900")), 200)
        self.assertEqual(planned(dict(chosen_path="RECOMPUTE", estimated_recompute_ns="2000000000",
            recovery_rate_wu_per_s="100", planned_catchup_redo_wu="150", reserved_idle_ns="900")), 500000000)
        self.assertEqual(planned(dict(chosen_path="REMOTE_REDO", recovery_accept_time_ns="1")), 0)
        self.assertIsNone(planned({}))

    def test_failed_replica_reports_raw_wu_not_negative_redundancy(self):
        row = dict(primary_executed_wu="20", replica_executed_wu="30", total_executed_wu="50",
            total_work_units="100", terminal_state="FAILED", redundant_actual_wu="0", failed_raw_executed_wu="50")
        ANALYSIS["replica_check"](row, [])
        row["redundant_actual_wu"] = "-50"
        with self.assertRaisesRegex(ValueError, "negative charge"):
            ANALYSIS["replica_check"](row, [])

    def test_r5_gate_ignores_only_wall_clock(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = (Path(directory) / x for x in ("a", "b"))
            for root in (a, b):
                root.mkdir()
                (root / "recovery-summary.csv").write_text("id\n1\n")
                for name in (*ANALYSIS["ACCOUNTING"]["BUSINESS_JSON"], "protection-finalization.json"):
                    (root / name).write_text(json.dumps(dict(value=1, wall_clock_ns=100, wall_clock_s=.0000001)))
            path = b / "run-summary.json"
            path.write_text(json.dumps(dict(value=1, wall_clock_ns=200, wall_clock_s=.0000002)))
            self.assertTrue(ANALYSIS["strict_equivalence"](a, b)["passed"])
            path.write_text(json.dumps(dict(value=2, wall_clock_ns=200, wall_clock_s=.0000002)))
            self.assertFalse(ANALYSIS["strict_equivalence"](a, b)["passed"])

    def test_busy_pairing_requires_same_fault_not_just_task(self):
        row = dict(task_id="1", fault_time_ns="100", fault_type="compute", chosen_path="RECOMPUTE",
            terminal_state="FAILED", terminal_reason="COMPUTE_DEADLINE_EXCEEDED", actual_T_catch_ns="",
            actual_catchup_redo_wu="12", recovery_reserved_idle_eq_wu="1")
        def run(r):
            return dict(busy=dict(rows=[r], actual_catchup_wu=12, reserved_idle_eq_wu=1,
                input_state_tail_sent_bytes=dict(RECOVERY_INPUT=10, RECOVERY_STATE=0, RECOVERY_TAIL=0)))
        result = ANALYSIS["busy_comparison"](run(row), run({**row, "fault_time_ns": "101"}))
        self.assertFalse(result["matched_events"])
        self.assertEqual(len(result["before_unmatched"]), 1)
        result = ANALYSIS["busy_comparison"](run(row), run(row))
        self.assertIsNone(result["matched_events"][0]["T_catch_difference_s"])


if __name__ == "__main__":
    unittest.main()
