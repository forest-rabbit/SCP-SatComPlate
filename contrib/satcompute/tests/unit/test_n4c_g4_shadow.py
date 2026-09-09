"""Read-only equivalence gate and reporting tests, with deliberately corrupt evidence."""
import json
from pathlib import Path
import runpy
import tempfile
import unittest

CHECK = runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/validation/summarize-n4c-g4-shadow.py"))


class ShadowEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.left, self.right = [Path(self.temporary.name) / s for s in ("left", "right")]
        for path in (self.left, self.right):
            path.mkdir()
            for name in CHECK["REQUIRED"]:
                (path / name).write_text('{"outcome": 1}' if name.endswith(".json") else "id,state\n1,FAILED\n")

    def test_only_wall_clock_is_exempt(self):
        for path, wall in ((self.left, 1), (self.right, 2)):
            (path / "run-summary.json").write_text(json.dumps({"outcome": 1, "wall_clock_ns": wall, "wall_clock_s": wall / 1e9}))
        self.assertTrue(CHECK["compare_business"](self.left, self.right)["pass"])
        (self.right / "run-summary.json").write_text('{"outcome": 2}')
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])

    def test_csv_mutation_missing_file_and_audit_presence_fail(self):
        self.assertTrue(CHECK["compare_business"](self.left, self.right)["pass"])
        victim = self.right / "fault-events.csv"
        victim.write_text("id,state\n1,COMPLETED\n")
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])
        victim.unlink()
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])
        victim.write_bytes((self.left / victim.name).read_bytes())
        (self.right / "fault-model-probabilities.csv").write_text("id,p\n1,0.5\n")
        self.assertFalse(CHECK["compare_business"](self.left, self.right)["pass"])

    def test_distribution_uses_linear_quantiles_not_success_accuracy(self):
        result = CHECK["distribution"]([1, 2, 3, 4])
        self.assertEqual(result["P50"], 2.5)
        self.assertAlmostEqual(result["P90"], 3.7)
        self.assertEqual(CHECK["distribution"]([]), {"count": 0})


if __name__ == "__main__":
    unittest.main()
