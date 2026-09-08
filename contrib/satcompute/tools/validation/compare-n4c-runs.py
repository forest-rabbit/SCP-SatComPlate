#!/usr/bin/env python3
"""Compare deterministic C800 business outputs, excluding optional probability audits."""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path

CORE = (
    "task-summary.csv", "task-events.csv", "compute-node-summary.csv",
    "transfer-summary.csv", "network-flow-metrics.csv", "network-flow-details.csv",
    "ecmp-route-events.csv", "size-aware-reservation-events.csv", "size-aware-summary.json",
    "capacity-aware-summary.json", "link-window-metrics.csv", "link-summary.csv",
    "network-link-window-metrics.csv",
)
FAULT = ("fault-events.csv", "fault-summary.json", "fault-trace.json", "fault-task-impact.csv")
AUDIT = ("fault-model-probabilities.csv", "fault-predictions.csv", "fault-prediction-summary.json")


def compare(left, right):
    compared = []
    for filename in CORE + FAULT:
        a, b = left / filename, right / filename
        if filename in FAULT and not a.exists() and not b.exists():
            continue
        if not a.is_file() or not b.is_file() or a.read_bytes() != b.read_bytes():
            raise ValueError(f"missing or unequal business output: {filename}")
        compared.append(filename)
    normalized = []
    for directory in (left, right):
        summary = json.loads((directory / "run-summary.json").read_text())
        for field in ("wall_clock_ns", "wall_clock_s"):
            summary.pop(field, None)
        normalized.append(summary)
    if normalized[0] != normalized[1]:
        raise ValueError("normalized run-summary differs")
    with (left / "task-summary.csv").open() as stream:
        tasks = list(csv.DictReader(stream))
    with (left / "transfer-summary.csv").open() as stream:
        transfers = list(csv.DictReader(stream))
    if len(tasks) != 800 or len(transfers) != 1600:
        raise ValueError("comparison requires the complete C800 task/transfer ledger")
    for task in tasks:
        success = task["compute_deadline_met"] == "1" and task["result_delivered"] == "1"
        if (task["task_success"] == "1") != success or (task["final_state"] == "COMPLETED") != success:
            raise ValueError("task success disagrees with deadline/RESULT evidence")
    return {"equal": True, "compared_files": compared + ["run-summary.json (excluding wall clock)"],
            "task_states": dict(Counter(t["final_state"] for t in tasks)),
            "task_failure_reasons": dict(Counter(t["failure_reason"] for t in tasks if t["failure_reason"])),
            "transfer_states": dict(Counter(t["terminal_state"] for t in transfers)),
            "audit_files": {str(d): [name for name in AUDIT if (d / name).is_file()] for d in (left, right)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--left", required=True, type=Path)
    parser.add_argument("--right", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = compare(args.left, args.right)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
