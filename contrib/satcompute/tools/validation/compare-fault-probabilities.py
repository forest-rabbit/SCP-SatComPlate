#!/usr/bin/env python3
"""Compare generate model probabilities with replay predictor probabilities."""

import argparse
import csv
import json
import math
from pathlib import Path
import sys


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
KEY_FIELDS = ("simulation_time_ns", "node_id", "task_id")
PROBABILITY_FIELDS = (
    "f1_step_failure_probability",
    "f2_step_failure_probability",
    "combined_step_failure_probability",
    "failure_before_finish_probability",
)
CONTEXT_FIELDS = tuple(
    field for field in FIELDS if field not in KEY_FIELDS + PROBABILITY_FIELDS
)


def nonnegative_float(value):
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("value must be finite and non-negative")
    return parsed


def read_records(path):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != FIELDS:
            raise ValueError(f"{path} has an unexpected probability schema")
        records = {}
        for line_number, row in enumerate(reader, start=2):
            key = tuple(int(row[field]) for field in KEY_FIELDS)
            if key in records:
                raise ValueError(f"{path}:{line_number} duplicates key {key}")
            for field in PROBABILITY_FIELDS:
                probability = float(row[field])
                if not math.isfinite(probability) or not 0.0 <= probability <= 1.0:
                    raise ValueError(
                        f"{path}:{line_number} has invalid {field}={row[field]}"
                    )
            records[key] = row
    return records


def error_summary(errors):
    if not errors:
        return {"mae": None, "rmse": None, "max_absolute_error": None}
    return {
        "mae": sum(errors) / len(errors),
        "rmse": math.sqrt(sum(error * error for error in errors) / len(errors)),
        "max_absolute_error": max(errors),
    }


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def compare(model_path, prediction_path, detail_path, summary_path, tolerance):
    model = read_records(model_path)
    prediction = read_records(prediction_path)
    model_keys = set(model)
    prediction_keys = set(prediction)
    matched_keys = sorted(model_keys & prediction_keys)
    only_model = sorted(model_keys - prediction_keys)
    only_prediction = sorted(prediction_keys - model_keys)
    errors = {field: [] for field in PROBABILITY_FIELDS}
    context_mismatch_count = 0

    detail_path.parent.mkdir(parents=True, exist_ok=True)
    with detail_path.open("w", encoding="utf-8", newline="") as stream:
        fieldnames = [*KEY_FIELDS, "context_match"]
        for field in PROBABILITY_FIELDS:
            fieldnames.extend(
                (f"model_{field}", f"prediction_{field}", f"absolute_error_{field}")
            )
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for key in matched_keys:
            model_row = model[key]
            prediction_row = prediction[key]
            context_match = all(
                model_row[field] == prediction_row[field] for field in CONTEXT_FIELDS
            )
            context_mismatch_count += 0 if context_match else 1
            detail = dict(zip(KEY_FIELDS, key))
            detail["context_match"] = str(context_match).lower()
            for field in PROBABILITY_FIELDS:
                model_probability = float(model_row[field])
                predicted_probability = float(prediction_row[field])
                absolute_error = abs(model_probability - predicted_probability)
                errors[field].append(absolute_error)
                detail[f"model_{field}"] = model_row[field]
                detail[f"prediction_{field}"] = prediction_row[field]
                detail[f"absolute_error_{field}"] = format(absolute_error, ".17g")
            writer.writerow(detail)

    probability_errors = {
        field: error_summary(field_errors) for field, field_errors in errors.items()
    }
    within_tolerance = (
        bool(matched_keys)
        and not only_model
        and not only_prediction
        and context_mismatch_count == 0
        and all(
            values["max_absolute_error"] is not None
            and values["max_absolute_error"] <= tolerance
            for values in probability_errors.values()
        )
    )
    summary = {
        "absolute_tolerance": tolerance,
        "model_record_count": len(model),
        "prediction_record_count": len(prediction),
        "matched_record_count": len(matched_keys),
        "missing_model_record_count": len(only_prediction),
        "missing_prediction_record_count": len(only_model),
        "context_mismatch_count": context_mismatch_count,
        "probability_errors": probability_errors,
        "within_tolerance": within_tolerance,
    }
    write_json(summary_path, summary)
    return summary


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Compare pre-sampling generate probabilities with replay predictor output"
        )
    )
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--prediction", required=True, type=Path)
    parser.add_argument("--detail", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument(
        "--absolute-tolerance",
        type=nonnegative_float,
        default=1e-12,
    )
    args = parser.parse_args()
    try:
        summary = compare(
            args.model,
            args.prediction,
            args.detail,
            args.summary,
            args.absolute_tolerance,
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    if not summary["within_tolerance"]:
        print(f"FAIL: probability audit differs: {summary}", file=sys.stderr)
        return 1
    print(
        "PASS: generate/replay probabilities agree "
        f"({summary['matched_record_count']} records)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
