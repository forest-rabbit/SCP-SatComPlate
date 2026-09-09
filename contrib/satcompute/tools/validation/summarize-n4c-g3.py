#!/usr/bin/env python3
"""Audit C800 G3 actual event/task/impact ledgers; never generates or selects faults."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import statistics

PLATFORM = Path(__file__).resolve().parents[2]


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    def quantile(p):
        index = (len(values) - 1) * p
        lo = int(index)
        return values[lo] + (values[min(lo+1, len(values)-1)] - values[lo]) * (index-lo)
    return {"count": len(values), "mean": statistics.mean(values),
            "sd": statistics.stdev(values) if len(values) > 1 else 0,
            "min": values[0], "p10": quantile(.1), "p50": quantile(.5),
            "p90": quantile(.9), "max": values[-1]}


def summarize(directory, manifest, expect_f3=False, none_directory=None):
    tasks = rows(directory / "task-summary.csv")
    by_id = {int(t["task_id"]): t for t in tasks}
    base = json.loads((PLATFORM / "input/examples/leo-66-1000s-n4c/task-trace.json").read_text())["tasks"]
    require(len(tasks) == len(by_id) == 800, "C800 task ledger incomplete")
    for original in base:
        actual = by_id[original["task_id"]]
        for field in ("input_bytes", "output_bytes", "compute_work_units", "arrival_time_ns"):
            require(int(actual[field]) == original[field], f"task business changed: {field}")
        require(actual["task_profile"] == original["task_profile"], "task profile changed")
        require(int(actual["compute_rate_work_units_per_second"]) == 100000, "compute rate changed")
        success = actual["compute_deadline_met"] == "1" and actual["result_delivered"] == "1"
        require(success == (actual["task_success"] == "1") == (actual["final_state"] == "COMPLETED"),
                "success/deadline/RESULT contract mismatch")
    placements = {p["task_id"]: p for p in manifest["placements"]}
    f3_plan = manifest["f3"]
    ordinary = by_id[f3_plan["ordinary_task_id"]]
    require(int(ordinary["source_node_id"]) == f3_plan["node_id"] and
            0 <= int(ordinary["arrival_time_ns"]) < int(ordinary["input_transfer_complete_time_ns"]) < f3_plan["time_ns"] and
            int(ordinary["compute_node_id"]) != f3_plan["node_id"] and
            int(ordinary["result_node_id"]) != f3_plan["node_id"],
            "ordinary pre-F3 source was not actually used and released before F3")
    ordinary_evidence = {"task_id": int(ordinary["task_id"]), "role": "source",
        "arrival_time_ns": int(ordinary["arrival_time_ns"]),
        "endpoint_release_time_ns": int(ordinary["input_transfer_complete_time_ns"]),
        "fault_time_ns": f3_plan["time_ns"]}
    events = rows(directory / "fault-events.csv") if (directory / "fault-events.csv").exists() else []
    starts = {int(e["fault_id"]): e for e in events if e["event_type"] == "START"}
    impacts = rows(directory / "fault-task-impact.csv") if events else []
    seen = set()
    direct = defaultdict(set)
    for row in impacts:
        key = (int(row["fault_id"]), int(row["task_id"]), row["impact_type"])
        require(key not in seen, "duplicate fault/task/impact row")
        seen.add(key)
        start = starts[key[0]]
        task = by_id[key[1]]
        require(row["fault_type"] == start["fault_source"], "impact source differs from actual START")
        require(int(row["fault_time_ns"]) == int(start["simulation_time_ns"]) <= int(row["impact_time_ns"]),
                "fault/impact time mismatch")
        require(row["fault_node_id"] == start["node_id"], "impact node differs")
        require(row["task_profile"] == task["task_profile"], "impact task profile differs")
        for key_field in ("input_bytes", "output_bytes", "compute_work_units", "baseline_compute_time_ns"):
            require(row[key_field] == task[key_field], f"impact task metadata differs: {key_field}")
        final = task["final_state"] if task["final_state"] in ("COMPLETED", "FAILED") else "TRUNCATED"
        require(row["final_task_state"] == final and row["final_failure_reason"] == task["failure_reason"],
                "impact final outcome differs")
        if row["progress_valid"] == "1":
            total = int(task["compute_work_units"])
            expected = min(total, (int(row["impact_time_ns"]) - int(task["compute_start_time_ns"])) * 100000 // 10**9)
            require(row["task_state_before_impact"] == "RUNNING" and
                    int(row["completed_work_units_at_fault"]) == expected and
                    int(row["remaining_work_units_at_fault"]) == total - expected and
                    abs(float(row["compute_progress_at_fault"]) - expected/total) < 1e-14,
                    "WU progress snapshot mismatch")
            require(row["compute_deadline_time_ns"] == task["compute_deadline_time_ns"] and
                    int(row["deadline_slack_at_fault_ns"]) == int(task["compute_deadline_time_ns"]) - int(row["fault_time_ns"]),
                    "fault-time deadline differs")
        else:
            require(all(row[k] == "-1" for k in ("completed_work_units_at_fault",
                                                 "remaining_work_units_at_fault", "compute_progress_at_fault")),
                    "non-running row fabricated progress")
        if row["impact_type"] in ("RUNNING_INTERRUPTED", "RUNNING_INTERRUPTED_PERMANENT"):
            require(row["progress_valid"] == "1" and row["task_state_before_fault"] == "RUNNING",
                    "direct interruption lacks a running snapshot")
            for source in row["fault_type"].split("+"):
                direct[source].add(int(row["task_id"]))
    compute_direct = direct["F1"] | direct["F2"]
    all_direct = compute_direct | direct["F3"]
    for fault_id, start in starts.items():
        actual_direct = {r["task_id"] for r in impacts if int(r["fault_id"]) == fault_id and
            r["impact_type"] == "RUNNING_INTERRUPTED"}
        if start["fault_type"] == "compute":
            require(len(actual_direct) == int(start["affected_task_count"]) <= 1,
                    "compute START count differs from its direct task impacts")
            require(start["route_recomputed"] == "false" and
                    start["communication_available_after"] == "true" and
                    start["satellite_available_after"] == "true" and
                    start["compute_available_after"] == "false", "compute outage changed communication")
    require(all(t["failure_reason"] != "COMPUTE_NODE_FAILURE" or int(t["task_id"]) in compute_direct
                for t in tasks), "compute-only failure without RUNNING interruption")
    f3_events = [e for e in starts.values() if e["fault_source"] == "F3"]
    victim_evidence = None
    if expect_f3:
        f3 = manifest["f3"]
        require(len(f3_events) == 1 and direct["F3"] == {f3["victim_task_id"]}, "F3 single victim failed")
        event = f3_events[0]
        require((int(event["node_id"]), int(event["simulation_time_ns"]), int(event["affected_task_count"])) ==
                (f3["node_id"], f3["time_ns"], 1), "controlled F3 target/time/direct count differs")
        require(event["route_recomputed"] == "true" and all(event[k] == "false" for k in (
            "satellite_available_after", "communication_available_after", "compute_available_after")),
            "F3 did not immediately disable the satellite and recompute routes")
        task = by_id[f3["victim_task_id"]]
        t0 = int(task["compute_start_time_ns"])
        require(t0 >= 0 and t0 < f3["time_ns"] < t0 + int(task["baseline_compute_time_ns"]),
                "F3 victim was not running inside its compute interval")
        require(len([i for i in impacts if i["fault_id"] == event["fault_id"]]) == 1,
                "F3 has extra active or post-fault victims")
        require(not compute_direct.intersection(direct["F3"]), "F3 victim already failed from F1/F2")
        for task_row in tasks:
            if int(task_row["arrival_time_ns"]) >= f3["time_ns"]:
                require(all(int(task_row[k]) != f3["node_id"] for k in (
                    "source_node_id", "compute_node_id", "result_node_id")), "post-F3 static endpoint uses failed node")
        victim_evidence = {**f3, "compute_start_time_ns": t0,
                           "no_failure_compute_finish_time_ns": t0 + int(task["baseline_compute_time_ns"]),
                           "progress": next(float(i["compute_progress_at_fault"]) for i in impacts
                                            if i["fault_id"] == event["fault_id"])}
    elif events:
        require(not f3_events, "unexpected F3 during F1/F2 pre-calibration")
    node_rows = rows(directory / "compute-node-summary.csv")
    require(sum(int(n["busy_time_ns"]) for n in node_rows) ==
            sum(max(0, int(t["compute_stage_elapsed_time_ns"])) for t in tasks), "busy-time ledger differs")
    region_runtime = {}
    for region in sorted({p["region"] for p in placements.values()}):
        subset = [t for t in tasks if placements[int(t["task_id"])]["region"] == region]
        region_runtime[region] = {"task_count": len(subset),
            "demand_s": sum(int(t["compute_work_units"]) for t in subset)/100000,
            "actual_busy_s": sum(max(0, int(t["compute_stage_elapsed_time_ns"])) for t in subset)/1e9,
            "queue_s": distribution([int(t["queue_delay_ns"])/1e9 for t in subset if int(t["queue_delay_ns"]) >= 0])}
    state_path = directory / "fault-model-state.csv"
    state_summary = {}
    if state_path.exists():
        state_rows = rows(state_path)
        states = {(int(s["simulation_time_ns"]), int(s["node_id"])): s for s in state_rows}
        for event in starts.values():
            if "F2" in event["fault_source"]:
                state = states[(int(event["simulation_time_ns"]), int(event["node_id"]))]
                require(state["in_saa"] == "1" and float(state["p_f2"]) > 0, "F2 event outside SAA")
            if "F1" in event["fault_source"]:
                state = states[(int(event["simulation_time_ns"]), int(event["node_id"]))]
                require(float(state["p_f1"]) > 0, "F1 event without model probability")
        for n in node_rows:
            subset = [s for s in state_rows if s["node_id"] == n["node_id"]]
            if subset:
                state_summary[n["node_id"]] = {"temperature_c": distribution([float(s["temperature_c"]) for s in subset]),
                    "p_f1": distribution([float(s["p_f1"]) for s in subset]),
                    "f1_risk": distribution([float(s["f1_risk"]) for s in subset])}
    source_counts = Counter(e["fault_source"] for e in starts.values())
    trace = json.loads((directory / "fault-trace.json").read_text())["faults"] if events else []
    require(all(f["fault_occurred"] for f in trace), "risk-only trace record survived")
    require(all(e["event_type"] in ("START", "RECOVERY") for e in events), "notification event survived")
    source_events = {"F1": source_counts["F1"] + source_counts["F1+F2"],
                     "F2": source_counts["F2"] + source_counts["F1+F2"],
                     "F1+F2_merged": source_counts["F1+F2"], "F3": source_counts["F3"],
                     "compute_outages": sum(e["fault_type"] == "compute" for e in starts.values())}
    by_source = {}
    for source in ("F1", "F2", "F3"):
        selected = [r for r in impacts if int(r["task_id"]) in direct[source] and
                    source in r["fault_type"].split("+") and r["progress_valid"] == "1"]
        by_source[source] = {"profiles": dict(Counter(r["task_profile"] for r in selected)),
            "input_bytes": distribution([int(r["input_bytes"]) for r in selected]),
            "work_units": distribution([int(r["compute_work_units"]) for r in selected]),
            "progress": distribution([float(r["compute_progress_at_fault"]) for r in selected]),
            "deadline_slack_s": distribution([int(r["deadline_slack_at_fault_ns"])/1e9 for r in selected]),
            "regions": dict(Counter(placements[int(r["task_id"])]["region"] for r in selected)),
            "outcomes": dict(Counter(r["final_task_state"] for r in selected))}
    f1_starts = [e for e in starts.values() if "F1" in e["fault_source"]]
    previous_by_node, intervals = {}, []
    for event in sorted(f1_starts, key=lambda e: int(e["simulation_time_ns"])):
        node, time = int(event["node_id"]), int(event["simulation_time_ns"])
        if node in previous_by_node:
            intervals.append((time - previous_by_node[node]) / 1e9)
        previous_by_node[node] = time
    f1_start_distributions = {
        field: distribution([float(e[field]) for e in f1_starts])
        for field in ("temperature_c", "p_f1", "continuous_busy_s")}
    f1_start_distributions["outage_duration_s"] = distribution([int(e["duration_ns"])/1e9 for e in f1_starts])
    f1_start_distributions["same_node_start_interval_s"] = distribution(intervals)
    for event in f1_starts:
        require(20 < float(event["temperature_c"]) <= 30 and 0 < float(event["p_f1"]) <= 1,
                "F1 START probability/temperature outside new contract")
        expected = (float(event["temperature_c"]) - 17) / 3.25
        if "F2" in event["fault_source"]:
            expected = max(expected, 8)
        actual = int(event["duration_ns"]) / 1e9
        preempted = any(f["node_id"] == int(event["node_id"]) and f["fault_type"] == "satellite" and
                       f["start_time_ns"] == int(event["simulation_time_ns"]) + int(event["duration_ns"]) for f in trace)
        require(abs(actual - expected) < 2e-9 or (preempted and actual < expected),
                "F1 dynamic recovery or F1+F2 maximum duration differs")
    queue_delta = None
    if none_directory:
        none = {r["task_id"]: r for r in rows(none_directory / "task-summary.csv")}
        queue_delta = distribution([(int(r["queue_delay_ns"]) - int(none[r["task_id"]]["queue_delay_ns"]))/1e9
                                    for r in tasks if int(r["queue_delay_ns"]) >= 0])
    require(not any(json.loads((directory / "capacity-aware-summary.json").read_text()).values()),
            "capacity ledger did not drain")
    transfers = rows(directory / "transfer-summary.csv")
    require(len(transfers) == 1600, "transfer ledger incomplete")
    run = json.loads((directory / "run-summary.json").read_text())
    links = rows(directory / "link-summary.csv")
    windows = rows(directory / "link-window-metrics.csv")
    last = max(float(w["window_end_s"]) for w in windows)
    require(last == 1000 and all(float(w["mean_reserved_rate_bps"]) == 0 and
            int(w["max_queue_bytes"]) == 0 for w in windows if float(w["window_end_s"]) == last),
            "terminal link ledger did not drain")
    require(all(float(link["peak_reserved_rate_bps"]) <= 10_000_000_000 for link in links),
            "reservation exceeds frozen link capacity")
    network_windows = rows(directory / "network-link-window-metrics.csv")
    network = {"flow_monitor_lost_packets": run["flow_monitor_lost_packets"],
        "route_recomputations_due_to_fault": sum(e["route_recomputed"] == "true" for e in events),
        "link_queue_drops": sum(int(link["drop_packets"]) for link in links),
        "mean_available_utilization_percent": 100 * sum(float(w["available_busy_time_s"]) for w in network_windows) /
            sum(float(w["available_link_time_s"]) for w in network_windows),
        "terminal_link_ledger_drained": True}
    return {"events": source_events, "unique_direct_running": {
                "F1": len(direct["F1"]), "F2": len(direct["F2"]), "F1_union_F2": len(compute_direct),
                "F3": len(direct["F3"]), "total": len(all_direct)},
            "impact_types": dict(Counter(r["impact_type"] for r in impacts)),
            "unique_tasks_by_impact_type": {kind: len({r["task_id"] for r in impacts if r["impact_type"] == kind})
                for kind in sorted({r["impact_type"] for r in impacts})},
            "unique_observed_impacted_tasks": len({r["task_id"] for r in impacts}),
            "indirect_unique_tasks": len({r["task_id"] for r in impacts if r["progress_valid"] == "0"}),
            "task_states": dict(Counter(t["final_state"] for t in tasks)),
            "failure_reasons": dict(Counter(t["failure_reason"] for t in tasks if t["failure_reason"])),
            "transfers": dict(Counter(t["terminal_state"] for t in transfers)),
            "network": network,
            "region_runtime": region_runtime, "node_state_audit": state_summary,
            "by_fault_source": by_source, "f3_victim": victim_evidence,
            "f3_pre_failure_participation": ordinary_evidence, "f1_start_distributions": f1_start_distributions,
            "queue_delta_vs_none_s": queue_delta, "node_busy_s": distribution([int(n["busy_time_ns"])/1e9 for n in node_rows]),
            "audit": "business, actual START, per-task impact, WU progress, deadline and terminal ledgers matched"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--expect-f3", action="store_true")
    parser.add_argument("--none-dir", type=Path)
    args = parser.parse_args()
    result = summarize(args.run_dir, json.loads(args.manifest.read_text()), args.expect_f3, args.none_dir)
    (args.run_dir / "g3-summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("events", "unique_direct_running", "task_states", "failure_reasons", "f3_victim")}, indent=2))


if __name__ == "__main__":
    main()
