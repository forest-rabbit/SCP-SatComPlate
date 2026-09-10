"""G4 analysis guards: planned/actual integer work, reserved-idle, no double cR."""
from pathlib import Path
import runpy
import unittest

MODULE = Path(__file__).resolve().parents[2]
API = runpy.run_path(str(MODULE / "tests/integration/regression/analyze-protection-accounting.py"))


class RecoveryAccountingTests(unittest.TestCase):
    def row(self):
        return dict(planned_total_recovery_wu="100", planned_catchup_redo_wu="30", planned_post_catchup_wu="70",
                    actual_total_recovery_wu="50", actual_catchup_redo_wu="30", actual_post_catchup_wu="20",
                    actual_recovery_service_ns="500000000", recovery_rate_wu_per_s="100", primary_rate_wu_per_s="200",
                    recovery_compute_complete_time_ns="", catchup_time_ns="310000001", recovery_accept_time_ns="1",
                    recovery_compute_start_time_ns="10000001", terminal_time_ns="510000001", reserved_idle_ns="10000000",
                    normal_protection_cost_ns="2000000", normal_protection_eq_wu="0.4",
                    recovery_reserved_idle_eq_wu="1", w_waste_actual="31.4")

    def test_actual_prefix_and_execution_ratio(self):
        self.assertEqual(API["recovery_check"](self.row())["execution_ratio"], .5)

    def test_invalid_ledger_is_rejected(self):
        for key, value in (("actual_total_recovery_wu", "100"), ("w_waste_actual", "32.4"),
                           ("reserved_idle_ns", "11000000"), ("recovery_compute_complete_time_ns", "510000001"),
                           ("catchup_time_ns", ""), ("normal_protection_eq_wu", "0.2")):
            with self.subTest(key=key):
                row = self.row()
                row[key] = value
                with self.assertRaises(ValueError):
                    API["recovery_check"](row)

    def test_empty_and_interpolated_distribution(self):
        self.assertEqual(API["stats"]([])["p50"], None)
        self.assertEqual(API["stats"]([0, 10])["p90"], 9)

    def test_migration_requires_real_committed_bytes(self):
        task = dict(compute_work_units="1000", input_bytes="1000", task_profile="dense-image")
        protected = dict(variable_state_bytes="1500", cR_ns="5")
        r = dict(chosen_path="MIGRATE_TAIL", remote_work_units="200", checkpoint_state_bytes="1100",
                 checkpoint_relocation_bytes="1100", old_remote_node="0", new_recovery_node="1",
                 input_start_time_ns="", recovery_compute_start_time_ns="35", state_received_time_ns="20",
                 tail_received_time_ns="30", tail_commit_time_ns="35")
        flows = [dict(kind="RECOVERY_STATE", bytes="1100", source_node="0", destination_node="1")]
        API["relocation_check"](task, protected, r, flows)
        for key, value in (("checkpoint_state_bytes", "300"), ("tail_commit_time_ns", "40"),
                           ("input_start_time_ns", "1"), ("recovery_compute_start_time_ns", "19")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                API["relocation_check"](task, protected, {**r, key: value}, flows)


if __name__ == "__main__":
    unittest.main()
