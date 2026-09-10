"""Shared generated-fault invariants; no fault quotas or seed-dependent F1 goldens."""
import csv
import json
import math
from pathlib import Path


def rows(path):
    with Path(path).open() as stream:
        return list(csv.DictReader(stream))


def require(value, message):
    if not value:
        raise AssertionError(message)


def audit(directory, task_count=None, allow_truncated=False):
    directory = Path(directory)
    events = rows(directory / "fault-events.csv")
    faults = json.loads((directory / "fault-trace.json").read_text())["faults"]
    starts = {int(r["fault_id"]): r for r in events if r["event_type"] == "START"}
    require(len(starts) == len(faults), "START/trace count differs")
    require(all(r["event_type"] in ("START", "RECOVERY") for r in events), "notification event survived")
    require(all(f["fault_occurred"] and not any(k in f for k in
                ("notice_time_ns", "risk_duration_ns", "warning_lead_time_ns")) for f in faults),
            "obsolete risk-only metadata survived")
    permanent = {f["node_id"]: f["start_time_ns"] for f in faults if f["fault_type"] == "satellite"}
    by_node = {}
    for fault in faults:
        start = starts[fault["fault_id"]]
        require(int(start["simulation_time_ns"]) == fault["start_time_ns"], "START timestamp differs")
        old_end = by_node.get(fault["node_id"], -1)
        require(old_end <= fault["start_time_ns"], "overlapping generated outages")
        by_node[fault["node_id"]] = (fault["start_time_ns"] + fault["duration_ns"]
                                     if fault["duration_ns"] is not None else math.inf)
        if fault["fault_type"] == "satellite":
            require(fault["duration_ns"] is None and fault["failure_probability"] is None,
                    "F3 is not permanent or has a compute probability")
            require(all(start[k] == "false" for k in ("satellite_available_after",
                    "communication_available_after", "compute_available_after")), "F3 availability differs")
            continue
        require(fault["f1_occurred"] or fault["f2_occurred"], "compute event has no source")
        p1, p2, pc = fault["p_f1"], fault["p_f2"], fault["failure_probability"]
        require(all(0 <= p <= 1 for p in (p1, p2, pc)) and
                abs(pc - (1 - (1-p1)*(1-p2))) < 1e-14, "START probability differs")
        require(not fault["f1_occurred"] or p1 > 0, "F1 hit with zero probability")
        require(not fault["f2_occurred"] or p2 > 0, "F2 hit with zero probability")
        require(start["communication_available_after"] == "true" and
                start["satellite_available_after"] == "true" and
                start["compute_available_after"] == "false" and
                start["route_recomputed"] == "false", "compute-only fault changed networking")
        if fault["f1_occurred"]:
            temperature = fault["temperature_c"]
            require(20 < temperature <= 30, "F1 temperature outside thermal range")
            expected = math.ceil((temperature - 17) / 3.25 * 1e9)
        else:
            expected = 0
        if fault["f2_occurred"]:
            expected = max(expected, 8_000_000_000)
        actual = fault["duration_ns"]
        f3_preempted = permanent.get(fault["node_id"]) == fault["start_time_ns"] + actual
        require(actual > 0 and (abs(actual - expected) <= 1 or
                (actual < expected and f3_preempted)), "dynamic recovery/max duration differs")
    tasks_path = directory / "task-summary.csv"
    tasks = rows(tasks_path) if tasks_path.exists() else []
    if task_count is not None:
        require(len(tasks) == task_count, "task count differs")
    if not allow_truncated:
        require(all(t["final_state"] in ("COMPLETED", "FAILED") for t in tasks), "task truncated")
    by_task = {int(t["task_id"]): t for t in tasks}
    impacts_path = directory / "fault-task-impact.csv"
    impacts = rows(impacts_path) if impacts_path.exists() else []
    direct = set()
    for row in impacts:
        task = by_task[int(row["task_id"])]
        require(row["fault_type"] == starts[int(row["fault_id"])]["fault_source"], "impact source differs")
        if row["progress_valid"] == "1":
            total = int(task["compute_work_units"])
            expected = min(total, (int(row["impact_time_ns"])-int(task["compute_start_time_ns"])) *
                           int(task["compute_rate_work_units_per_second"]) // 10**9)
            require(int(row["completed_work_units_at_fault"]) == expected and
                    int(row["remaining_work_units_at_fault"]) == total-expected,
                    "interrupted WU ledger differs")
            direct.add(int(row["task_id"]))
        else:
            require(row["compute_progress_at_fault"] == "-1", "non-running progress fabricated")
    require(all(t["failure_reason"] != "COMPUTE_NODE_FAILURE" or int(t["task_id"]) in direct
                for t in tasks), "queued task failed from compute-only outage")
    model_path = directory / "fault-model-probabilities.csv"
    if model_path.exists():
        model, predictions = rows(model_path), rows(directory / "fault-predictions.csv")
        require(model == predictions, "model and independent prediction records differ")
        summary = json.loads((directory / "fault-prediction-summary.json").read_text())
        require(summary == {"prediction_count": len(model),
                            "task_count": len({r["task_id"] for r in model})}, "prediction summary differs")
        for row in model:
            task = by_task[int(row["task_id"])]
            time = int(row["simulation_time_ns"])
            require(int(task["compute_start_time_ns"]) <= time and
                    (int(task["failure_time_ns"]) < 0 or time <= int(task["failure_time_ns"])),
                    "prediction outside live task lifecycle")
            elapsed, remaining = int(row["task_elapsed_time_ns"]), int(row["remaining_compute_time_ns"])
            require(elapsed + remaining == int(row["task_service_time_ns"]) and
                    int(row["expected_compute_completion_time_ns"]) == time + remaining,
                    "prediction progress/horizon differs")
            q = float(row["combined_step_failure_probability"])
            p = float(row["failure_before_finish_probability"])
            # The log-survival round trip can differ by one ULP for a one-step horizon.
            require(0 <= q <= 1 and 0 <= p <= 1 and q <= p + 1e-12,
                    "invalid forecast probabilities")
    return {"faults": faults, "events": events, "tasks": tasks, "impacts": impacts}
