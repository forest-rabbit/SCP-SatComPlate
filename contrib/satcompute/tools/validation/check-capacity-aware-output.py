#!/usr/bin/env python3
"""Validate the deterministic capacity-aware bottleneck comparison."""

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def require(condition, message):
    if not condition:
        fail(message)


def read_json(directory, filename):
    path = Path(directory) / filename
    require(path.is_file(), f"missing {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def read_rows(directory, filename):
    path = Path(directory) / filename
    require(path.is_file(), f"missing {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def int_field(row, name):
    try:
        return int(row[name])
    except (KeyError, ValueError) as error:
        fail(f"invalid integer field {name}: {error}")


def rows_by_transfer(directory, filename):
    rows = read_rows(directory, filename)
    result = {int_field(row, "transfer_id"): row for row in rows}
    require(len(result) == len(rows), f"duplicate transfer in {filename}")
    return result


def validate_baseline(directory):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == "global-size-aware-hrw", "baseline mode mismatch")
    require(run["pacing_mode"] == "first-hop-serialization", "baseline pacing mismatch")
    require(run["transfer_count"] == 2, "baseline transfer count mismatch")
    require(run["declared_application_bytes"] == 24_000_000, "baseline bytes changed")
    require(run["flow_monitor_lost_packets"] > 0, "baseline no longer drops packets")
    require(
        run["flow_monitor_reported_drop_packets"]
        == run["flow_monitor_lost_packets"],
        "baseline losses are not fully attributed",
    )
    reasons = {
        row["reason_name"]: row["dropped_packets"]
        for row in run["flow_monitor_drop_reasons"]
    }
    require(reasons["QUEUE_DISC"] > 0, "baseline lacks QueueDisc drops")
    require(run["received_application_bytes"] < 24_000_000, "baseline unexpectedly completed")

    transfers = rows_by_transfer(directory, "transfer-summary.csv")
    require(set(transfers) == {1, 2}, "baseline transfer IDs changed")
    require(
        all(int_field(row, "completion_time_ns") == -1 for row in transfers.values()),
        "baseline bottleneck transfer unexpectedly completed",
    )

    drop_rows = read_rows(directory, "diagnostics/failure/flow-drop-reasons.csv")
    require(
        any(row["reason_name"] == "QUEUE_DISC" for row in drop_rows),
        "baseline failure diagnostics omit QueueDisc",
    )


def validate_capacity(directory):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == "global-capacity-aware-hrw", "capacity mode mismatch")
    require(
        run["pacing_mode"] == "path-bottleneck-serialization",
        "capacity pacing mismatch",
    )
    require(run["transfer_count"] == 2, "capacity transfer count mismatch")
    require(run["declared_application_bytes"] == 24_000_000, "capacity declared bytes changed")
    require(run["sent_application_bytes"] == 24_000_000, "capacity sent bytes mismatch")
    require(run["received_application_bytes"] == 24_000_000, "capacity received bytes mismatch")
    require(run["derived_udp_packets"] == 17_144, "capacity packet count changed")
    require(run["flow_monitor_tx_packets"] == 17_144, "capacity tx packets changed")
    require(run["flow_monitor_rx_packets"] == 17_144, "capacity rx packets changed")
    require(run["flow_monitor_lost_packets"] == 0, "capacity run lost packets")
    require(run["flow_monitor_reported_drop_packets"] == 0, "capacity run reports drops")
    require(
        run["flow_monitor_unattributed_lost_packets"] == 0,
        "capacity run has unattributed loss",
    )
    require(run["udp_socket_drop_packets"] == 0, "capacity run has UDP socket drops")

    transfers = rows_by_transfer(directory, "transfer-summary.csv")
    flows = rows_by_transfer(directory, "network-flow-details.csv")
    require(set(transfers) == {1, 2} == set(flows), "capacity transfer IDs changed")
    for transfer_id in (1, 2):
        transfer = transfers[transfer_id]
        flow = flows[transfer_id]
        require(
            transfer["pacing_mode"] == "path-bottleneck-serialization",
            f"transfer {transfer_id} pacing mismatch",
        )
        require(
            int_field(transfer, "received_application_bytes") == 12_000_000,
            f"transfer {transfer_id} did not complete",
        )
        require(int_field(transfer, "completion_time_ns") > 0, "missing completion time")
        require(
            int_field(flow, "tx_packets") == int_field(flow, "rx_packets"),
            "flow packet mismatch",
        )
        require(int_field(flow, "lost_packets") == 0, "capacity flow lost packets")

    first_start = int_field(flows[1], "time_first_tx_ns")
    second_start = int_field(flows[2], "time_first_tx_ns")
    first_completion = int_field(transfers[1], "completion_time_ns")
    require(first_start == 100_000_000, "first flow no longer starts at arrival")
    require(
        second_start == first_completion and second_start > first_start,
        "second flow was not held until bottleneck capacity was released",
    )

    events = read_rows(directory, "size-aware-reservation-events.csv")
    actions = Counter(row["action"] for row in events)
    require(actions["ASSIGN"] == 2, "capacity assignment count mismatch")
    require(actions["STICKY_REUSE"] == 2, "capacity sticky count mismatch")
    require(actions["RELEASE_TRANSFER_COMPLETED"] == 2, "capacity release count mismatch")
    require(actions["RELEASE_CANDIDATE_INVALID"] == 0, "capacity path became invalid")
    require(actions["RELEASE_SENDER_FINISHED"] == 0, "capacity path released at sender finish")
    require(
        all(
            row["selection_reason"] == "CAPACITY_AWARE_PATH"
            for row in events
            if row["action"] == "ASSIGN"
        ),
        "capacity assignment reason mismatch",
    )
    summary = read_json(directory, "size-aware-summary.json")
    require(summary["active_flow_count_at_end"] == 0, "capacity active flow remains")
    require(summary["assignment_count_at_end"] == 0, "capacity assignment remains")
    require(summary["final_total_reserved_bytes"] == 0, "capacity bytes remain reserved")
    require(
        summary["transfer_completed_release_event_count"] == 2,
        "capacity completion release summary mismatch",
    )

    drop_rows = read_rows(directory, "diagnostics/failure/flow-drop-reasons.csv")
    require(not drop_rows, "capacity run produced failure drop rows")


