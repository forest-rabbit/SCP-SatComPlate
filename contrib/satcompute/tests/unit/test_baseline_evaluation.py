"""Physical flow de-duplication and planned/actual baseline comparison gates."""
import csv
import json
from pathlib import Path
import runpy
import tempfile
import unittest

ANALYSIS = runpy.run_path(str(Path(__file__).resolve().parents[1] /
    "integration/regression/analyze-baseline-evaluation.py"))


def table(root, name, records):
    with (root / name).open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


class BaselineEvaluationTests(unittest.TestCase):
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
