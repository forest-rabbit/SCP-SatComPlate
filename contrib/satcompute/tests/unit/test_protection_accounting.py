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


if __name__ == "__main__":
    unittest.main()
