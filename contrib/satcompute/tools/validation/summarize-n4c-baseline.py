#!/usr/bin/env python3
"""Audit final 800-task no-fault lifecycle/deadline and link ledgers."""

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import runpy

PRESSURE = runpy.run_path(str(Path(__file__).with_name("summarize-pressure-baseline.py")))
require = PRESSURE["require"]


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def distribution(values):
    if not values:
        return None
    return {"min": min(values), "mean": sum(values) / len(values),
            "median": PRESSURE["percentile"](values, .5),
            "p95": PRESSURE["percentile"](values, .95), "max": max(values)}


def population(tasks):
    timestamp_counts = {label: sum(int(t[field]) >= 0 for t in tasks) for label, field in (
        ("input_completed", "input_transfer_complete_time_ns"), ("queue_entered", "queue_enter_time_ns"),
        ("compute_started", "compute_start_time_ns"), ("compute_completed", "compute_complete_time_ns"),
        ("result_started", "result_transfer_start_time_ns"), ("result_delivered", "result_transfer_complete_time_ns"),
        ("deadline_established", "compute_deadline_time_ns"))}
    return {"task_count": len(tasks), **timestamp_counts,
            "final_states": dict(Counter(t["final_state"] for t in tasks)),
            "compute_on_time": sum(t["compute_deadline_met"] == "1" for t in tasks),
            "task_success": sum(t["task_success"] == "1" for t in tasks),
            "failed": sum(t["final_state"] == "FAILED" for t in tasks),
            "truncated": sum(t["final_state"] not in ("COMPLETED", "FAILED") for t in tasks),
            "deadline_exceeded": sum(t["failure_reason"] == "COMPUTE_DEADLINE_EXCEEDED" for t in tasks),
            "input_bytes": sum(int(t["input_bytes"]) for t in tasks),
            "output_bytes": sum(int(t["output_bytes"]) for t in tasks),
            "work_units": sum(int(t["compute_work_units"]) for t in tasks),
            "seconds": {field: distribution([int(t[field]) / 1e9 for t in tasks if int(t[field]) >= 0])
                        for field in ("input_transfer_delay_ns", "queue_delay_ns", "compute_service_time_ns",
                                      "compute_stage_elapsed_time_ns", "result_transfer_delay_ns",
                                      "end_to_end_completion_delay_ns")},
            "compute_slack_s": distribution([(int(t["compute_deadline_time_ns"]) -
                                                int(t["compute_complete_time_ns"])) / 1e9
                                               for t in tasks if t["compute_deadline_met"] == "1"])}


def summarize(directory):
    network = PRESSURE["summarize"](directory)
    tasks = rows(directory / "task-summary.csv")
    require(len(tasks) == 800, "requires formal C800")
    require(Counter(t["task_profile"] for t in tasks) ==
            {"dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80}, "class counts differ")
    overall = population(tasks)
    base_path = Path(__file__).resolve().parents[2] / "input/experiments/leo-66/workload/task-trace.json"
    expected = {t["task_id"]: t for t in json.loads(base_path.read_text())["tasks"]}
    require({int(t["task_id"]) for t in tasks} == set(expected), "final task IDs differ")
    for task in tasks:
        original = expected[int(task["task_id"])]
        require(all((task[k] if k == "task_profile" else int(task[k])) == v
                    for k, v in original.items()), "final task business or placement differs")
    run = json.loads((directory / "run-summary.json").read_text())
    require(run["simulation_duration_s"] == 1300, "final simulation horizon differs")
    require(overall["task_success"] == 800 and network["completed_transfers"] == 1600,
            "no-fault C800 did not fully complete")
    for task in tasks:
        start, end, deadline = (int(task[k]) for k in
                                ("compute_start_time_ns", "compute_complete_time_ns", "compute_deadline_time_ns"))
        baseline = (int(task["compute_work_units"]) * 10**9 + 99999) // 100000
        require(int(task["compute_rate_work_units_per_second"]) == 100000, "compute rate differs")
        require(int(task["baseline_compute_time_ns"]) == baseline and end - start == baseline,
                "service time differs from G1")
        budget = (baseline * 13 + 9) // 10
        require(int(task["compute_deadline_budget_ns"]) == budget and deadline == start + budget,
                "deadline is not first-compute-start plus 1.3 baseline")
        require(task["compute_deadline_met"] == "1" and task["result_delivered"] == "1",
                "success lacks on-time compute or complete RESULT")
    nodes = rows(directory / "compute-node-summary.csv")
    require(sum(int(n["total_work_units"]) for n in nodes) == overall["work_units"], "node WU ledger differs")
    require(sum(int(n["busy_time_ns"]) for n in nodes) ==
            sum(int(t["compute_service_time_ns"]) for t in tasks), "node busy-time ledger differs")
    require(network["application_bytes"] == overall["input_bytes"] + overall["output_bytes"],
            "application byte ledger differs")
    require(network["lost_packets"] == 0 and network["link_queue_drops"] == 0, "no-fault packet loss")
    capacity = json.loads((directory / "capacity-aware-summary.json").read_text())
    require(not any(capacity.values()), "capacity reservation ledger did not drain")
    for link in rows(directory / "link-summary.csv"):
        require(float(link["peak_reserved_rate_bps"]) <= 10_000_000_000, "reservation exceeds capacity")
    windows = rows(directory / "link-window-metrics.csv")
    last = max(float(w["window_end_s"]) for w in windows)
    require(all(float(w["mean_reserved_rate_bps"]) == 0 and int(w["max_queue_bytes"]) == 0
                for w in windows if float(w["window_end_s"]) == last), "terminal link ledger did not drain")
    network["link_window_utilization_percent"] = distribution([float(w["utilization_percent"]) for w in windows])
    network["link_mean_utilization_percent"] = distribution([
        float(w["utilization_percent"]) for w in rows(directory / "link-summary.csv")])
    return {"overall": overall,
            "by_profile": {p: population([t for t in tasks if t["task_profile"] == p])
                           for p in sorted({t["task_profile"] for t in tasks})},
            "network": network, "nodes": nodes,
            "audit": "C800 business, deadline, task/WU/byte and terminal link ledgers passed"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.run_dir)
    (args.run_dir / "n4c-summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"overall": result["overall"], "network": result["network"]}, indent=2))


if __name__ == "__main__":
    main()
