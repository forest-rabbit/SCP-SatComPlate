"""Pressure matrix placement and budget checks, without running ns-3."""

import runpy
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[2] / "tools/generation/prepare-pressure-baseline.py"
PREPARE = runpy.run_path(str(SCRIPT))


class PressureBaselineTests(unittest.TestCase):
    def test_compute_placement(self):
        for size, expected in ((66, 66), (351, 117), (720, 240)):
            ids = PREPARE["compute_ids"](size)
            self.assertEqual(len(ids), expected)
            self.assertEqual(len(set(ids)), expected)
            self.assertTrue(all(0 <= node < size for node in ids))

    def test_arrival_drain_and_budget(self):
        self.assertEqual(PREPARE["INPUT_BYTES"], 81_750_000_000)
        for size, (planes, per_plane, duration, last_arrival) in PREPARE["SCALES"].items():
            self.assertEqual(planes * per_plane, size)
            self.assertLess(last_arrival, duration)


if __name__ == "__main__":
    unittest.main()
