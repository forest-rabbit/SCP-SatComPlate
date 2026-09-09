"""Generic link-utilization report statistics, independent of pressure candidates."""
from pathlib import Path
import runpy
import unittest

REPORT = runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/validation/summarize-pressure-baseline.py"))


class LinkMetricsReportTest(unittest.TestCase):
    def test_percentiles(self):
        self.assertEqual(REPORT["percentile"]([1, 2, 3], .5), 2)
        self.assertAlmostEqual(REPORT["percentile"]([1, 2, 3], .95), 2.9)


if __name__ == "__main__":
    unittest.main()
