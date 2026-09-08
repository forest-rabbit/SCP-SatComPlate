"""The C800 comparison gate must detect missing or changed business evidence."""
import json
from pathlib import Path
import runpy
import tempfile
import unittest

TOOL = runpy.run_path(str(Path(__file__).resolve().parents[2] /
                         "tools/validation/compare-n4c-runs.py"))


class RunComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.left, self.right = (Path(self.temporary.name) / name for name in ("a", "b"))
        for index, directory in enumerate((self.left, self.right)):
            directory.mkdir()
            for name in TOOL["CORE"]:
                (directory / name).write_text("same\n")
            (directory / "task-summary.csv").write_text(
                "final_state,compute_deadline_met,result_delivered,task_success,failure_reason\n" +
                "COMPLETED,1,1,1,\n" * 800)
            (directory / "transfer-summary.csv").write_text("terminal_state\n" + "COMPLETED\n" * 1600)
            (directory / "run-summary.json").write_text(json.dumps({"wall_clock_s": index, "tasks": 800}))

    def test_wall_clock_and_optional_audit_are_not_business(self):
        (self.right / "fault-predictions.csv").write_text("audit only\n")
        self.assertTrue(TOOL["compare"](self.left, self.right)["equal"])
        (self.right / "run-summary.json").write_text(json.dumps({"tasks": 799}))
        with self.assertRaises(ValueError):
            TOOL["compare"](self.left, self.right)

    def test_business_mutation_and_missing_file_are_rejected(self):
        (self.right / "task-events.csv").write_text("changed\n")
        with self.assertRaises(ValueError):
            TOOL["compare"](self.left, self.right)
        (self.right / "task-events.csv").unlink()
        with self.assertRaises(ValueError):
            TOOL["compare"](self.left, self.right)


if __name__ == "__main__":
    unittest.main()