def validate_parallel_ecmp(directory):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == "global-capacity-aware-hrw", "parallel mode mismatch")
    require(run["transfer_count"] == 8, "parallel transfer count mismatch")
    require(run["received_application_bytes"] == 3_600_000, "parallel bytes mismatch")
    require(run["flow_monitor_tx_packets"] == 60, "parallel tx packets changed")
    require(run["flow_monitor_rx_packets"] == 60, "parallel rx packets changed")
    require(run["flow_monitor_lost_packets"] == 0, "parallel ECMP run lost packets")

    flows = rows_by_transfer(directory, "network-flow-details.csv")
    require(set(flows) == set(range(1, 9)), "parallel transfer IDs changed")
    require(
        int_field(flows[1], "time_first_tx_ns") == 100_000_000
        and int_field(flows[2], "time_first_tx_ns") == 100_000_000,
        "two disjoint ECMP paths did not start concurrently",
    )

    route_events = read_rows(directory, "ecmp-route-events.csv")
    first_pair = [
        row
        for row in route_events
        if int_field(row, "node_id") == 0
        and int_field(row, "source_port") in {10_000, 10_001}
    ]
    require(len(first_pair) == 2, "missing first-pair source route decisions")
    require(
        {int_field(row, "candidate_count_after_dedup") for row in first_pair} == {2},
        "parallel fixture no longer exposes two ECMP candidates",
    )
    require(
        len({int_field(row, "selected_output_interface") for row in first_pair}) == 2,
        "capacity-aware routing did not use both free ECMP paths",
    )
    require(
        {row["selection_reason"] for row in first_pair}
        == {"CAPACITY_AWARE_STICKY"},
        "parallel path pinning reason mismatch",
    )

    events = read_rows(directory, "size-aware-reservation-events.csv")
    require(
        not any(row["action"] == "RELEASE_CANDIDATE_INVALID" for row in events),
        "parallel capacity path became invalid",
    )
    summary = read_json(directory, "size-aware-summary.json")
    require(summary["active_flow_count_at_end"] == 0, "parallel active flow remains")
    require(summary["assignment_count_at_end"] == 0, "parallel assignment remains")


