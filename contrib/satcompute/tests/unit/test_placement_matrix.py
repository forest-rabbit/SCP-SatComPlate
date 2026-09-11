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


if __name__ == "__main__":
    unittest.main()
