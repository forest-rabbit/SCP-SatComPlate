"""Failure evidence must remain visible and must never pass the ensemble gate."""
from pathlib import Path
import runpy
import unittest

MODULE = runpy.run_path(str(Path(__file__).resolve().parents[2] /
                          "tools/validation/summarize-n4c-g3.py"))


class G3AcceptanceTests(unittest.TestCase):
    def test_completed_and_declared_direct_failures_pass(self):
        result = MODULE["lifecycle_acceptance"](
            [{"task_id": "1", "final_state": "COMPLETED"}, {"task_id": "2", "final_state": "FAILED"}],
            {"flow_monitor_lost_packets": 0, "link_queue_drops": 0}, set())
        self.assertEqual(result, {"passed": True, "errors": [], "truncated_task_ids": []})

    def test_truncation_loss_and_collateral_fail_closed_and_are_preserved(self):
        cases = (
            ([{"task_id": "642", "final_state": "RESULT_TRANSFERRING"}], 95, 0, set(), [642]),
            ([{"task_id": "411", "final_state": "FAILED"}], 0, 0, {411}, []),
            ([{"task_id": "1", "final_state": "COMPLETED"}], 0, 1, set(), []),
        )
        for tasks, lost, drops, collateral, truncated in cases:
            with self.subTest(lost=lost, drops=drops, collateral=collateral):
                result = MODULE["lifecycle_acceptance"](tasks,
                    {"flow_monitor_lost_packets": lost, "link_queue_drops": drops}, collateral)
                self.assertFalse(result["passed"])
                self.assertTrue(result["errors"])
                self.assertEqual(result["truncated_task_ids"], truncated)


if __name__ == "__main__":
    unittest.main()