def validate_task_mode(directory):
    run = read_json(directory, "run-summary.json")
    require(run["mode"] == "task", "capacity task mode mismatch")
    require(run["routing_mode"] == "global-capacity-aware-hrw", "capacity task routing mismatch")
    require(run["pacing_mode"] == "path-bottleneck-serialization", "capacity task pacing mismatch")
    require(run["task_count"] == 1, "capacity task count mismatch")
    require(run["completed_task_count"] == 1, "capacity task did not complete")
    require(run["transfer_count"] == 2, "capacity task transfer count mismatch")
    require(run["flow_monitor_lost_packets"] == 0, "capacity task lost packets")
    task_rows = read_rows(directory, "task-summary.csv")
    require(len(task_rows) == 1, "capacity task summary count mismatch")
    require(task_rows[0]["final_state"] == "COMPLETED", "capacity task final state mismatch")
    transfers = read_rows(directory, "transfer-summary.csv")
    require(len(transfers) == 2, "capacity task transfer summary count mismatch")
    require(
        all(
            int_field(row, "received_application_bytes")
            == int_field(row, "declared_size_bytes")
            for row in transfers
        ),
        "capacity task transfer payload mismatch",
    )
    require(
        not (Path(directory) / "diagnostics").exists(),
        "successful capacity task retained diagnostics",
    )


def validate_same_edge_epoch(directory):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == "global-capacity-aware-hrw", "epoch mode mismatch")
    require(run["run_status"] == "PARTIAL", "epoch partial status mismatch")
    require(run["transfer_count"] == 4, "epoch transfer count mismatch")
    require(run["flow_monitor_lost_packets"] == 0, "same-edge epoch lost packets")
    flows = rows_by_transfer(directory, "network-flow-details.csv")
    require(set(flows) == {1, 2, 3, 4}, "epoch transfer coverage mismatch")
    require(
        int_field(flows[1], "tx_packets") > 0
        and int_field(flows[2], "tx_packets") > 0,
        "epoch admitted flows did not send",
    )
    require(
        int_field(flows[3], "flow_monitor_id") == 0
        and int_field(flows[4], "flow_monitor_id") == 0
        and int_field(flows[3], "tx_packets") == 0
        and int_field(flows[4], "tx_packets") == 0,
        "epoch waiting flows were not preserved as zero-send records",
    )
    route_events = read_rows(directory, "ecmp-route-events.csv")
    sticky_epochs = {
        int_field(row, "route_epoch")
        for row in route_events
        if row["selection_reason"] == "CAPACITY_AWARE_STICKY"
    }
    require(sticky_epochs == {0, 1}, "same-edge route epoch did not retain pinned paths")
    summary = read_json(directory, "size-aware-summary.json")
    require(summary["active_flow_count_at_end"] == 2, "epoch active flow count mismatch")
    require(summary["assignment_count_at_end"] == 4, "epoch assignment count mismatch")


def validate_replay(first, second):
    deterministic_files = (
        "network-flow-details.csv",
        "network-flow-metrics.csv",
        "transfer-summary.csv",
        "ecmp-route-events.csv",
        "size-aware-reservation-events.csv",
        "size-aware-summary.json",
        "diagnostics/failure/flow-drop-reasons.csv",
    )
    for filename in deterministic_files:
        left = (Path(first) / filename).read_bytes()
        right = (Path(second) / filename).read_bytes()
        require(left == right, f"capacity replay differs for {filename}")
    first_run = read_json(first, "run-summary.json")
    second_run = read_json(second, "run-summary.json")
    first_run.pop("wall_clock_s")
    second_run.pop("wall_clock_s")
    require(first_run == second_run, "capacity run summary replay differs")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--capacity-first", required=True)
    parser.add_argument("--capacity-second", required=True)
    parser.add_argument("--parallel-ecmp", required=True)
    parser.add_argument("--task-mode", required=True)
    parser.add_argument("--same-edge-epoch", required=True)
    arguments = parser.parse_args()

    validate_baseline(arguments.baseline)
    validate_capacity(arguments.capacity_first)
    validate_capacity(arguments.capacity_second)
    validate_parallel_ecmp(arguments.parallel_ecmp)
    validate_task_mode(arguments.task_mode)
    validate_same_edge_epoch(arguments.same_edge_epoch)
    validate_replay(arguments.capacity_first, arguments.capacity_second)
    print(
        "PASS: baseline loses QueueDisc packets; capacity-aware path admission "
        "completes 2/2 transfers with 17144/17144 packets and deterministic zero loss; "
        "two free ECMP paths remain concurrent; task input/result lifecycle completes; "
        "same-edge route epochs preserve active paths and waiting-flow metrics"
    )


if __name__ == "__main__":
    main()
