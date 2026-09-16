"""Placement matrix scope, honest distributions and strict rename comparison."""
import csv
from pathlib import Path
import runpy
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1] / "integration/regression"
RUN = runpy.run_path(str(HERE / "run-pre-n5c-placement-matrix.py"))
AUDIT = runpy.run_path(str(HERE / "analyze-pre-n5c-placement-matrix.py"))


class PlacementMatrixTests(unittest.TestCase):
    def test_full_matrix_includes_two_gates(self):
        groups = [f"{s}-{m}" for s in RUN["SCENARIOS"] for m in RUN["MODES"]]
        self.assertEqual(len(groups), 32)
        self.assertEqual(len(set(groups)), 32)
        self.assertTrue(set(RUN["GATES"]) <= set(groups))
        for group in groups:
            command = RUN["command"](Path("unused"), group)
            self.assertEqual(command[command.index("--placement-mode")+1], group.split("-", 1)[1])

    def test_distribution_includes_zero_count_nodes(self):
        d = AUDIT["distribution"]([0, 0, 0, 4])
        self.assertEqual(d["p50"], 0)
        self.assertEqual(d["top1_share"], 1)
        self.assertEqual(d["gini"], .75)
        zero = AUDIT["distribution"]([0]*66)
        self.assertIsNone(zero["gini"])
        self.assertIsNone(zero["top3_share"])
        self.assertIsNone(AUDIT["distribution"]([])["mean"])

    def test_half_open_busy_window(self):
        values = [dict(task="a",start_ns=1,end_ns=3), dict(task="b",start_ns=3,end_ns=5)]
        self.assertEqual(AUDIT["overlapping"](values, 3, 4), [values[1]])
        self.assertEqual(AUDIT["overlapping"](values, 4, 4), [])

    def test_rename_csv_allows_name_only(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = (Path(directory)/n for n in ("a.csv", "b.csv"))
            def write(path, mode, count=3):
                with path.open("w") as stream:
                    writer = csv.writer(stream)
                    writer.writerow(["placement_mode", "actual_wu"])
                    writer.writerow([mode, count])
            write(a, "ffp"); write(b, "fa-ffp")
            self.assertEqual(RUN["csv_equivalent"](a, b), 1)
            write(b, "fa-ffp", 4)
            with self.assertRaisesRegex(ValueError, "behavior changed"):
                RUN["csv_equivalent"](a, b)
            write(b, "lrl")
            with self.assertRaises(ValueError):
                RUN["csv_equivalent"](a, b)

    def test_normalize_does_not_hide_business_changes(self):
        path = Path("/tmp/old-run")
        a = {"wall_clock_ns":1, "path":"/tmp/old-run/fault.json", "actual_wu":3}
        self.assertEqual(RUN["normalize"](a, path), {"path":"OUTPUT/fault.json", "actual_wu":3})


    def placement_rows(self, trigger, local="2", sampled="0"):
        decision = dict(task_id="7", fault_epoch_time_ns="5", phase_before="OFF",
                        decision_trigger=trigger, local_node=local, remote_node="0", placement_mode="ffp",
                        actual_fault_sampled=sampled, actual_fault_hit="0", resource_reason="NO_CAPACITY_NOW",
                        reason="NO_CAPACITY_NOW", decision_committed="0", proposed_action="NONE")
        selection = dict(task_id="7", time_ns="5", local_node=local, remote_node="0", placement_mode="ffp",
                         reason="NO_CAPACITY_NOW", actual_admission="NOT_ADMITTED")
        return selection, decision

    def test_normal_then_real_capacity_is_two_distinct_events(self):
        a, x = self.placement_rows("FAULT_EPOCH", sampled="1")
        b, y = self.placement_rows("CAPACITY_RELEASE", local="3")
        self.assertEqual(AUDIT["validate_selection_events"]([a, b], [x, y]),
                         dict(off_events_matched=2, same_ns_normal_then_capacity=1))
        self.assertEqual(b["decision_trigger"], "CAPACITY_RELEASE")
        self.assertEqual(b["same_ns_event_index"], 2)

    def test_repeated_capacity_or_extra_draw_is_rejected(self):
        a, x = self.placement_rows("CAPACITY_RELEASE")
        b, y = self.placement_rows("CAPACITY_RELEASE")
        with self.assertRaisesRegex(ValueError, "duplicate placement trigger family"):
            AUDIT["validate_selection_events"]([a, b], [x, y])
        x["actual_fault_sampled"] = "1"
        with self.assertRaisesRegex(ValueError, "sampled a fault"):
            AUDIT["validate_selection_events"]([a], [x])

    def test_wrong_pair_or_native_duplicate_is_rejected(self):
        a, x = self.placement_rows("TASK_RUNNING")
        a["remote_node"] = "9"
        with self.assertRaisesRegex(ValueError, "differs from its OFF event"):
            AUDIT["validate_selection_events"]([a], [x])
        with self.assertRaisesRegex(ValueError, "duplicate native/fixed"):
            AUDIT["validate_selection_events"]([a, a], [])

    def event(self, time, kind, work=0, generation="0"):
        return dict(time_ns=str(time), event=kind, local_work_units=str(work),
                    attempt_generation=generation)

    def test_snapshot_order_excludes_later_same_ns_commit(self):
        values = [self.event(3, "INIT_COST_COMMITTED"), self.event(4, "L1_COMMITTED_LOCAL", 10),
                  self.event(5, "FAULT_SNAPSHOT"), self.event(5, "L1_COMMITTED_LOCAL", 20)]
        self.assertEqual(AUDIT["local_commit_window"](values, 2, 5),
            dict(start_ns=4, end_ns=5, anchor_kind="L1_COMMITTED_LOCAL", committed_local_wu=10))
        values.insert(2, self.event(5, "L1_COMMITTED_LOCAL", 15))
        self.assertEqual(AUDIT["local_commit_window"](values, 2, 5)["committed_local_wu"], 15)

    def test_uncommitted_start_is_explicit_not_fake_commit(self):
        values = [self.event(1, "L1_COMMITTED_LOCAL", 2),
                  self.event(3, "L1_COMMITTED_LOCAL", 5, "1"),
                  self.event(6, "L1_COMMITTED_LOCAL", 9)]
        self.assertEqual(AUDIT["local_commit_window"](values, 2, 5),
            dict(start_ns=2, end_ns=5, anchor_kind="PROTECTION_START_NO_COMMIT", committed_local_wu=None))


if __name__ == "__main__":
    unittest.main()
