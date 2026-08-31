"""Validation-tool tests for generate/replay probability comparisons."""

import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


MODULE_ROOT = Path(__file__).resolve().parents[2]
TOOL = MODULE_ROOT / "tools" / "validation" / "compare-fault-probabilities.py"
FIELDS = (
    "simulation_time_ns",
    "fault_id",
    "node_id",
    "task_id",
    "notice_time_ns",
    "risk_elapsed_time_ns",
    "task_compute_start_time_ns",
    "task_service_time_ns",
    "task_elapsed_time_ns",
    "remaining_compute_time_ns",
    "expected_compute_completion_time_ns",
    "completion_ratio",
    "f1_step_failure_probability",
    "f2_step_failure_probability",
    "combined_step_failure_probability",
    "horizon_step_count",
    "failure_before_finish_probability",
)


def make_row(simulation_time_ns, probability):
    return {
        "simulation_time_ns": simulation_time_ns,
        "fault_id": 1,
        "node_id": 3,
        "task_id": 7,
        "notice_time_ns": 1_000_000_000,
        "risk_elapsed_time_ns": simulation_time_ns - 1_000_000_000,
        "task_compute_start_time_ns": 0,
        "task_service_time_ns": 10_000_000_000,
        "task_elapsed_time_ns": simulation_time_ns,
        "remaining_compute_time_ns": 10_000_000_000 - simulation_time_ns,
        "expected_compute_completion_time_ns": 10_000_000_000,
        "completion_ratio": simulation_time_ns / 10_000_000_000,
        "f1_step_failure_probability": probability,
        "f2_step_failure_probability": 0.02,
        "combined_step_failure_probability": 1 - (1 - probability) * 0.98,
        "horizon_step_count": 11 - simulation_time_ns // 1_000_000_000,
        "failure_before_finish_probability": 0.4 + probability,
    }


def write_rows(path, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


class FaultProbabilityComparisonTest(unittest.TestCase):
    def run_tool(self, model_rows, prediction_rows, tolerance="1e-12"):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        model = root / "model.csv"
        prediction = root / "prediction.csv"
        detail = root / "detail.csv"
        summary = root / "summary.json"
        write_rows(model, model_rows)
        write_rows(prediction, prediction_rows)
        result = subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--model",
                str(model),
                "--prediction",
                str(prediction),
                "--detail",
                str(detail),
                "--summary",
                str(summary),
                "--absolute-tolerance",
                tolerance,
            ],
            cwd=MODULE_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        return result, detail, json.loads(summary.read_text(encoding="utf-8"))

    def test_matching_probabilities_report_zero_error(self):
        rows = [make_row(1_000_000_000, 0.1), make_row(2_000_000_000, 0.2)]
        result, detail, summary = self.run_tool(rows, rows)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(summary["within_tolerance"])
        self.assertEqual(summary["matched_record_count"], 2)
        self.assertEqual(summary["missing_model_record_count"], 0)
        self.assertEqual(summary["missing_prediction_record_count"], 0)
        self.assertEqual(summary["context_mismatch_count"], 0)
        self.assertTrue(
            all(
                values == {"mae": 0.0, "rmse": 0.0, "max_absolute_error": 0.0}
                for values in summary["probability_errors"].values()
            )
        )
        with detail.open(newline="", encoding="utf-8") as stream:
            self.assertEqual(len(list(csv.DictReader(stream))), 2)

    def test_missing_prediction_record_fails_audit(self):
        rows = [make_row(1_000_000_000, 0.1), make_row(2_000_000_000, 0.2)]
        result, _, summary = self.run_tool(rows, rows[:1])
        self.assertEqual(result.returncode, 1)
        self.assertFalse(summary["within_tolerance"])
        self.assertEqual(summary["missing_prediction_record_count"], 1)


if __name__ == "__main__":
    unittest.main()
