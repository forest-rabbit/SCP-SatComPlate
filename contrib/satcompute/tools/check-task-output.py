#!/usr/bin/env python3
"""Validate deterministic SatCompute task, compute, and network evidence."""

import argparse
import csv
import hashlib
import json
import math
import sys
from collections import defaultdict
from pathlib import Path


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1

PROFILE_ROOT_FIELDS = {"schema_version", "compute_nodes"}
PROFILE_NODE_FIELDS = {
    "node_id",
    "compute_rate_work_units_per_second",
}
TASK_ROOT_FIELDS = {"schema_version", "tasks"}
TASK_FIELDS = {
    "task_id",
    "source_node_id",
    "compute_node_id",
    "result_node_id",
    "input_bytes",
    "output_bytes",
    "compute_work_units",
    "arrival_time_ns",
}

TASK_EVENT_FIELDS = [
    "simulation_time_ns",
    "task_id",
    "from_state",
    "to_state",
    "node_id",
    "cause",
]
TASK_SUMMARY_FIELDS = [
    "task_id",
    "source_node_id",
    "compute_node_id",
    "result_node_id",
    "input_bytes",
    "output_bytes",
    "compute_work_units",
    "compute_rate_work_units_per_second",
    "input_transfer_id",
    "result_transfer_id",
    "arrival_time_ns",
    "input_transfer_complete_time_ns",
    "queue_enter_time_ns",
    "compute_start_time_ns",
    "compute_complete_time_ns",
    "result_transfer_start_time_ns",
    "result_transfer_complete_time_ns",
    "input_transfer_delay_ns",
    "queue_delay_ns",
    "compute_service_time_ns",
    "result_transfer_delay_ns",
    "end_to_end_completion_delay_ns",
    "final_state",
]
COMPUTE_SUMMARY_FIELDS = [
    "node_id",
    "compute_rate_work_units_per_second",
    "enqueued_tasks",
    "completed_tasks",
    "busy_time_ns",
    "max_queue_length",
    "utilization_percent",
]
TRANSFER_SUMMARY_FIELDS = [
    "transfer_id",
    "source_node_id",
    "destination_node_id",
    "source_address",
    "destination_address",
    "source_port",
    "destination_port",
    "declared_size_bytes",
    "effective_payload_bytes",
    "pacing_mode",
    "derived_packet_count",
    "final_packet_payload_bytes",
    "arrival_time_ns",
    "last_send_time_ns",
    "sent_application_bytes",
    "received_application_bytes",
    "received_packet_count",
    "completion_time_ns",
    "completion_delay_ns",
]
FLOW_DETAIL_FIELDS = [
    "flow_monitor_id",
    "transfer_id",
    "source_address",
    "destination_address",
    "protocol",
    "source_port",
    "destination_port",
    "planned_application_payload_bytes",
    "received_application_payload_bytes",
    "tx_packets",
    "rx_packets",
    "lost_packets",
    "tx_bytes",
    "rx_bytes",
    "time_first_tx_ns",
    "time_last_rx_ns",
    "mean_delay_ns",
    "mean_jitter_ns",
    "throughput_bps",
]
INCOMPLETE_TASK_FIELDS = [
    "task_id",
    "state",
    "source_node_id",
    "compute_node_id",
    "result_node_id",
    "input_transfer_id",
    "result_transfer_id",
    "arrival_time_ns",
    "last_transition_time_ns",
    "input_transfer_complete_time_ns",
    "queue_enter_time_ns",
    "compute_start_time_ns",
    "compute_complete_time_ns",
    "result_transfer_complete_time_ns",
]
INCOMPLETE_TRANSFER_FIELDS = [
    "transfer_id",
    "transfer_state",
    "source_node_id",
    "destination_node_id",
    "source_address",
    "destination_address",
    "source_port",
    "destination_port",
    "declared_size_bytes",
    "payload_bytes_per_packet",
    "derived_packet_count",
    "sent_application_bytes",
    "sent_packet_count",
    "received_application_bytes",
    "received_packet_count",
    "missing_application_bytes",
    "missing_packet_count_lower_bound",
    "arrival_time_ns",
    "last_send_time_ns",
    "completion_time_ns",
]
QUEUE_DROP_FIELDS = [
    "simulation_time_ns",
    "source_node_id",
    "destination_node_id",
    "output_interface",
    "packet_size_bytes",
    "cumulative_drop_packets",
    "cumulative_drop_bytes",
]
QUEUE_DROP_SUMMARY_FIELDS = [
    "source_node_id",
    "destination_node_id",
    "output_interface",
    "drop_packets",
    "drop_bytes",
    "first_drop_time_ns",
    "last_drop_time_ns",
]
FLOW_LINK_FIELDS = [
    "source_node_id",
    "destination_node_id",
    "output_interface",
    "unique_transfer_count",
    "planned_application_bytes",
    "large_transfer_count",
    "input_transfer_count",
    "result_transfer_count",
    "adjacent_to_compute_node",
    "drop_packets",
    "drop_bytes",
]
EXPECTED_TRANSITIONS = [
    ("PENDING", "INPUT_TRANSFERRING", "TASK_ARRIVAL"),
    ("INPUT_TRANSFERRING", "QUEUED", "INPUT_TRANSFER_COMPLETE"),
    ("QUEUED", "RUNNING", "COMPUTE_DISPATCH"),
    ("RUNNING", "RESULT_TRANSFERRING", "COMPUTE_COMPLETE"),
    ("RESULT_TRANSFERRING", "COMPLETED", "RESULT_TRANSFER_COMPLETE"),
]
DETERMINISTIC_FILES = [
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "transfer-summary.csv",
    "network-flow-metrics.csv",
    "network-flow-details.csv",
    "ecmp-route-events.csv",
]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_json(path):
    with Path(path).open(encoding="utf-8") as stream:
        return json.load(stream)


def require_integer(value, field, minimum, maximum):
    require(
        isinstance(value, int) and not isinstance(value, bool),
        f"{field} must be an integer",
    )
    require(minimum <= value <= maximum, f"{field} is out of range")


def read_csv_rows(directory, filename, expected_fields):
    path = Path(directory) / filename
    require(path.is_file(), f"missing output file: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == expected_fields, f"{filename} header mismatch")
        return list(reader)


def int_field(row, field):
    try:
        return int(row[field])
    except (KeyError, ValueError) as error:
        raise AssertionError(f"{field} must be an integer") from error


def float_field(row, field):
    try:
        value = float(row[field])
    except (KeyError, ValueError) as error:
        raise AssertionError(f"{field} must be numeric") from error
    require(math.isfinite(value), f"{field} must be finite")
    return value


def flow_key(row):
    return (
        row["source_address"],
        row["destination_address"],
        int_field(row, "protocol"),
        int_field(row, "source_port"),
        int_field(row, "destination_port"),
    )


def read_topology_nodes(directory):
    root = read_json(Path(directory) / "nodes_0s.json")
    require(isinstance(root, dict), "topology nodes root must be an object")
    require(isinstance(root.get("nodes"), list), "topology nodes must be an array")
    node_ids = set()
    for item in root["nodes"]:
        require(isinstance(item, dict), "topology node must be an object")
        node_id = item.get("node_id")
        require_integer(node_id, "node_id", 0, UINT32_MAX)
        require(item.get("node_type") == "sat", "topology must be satellite-only")
        require(node_id not in node_ids, f"duplicate topology node {node_id}")
        node_ids.add(node_id)
    require(node_ids, "topology must contain nodes")
    return node_ids


def read_topology_links(directory):
    root = read_json(Path(directory) / "topology_0s.json")
    require(isinstance(root, dict), "topology links root must be an object")
    require(isinstance(root.get("links"), list), "topology links must be an array")
    links = set()
    for item in root["links"]:
        require(isinstance(item, dict), "topology link must be an object")
        source = item.get("node1_id")
        destination = item.get("node2_id")
        require_integer(source, "topology node1_id", 0, UINT32_MAX)
        require_integer(destination, "topology node2_id", 0, UINT32_MAX)
        require(source != destination, "topology link must connect distinct nodes")
        key = tuple(sorted((source, destination)))
        require(key not in links, f"duplicate topology link {key}")
        links.add(key)
    require(links, "topology must contain links")
    return links


def read_compute_profile(path, topology_nodes):
    root = read_json(path)
    require(
        isinstance(root, dict) and set(root) == PROFILE_ROOT_FIELDS,
        "ComputeProfile root must contain exactly schema_version and compute_nodes",
    )
    require(root["schema_version"] == "0.1", "ComputeProfile version must be 0.1")
    require(
        isinstance(root["compute_nodes"], list) and root["compute_nodes"],
        "ComputeProfile compute_nodes must be a non-empty array",
    )
    nodes = []
    seen = set()
    for item in root["compute_nodes"]:
        require(
            isinstance(item, dict) and set(item) == PROFILE_NODE_FIELDS,
            "ComputeProfile node fields violate the closed-world contract",
        )
        node_id = item["node_id"]
        rate = item["compute_rate_work_units_per_second"]
        require_integer(node_id, "ComputeProfile node_id", 0, UINT32_MAX)
        require_integer(rate, "compute rate", 1, UINT64_MAX)
        require(node_id in topology_nodes, f"unknown compute node {node_id}")
        require(node_id not in seen, f"duplicate compute node {node_id}")
        seen.add(node_id)
        nodes.append(item)
    return sorted(nodes, key=lambda item: item["node_id"])


def read_task_trace(path, topology_nodes, compute_nodes):
    root = read_json(path)
    require(
        isinstance(root, dict) and set(root) == TASK_ROOT_FIELDS,
        "TaskTrace root must contain exactly schema_version and tasks",
    )
    require(root["schema_version"] == "0.1", "TaskTrace version must be 0.1")
    require(
        isinstance(root["tasks"], list) and root["tasks"],
        "TaskTrace tasks must be a non-empty array",
    )
    compute_ids = {item["node_id"] for item in compute_nodes}
    tasks = []
    seen = set()
    for item in root["tasks"]:
        require(
            isinstance(item, dict) and set(item) == TASK_FIELDS,
            "TaskTrace task fields violate the closed-world contract",
        )
        for field in TASK_FIELDS:
            require_integer(item[field], field, 0, UINT64_MAX)
        require(0 < item["task_id"] <= UINT64_MAX // 2, "invalid task_id")
        for field in ("source_node_id", "compute_node_id", "result_node_id"):
            require(item[field] <= UINT32_MAX, f"{field} exceeds uint32")
            require(item[field] in topology_nodes, f"unknown {field}")
        require(
            item["compute_node_id"] in compute_ids,
            "compute_node_id does not reference ComputeProfile",
        )
        require(
            item["source_node_id"] != item["compute_node_id"],
            "source_node_id must differ from compute_node_id",
        )
        require(
            item["compute_node_id"] != item["result_node_id"],
            "compute_node_id must differ from result_node_id",
        )
        for field in ("input_bytes", "output_bytes", "compute_work_units"):
            require(item[field] > 0, f"{field} must be positive")
        require(item["arrival_time_ns"] <= (1 << 63) - 1, "arrival exceeds int64")
        require(item["task_id"] not in seen, "duplicate task_id")
        seen.add(item["task_id"])
        tasks.append(item)
    return sorted(tasks, key=lambda item: item["task_id"])


def service_time_ns(work_units, rate):
    return (work_units * 1_000_000_000 + rate - 1) // rate


def expected_payload(run, size_bytes):
    mode = run["transfer_chunk_mode"]
    require(mode in {"fixed", "size-aware"}, "invalid transfer chunk mode")
    if mode == "fixed":
        payload = run["fixed_payload_bytes"]
        require(isinstance(payload, int) and payload > 0, "invalid fixed payload")
        return payload
    require(run["fixed_payload_bytes"] is None, "size-aware fixed payload must be null")
    if size_bytes <= 1 << 20:
        return 1024
    if size_bytes <= 64 << 20:
        return 8192
    return 64000


def validate_task_summaries(tasks, profile, rows):
    require(len(rows) == len(tasks), "task-summary row count mismatch")
    require(
        [int_field(row, "task_id") for row in rows]
        == [task["task_id"] for task in tasks],
        "task-summary must use canonical task_id order",
    )
    rates = {
        item["node_id"]: item["compute_rate_work_units_per_second"]
        for item in profile
    }
    summaries = {}
    for task, row in zip(tasks, rows):
        task_id = task["task_id"]
        for field in (
            "source_node_id",
            "compute_node_id",
            "result_node_id",
            "input_bytes",
            "output_bytes",
            "compute_work_units",
            "arrival_time_ns",
        ):
            require(int_field(row, field) == task[field], f"task {task_id} {field} mismatch")
        rate = rates[task["compute_node_id"]]
        require(
            int_field(row, "compute_rate_work_units_per_second") == rate,
            f"task {task_id} compute rate mismatch",
        )
        require(
            int_field(row, "input_transfer_id") == 2 * task_id - 1
            and int_field(row, "result_transfer_id") == 2 * task_id,
            f"task {task_id} transfer ID mapping mismatch",
        )
        times = {
            field: int_field(row, field)
            for field in (
                "input_transfer_complete_time_ns",
                "queue_enter_time_ns",
                "compute_start_time_ns",
                "compute_complete_time_ns",
                "result_transfer_start_time_ns",
                "result_transfer_complete_time_ns",
            )
        }
        require(
            times["input_transfer_complete_time_ns"]
            == times["queue_enter_time_ns"],
            f"task {task_id} input completion/queue time mismatch",
        )
        require(
            times["compute_start_time_ns"] >= times["queue_enter_time_ns"],
            f"task {task_id} computed before input completion",
        )
        require(
            times["result_transfer_start_time_ns"]
            == times["compute_complete_time_ns"],
            f"task {task_id} result did not start at compute completion",
        )
        input_delay = times["input_transfer_complete_time_ns"] - task["arrival_time_ns"]
        queue_delay = times["compute_start_time_ns"] - times["queue_enter_time_ns"]
        compute_delay = times["compute_complete_time_ns"] - times["compute_start_time_ns"]
        result_delay = (
            times["result_transfer_complete_time_ns"]
            - times["result_transfer_start_time_ns"]
        )
        completion_delay = (
            times["result_transfer_complete_time_ns"] - task["arrival_time_ns"]
        )
        require(min(input_delay, queue_delay, compute_delay, result_delay) >= 0, "negative delay")
        require(
            compute_delay == service_time_ns(task["compute_work_units"], rate),
            f"task {task_id} exact compute duration mismatch",
        )
        for field, expected in (
            ("input_transfer_delay_ns", input_delay),
            ("queue_delay_ns", queue_delay),
            ("compute_service_time_ns", compute_delay),
            ("result_transfer_delay_ns", result_delay),
            ("end_to_end_completion_delay_ns", completion_delay),
        ):
            require(int_field(row, field) == expected, f"task {task_id} {field} mismatch")
        require(row["final_state"] == "COMPLETED", f"task {task_id} is not COMPLETED")
        summaries[task_id] = row
    return summaries


def validate_task_events(tasks, summaries, rows):
    require(len(rows) == len(tasks) * 5, "each task must have exactly five events")
    by_task = defaultdict(list)
    for row in rows:
        by_task[int_field(row, "task_id")].append(row)
    require(set(by_task) == {task["task_id"] for task in tasks}, "task event coverage mismatch")
    for task in tasks:
        task_id = task["task_id"]
        summary = summaries[task_id]
        events = by_task[task_id]
        expected_times = [
            task["arrival_time_ns"],
            int_field(summary, "input_transfer_complete_time_ns"),
            int_field(summary, "compute_start_time_ns"),
            int_field(summary, "compute_complete_time_ns"),
            int_field(summary, "result_transfer_complete_time_ns"),
        ]
        expected_nodes = [
            task["source_node_id"],
            task["compute_node_id"],
            task["compute_node_id"],
            task["compute_node_id"],
            task["result_node_id"],
        ]
        for index, (from_state, to_state, cause) in enumerate(EXPECTED_TRANSITIONS):
            row = events[index]
            require(
                (row["from_state"], row["to_state"], row["cause"])
                == (from_state, to_state, cause),
                f"task {task_id} transition {index} mismatch",
            )
            require(
                int_field(row, "simulation_time_ns") == expected_times[index]
                and int_field(row, "node_id") == expected_nodes[index],
                f"task {task_id} event {index} time/node mismatch",
            )


def validate_transfer_evidence(tasks, summaries, output, run):
    transfer_rows = read_csv_rows(output, "transfer-summary.csv", TRANSFER_SUMMARY_FIELDS)
    detail_rows = read_csv_rows(output, "network-flow-details.csv", FLOW_DETAIL_FIELDS)
    require(len(transfer_rows) == 2 * len(tasks), "task transfer row count mismatch")
    require(len(detail_rows) == 2 * len(tasks), "task flow detail row count mismatch")
    transfers = {int_field(row, "transfer_id"): row for row in transfer_rows}
    details = {int_field(row, "transfer_id"): row for row in detail_rows}
    require(len(transfers) == len(transfer_rows), "duplicate transfer summary ID")
    require(len(details) == len(detail_rows), "duplicate flow transfer ID")

    route_rows = read_csv_rows(
        output,
        "ecmp-route-events.csv",
        [
            "simulation_time_ns",
            "route_epoch",
            "node_id",
            "source_address",
            "destination_address",
            "protocol",
            "source_port",
            "destination_port",
            "candidate_count_before_dedup",
            "candidate_count_after_dedup",
            "selected_index",
            "selected_gateway",
            "selected_output_interface",
            "hash_value",
            "selection_reason",
        ],
    )
    route_index = defaultdict(list)
    for event in route_rows:
        route_index[flow_key(event)].append(event)

    expected_plans = []
    for task in tasks:
        summary = summaries[task["task_id"]]
        expected_plans.extend(
            [
                {
                    "transfer_id": 2 * task["task_id"] - 1,
                    "source_node_id": task["source_node_id"],
                    "destination_node_id": task["compute_node_id"],
                    "size_bytes": task["input_bytes"],
                    "start_time_ns": task["arrival_time_ns"],
                    "completion_time_ns": int_field(
                        summary, "input_transfer_complete_time_ns"
                    ),
                },
                {
                    "transfer_id": 2 * task["task_id"],
                    "source_node_id": task["compute_node_id"],
                    "destination_node_id": task["result_node_id"],
                    "size_bytes": task["output_bytes"],
                    "start_time_ns": int_field(
                        summary, "result_transfer_start_time_ns"
                    ),
                    "completion_time_ns": int_field(
                        summary, "result_transfer_complete_time_ns"
                    ),
                },
            ]
        )
    expected_plans.sort(key=lambda item: item["transfer_id"])
    require(
        set(transfers) == {item["transfer_id"] for item in expected_plans},
        "task transfer ID coverage mismatch",
    )

    next_source_port = defaultdict(lambda: 10000)
    total_bytes = 0
    total_packets = 0
    flow_keys = set()
    for plan in expected_plans:
        transfer_id = plan["transfer_id"]
        row = transfers[transfer_id]
        detail = details[transfer_id]
        payload = expected_payload(run, plan["size_bytes"])
        packets = (plan["size_bytes"] + payload - 1) // payload
        final_payload = plan["size_bytes"] % payload or payload
        source_port = next_source_port[plan["source_node_id"]]
        next_source_port[plan["source_node_id"]] += 1
        require(
            int_field(row, "source_node_id") == plan["source_node_id"]
            and int_field(row, "destination_node_id")
            == plan["destination_node_id"],
            f"transfer {transfer_id} endpoint mismatch",
        )
        require(
            int_field(row, "declared_size_bytes") == plan["size_bytes"]
            and int_field(row, "effective_payload_bytes") == payload
            and int_field(row, "derived_packet_count") == packets
            and int_field(row, "final_packet_payload_bytes") == final_payload,
            f"transfer {transfer_id} packetization mismatch",
        )
        require(
            int_field(row, "source_port") == source_port
            and int_field(row, "destination_port") == 9000,
            f"transfer {transfer_id} canonical port mismatch",
        )
        require(
            int_field(row, "arrival_time_ns") == plan["start_time_ns"]
            and int_field(row, "completion_time_ns")
            == plan["completion_time_ns"],
            f"transfer {transfer_id} start/completion mismatch",
        )
        require(
            int_field(row, "completion_delay_ns")
            == plan["completion_time_ns"] - plan["start_time_ns"],
            f"transfer {transfer_id} completion delay mismatch",
        )
        require(
            int_field(row, "last_send_time_ns") >= plan["start_time_ns"],
            f"transfer {transfer_id} last send precedes start",
        )
        if packets == 1:
            require(
                int_field(row, "last_send_time_ns") == plan["start_time_ns"],
                f"single-packet transfer {transfer_id} did not send at start",
            )
        require(
            int_field(row, "sent_application_bytes") == plan["size_bytes"]
            and int_field(row, "received_application_bytes")
            == plan["size_bytes"]
            and int_field(row, "received_packet_count") == packets,
            f"transfer {transfer_id} payload completeness mismatch",
        )
        require(row["pacing_mode"] == "first-hop-serialization", "pacing mismatch")
        require(
            detail["source_address"] == row["source_address"]
            and detail["destination_address"] == row["destination_address"]
            and int_field(detail, "source_port") == source_port
            and int_field(detail, "destination_port") == 9000,
            f"transfer {transfer_id} five-tuple metadata mismatch",
        )
        require(
            int_field(detail, "protocol") == 17
            and int_field(detail, "planned_application_payload_bytes")
            == plan["size_bytes"]
            and int_field(detail, "received_application_payload_bytes")
            == plan["size_bytes"]
            and int_field(detail, "tx_packets") == packets
            and int_field(detail, "rx_packets") == packets
            and int_field(detail, "lost_packets") == 0,
            f"transfer {transfer_id} FlowMonitor evidence mismatch",
        )
        transfer_flow_key = flow_key(detail)
        require(
            transfer_flow_key not in flow_keys,
            "duplicate task transfer five-tuple",
        )
        flow_keys.add(transfer_flow_key)
        plan["flow_key"] = transfer_flow_key
        matching_routes = route_index.get(transfer_flow_key, [])
        require(matching_routes, f"transfer {transfer_id} lacks ECMP route evidence")
        total_bytes += plan["size_bytes"]
        total_packets += packets
    return expected_plans, route_rows, total_bytes, total_packets


def validate_compute_summaries(profile, tasks, task_summaries, output, run):
    rows = read_csv_rows(output, "compute-node-summary.csv", COMPUTE_SUMMARY_FIELDS)
    require(len(rows) == len(profile), "compute-node-summary row count mismatch")
    require(
        [int_field(row, "node_id") for row in rows]
        == [node["node_id"] for node in profile],
        "compute-node-summary must use canonical node_id order",
    )
    simulation_duration_ns = round(run["simulation_duration_s"] * 1_000_000_000)
    by_node = defaultdict(list)
    for task in tasks:
        by_node[task["compute_node_id"]].append(task["task_id"])
    for node, row in zip(profile, rows):
        node_id = node["node_id"]
        task_ids = by_node[node_id]
        busy_time = sum(
            int_field(task_summaries[task_id], "compute_service_time_ns")
            for task_id in task_ids
        )
        require(
            int_field(row, "compute_rate_work_units_per_second")
            == node["compute_rate_work_units_per_second"],
            f"compute node {node_id} rate mismatch",
        )
        require(
            int_field(row, "enqueued_tasks") == len(task_ids)
            and int_field(row, "completed_tasks") == len(task_ids)
            and int_field(row, "busy_time_ns") == busy_time,
            f"compute node {node_id} counters mismatch",
        )
        max_queue = int_field(row, "max_queue_length")
        require(0 <= max_queue <= len(task_ids), f"compute node {node_id} queue metric invalid")
        if task_ids:
            require(max_queue >= 1, f"compute node {node_id} queue metric missing")
        expected_utilization = busy_time * 100.0 / simulation_duration_ns
        require(
            math.isclose(
                float_field(row, "utilization_percent"),
                expected_utilization,
                rel_tol=1e-12,
                abs_tol=1e-12,
            ),
            f"compute node {node_id} utilization mismatch",
        )
    return rows


def validate_generic_fcfs(tasks, task_summaries):
    by_node = defaultdict(list)
    for task in tasks:
        by_node[task["compute_node_id"]].append(task)
    for node_id, node_tasks in by_node.items():
        expected_order = sorted(
            node_tasks,
            key=lambda task: (
                int_field(
                    task_summaries[task["task_id"]],
                    "queue_enter_time_ns",
                ),
                task["task_id"],
            ),
        )
        actual_order = sorted(
            node_tasks,
            key=lambda task: (
                int_field(
                    task_summaries[task["task_id"]],
                    "compute_start_time_ns",
                ),
                task["task_id"],
            ),
        )
        require(
            [task["task_id"] for task in actual_order]
            == [task["task_id"] for task in expected_order],
            f"compute node {node_id} violates FCFS dispatch order",
        )
        previous_completion = None
        for task in actual_order:
            summary = task_summaries[task["task_id"]]
            queue_enter = int_field(summary, "queue_enter_time_ns")
            compute_start = int_field(summary, "compute_start_time_ns")
            expected_start = (
                queue_enter
                if previous_completion is None
                else max(queue_enter, previous_completion)
            )
            require(
                compute_start == expected_start,
                f"compute node {node_id} has an FCFS gap or overlap",
            )
            previous_completion = int_field(
                summary,
                "compute_complete_time_ns",
            )


def validate_run_summary(
    run,
    profile_path,
    trace_path,
    profile,
    tasks,
    task_summaries,
    total_bytes,
    total_packets,
):
    require(run["mode"] == "task", "run mode must be task")
    require(run["compute_profile_path"] == str(profile_path), "profile path mismatch")
    require(run["task_trace_path"] == str(trace_path), "task trace path mismatch")
    require(run["compute_node_count"] == len(profile), "compute node count mismatch")
    require(run["task_count"] == len(tasks), "task count mismatch")
    require(run["completed_task_count"] == len(tasks), "completed task count mismatch")
    require(
        run["total_input_bytes"] == sum(task["input_bytes"] for task in tasks)
        and run["total_output_bytes"] == sum(task["output_bytes"] for task in tasks)
        and run["total_compute_work_units"]
        == sum(task["compute_work_units"] for task in tasks),
        "task aggregate totals mismatch",
    )
    delays = [
        int_field(task_summaries[task["task_id"]], "end_to_end_completion_delay_ns")
        for task in tasks
    ]
    require(
        run["mean_task_completion_delay_ns"] == sum(delays) // len(delays)
        and run["max_task_completion_delay_ns"] == max(delays),
        "task completion delay aggregate mismatch",
    )
    require(run["transfer_count"] == 2 * len(tasks), "task transfer count mismatch")
    require(
        run["declared_application_bytes"] == total_bytes
        and run["sent_application_bytes"] == total_bytes
        and run["received_application_bytes"] == total_bytes,
        "task transfer byte aggregate mismatch",
    )
    require(
        run["derived_udp_packets"] == total_packets
        and run["flow_monitor_tx_packets"] == total_packets
        and run["flow_monitor_rx_packets"] == total_packets
        and run["flow_monitor_lost_packets"] == 0,
        "task packet aggregate mismatch",
    )


def validate_workload_summary(summary_path, trace_path, tasks):
    summary = read_json(summary_path)
    require(isinstance(summary, dict), "workload summary root must be an object")
    required_fields = {
        "task_count",
        "total_input_bytes",
        "total_output_bytes",
        "total_compute_work_units",
        "task_trace_sha256",
    }
    require(
        required_fields <= set(summary),
        "workload summary is missing required audit fields",
    )
    expected_totals = {
        "task_count": len(tasks),
        "total_input_bytes": sum(task["input_bytes"] for task in tasks),
        "total_output_bytes": sum(task["output_bytes"] for task in tasks),
        "total_compute_work_units": sum(
            task["compute_work_units"] for task in tasks
        ),
    }
    for field, expected in expected_totals.items():
        require_integer(summary[field], f"workload summary {field}", 0, UINT64_MAX)
        require(
            summary[field] == expected,
            f"workload summary {field} does not match TaskTrace",
        )
    expected_hash = hashlib.sha256(Path(trace_path).read_bytes()).hexdigest()
    require(
        summary["task_trace_sha256"] == expected_hash,
        "workload summary TaskTrace SHA-256 mismatch",
    )
    return summary


def validate_scenario(output, topology_dir, profile_path, trace_path):
    topology_nodes = read_topology_nodes(topology_dir)
    profile = read_compute_profile(profile_path, topology_nodes)
    tasks = read_task_trace(trace_path, topology_nodes, profile)
    run = read_json(Path(output) / "run-summary.json")
    simulation_duration_ns = round(run["simulation_duration_s"] * 1_000_000_000)
    require(
        all(task["arrival_time_ns"] < simulation_duration_ns for task in tasks),
        "task arrival must precede simulation stop",
    )
    task_rows = read_csv_rows(output, "task-summary.csv", TASK_SUMMARY_FIELDS)
    task_summaries = validate_task_summaries(tasks, profile, task_rows)
    event_rows = read_csv_rows(output, "task-events.csv", TASK_EVENT_FIELDS)
    validate_task_events(tasks, task_summaries, event_rows)
    plans, route_rows, total_bytes, total_packets = validate_transfer_evidence(
        tasks, task_summaries, output, run
    )
    compute_rows = validate_compute_summaries(
        profile, tasks, task_summaries, output, run
    )
    validate_generic_fcfs(tasks, task_summaries)
    validate_run_summary(
        run,
        profile_path,
        trace_path,
        profile,
        tasks,
        task_summaries,
        total_bytes,
        total_packets,
    )
    return {
        "profile": profile,
        "tasks": tasks,
        "task_summaries": task_summaries,
        "plans": plans,
        "route_rows": route_rows,
        "compute_rows": compute_rows,
        "run": run,
    }


def validate_single(result):
    require(len(result["tasks"]) == 1, "single scenario must contain one task")
    for plan in result["plans"]:
        transfer_id = plan["transfer_id"]
        matching = [
            event
            for event in result["route_rows"]
            if (
                event["source_address"],
                event["destination_address"],
                int_field(event, "protocol"),
                int_field(event, "source_port"),
                int_field(event, "destination_port"),
            )
            == plan["flow_key"]
            and int_field(event, "candidate_count_after_dedup") > 1
            and event["selection_reason"] == "HASH_PER_FLOW"
        ]
        require(matching, f"single transfer {transfer_id} lacks ECMP hash evidence")
    print("PASS: single task completes INPUT -> COMPUTE -> RESULT with ECMP evidence")


def validate_fcfs(result):
    tasks = result["tasks"]
    require(len(tasks) >= 3, "FCFS scenario must contain at least three tasks")
    require(
        len({task["compute_node_id"] for task in tasks}) == 1,
        "FCFS tasks must share one compute node",
    )
    rows = result["task_summaries"]
    expected_order = sorted(
        tasks,
        key=lambda task: (
            int_field(rows[task["task_id"]], "queue_enter_time_ns"),
            task["task_id"],
        ),
    )
    actual_order = sorted(
        tasks,
        key=lambda task: (
            int_field(rows[task["task_id"]], "compute_start_time_ns"),
            task["task_id"],
        ),
    )
    require(
        [task["task_id"] for task in actual_order]
        == [task["task_id"] for task in expected_order],
        "FCFS dispatch order mismatch",
    )
    for previous, current in zip(actual_order, actual_order[1:]):
        require(
            int_field(rows[previous["task_id"]], "compute_complete_time_ns")
            == int_field(rows[current["task_id"]], "compute_start_time_ns"),
            "FCFS introduced a gap or overlap between tasks",
        )
    require(
        any(int_field(row, "queue_delay_ns") > 0 for row in rows.values()),
        "FCFS scenario must expose positive queue delay",
    )
    require(
        max(int_field(row, "max_queue_length") for row in result["compute_rows"]) >= 2,
        "FCFS scenario must expose a waiting queue",
    )
    print("PASS: non-preemptive single-server FCFS order and queue evidence")


def validate_heterogeneous(result):
    tasks = result["tasks"]
    require(len(tasks) == 2, "heterogeneous scenario must contain two tasks")
    require(
        len({task["compute_work_units"] for task in tasks}) == 1,
        "heterogeneous tasks must use equal work",
    )
    rows = result["task_summaries"]
    pairs = sorted(
        (
            int_field(rows[task["task_id"]], "compute_rate_work_units_per_second"),
            int_field(rows[task["task_id"]], "compute_service_time_ns"),
        )
        for task in tasks
    )
    require(
        pairs[1][0] == 2 * pairs[0][0]
        and pairs[0][1] == 2 * pairs[1][1],
        "2R compute node must complete equal work in half the time",
    )
    print("PASS: heterogeneous R/2R compute duration is exact")


def normalized_run(run, ignored_path_field):
    normalized = dict(run)
    normalized.pop("wall_clock_s", None)
    normalized.pop(ignored_path_field, None)
    return normalized


def validate_determinism(
    first_output,
    second_output,
    first_result,
    second_result,
    ignored_path_field,
    label,
):
    require(first_result["profile"] == second_result["profile"], f"{label} profile differs")
    require(first_result["tasks"] == second_result["tasks"], f"{label} task set differs")
    for filename in DETERMINISTIC_FILES:
        require(
            (Path(first_output) / filename).read_bytes()
            == (Path(second_output) / filename).read_bytes(),
            f"{label} output differs: {filename}",
        )
    require(
        normalized_run(first_result["run"], ignored_path_field)
        == normalized_run(second_result["run"], ignored_path_field),
        f"{label} run-summary differs outside path/wall clock",
    )
    print(f"PASS: {label} preserves deterministic structured output")


def directed_link_key(row):
    return (
        int_field(row, "source_node_id"),
        int_field(row, "destination_node_id"),
        int_field(row, "output_interface"),
    )


def validate_failure_diagnostics(output, topology_dir, profile_path, trace_path):
    topology_nodes = read_topology_nodes(topology_dir)
    topology_links = read_topology_links(topology_dir)
    profile = read_compute_profile(profile_path, topology_nodes)
    tasks = read_task_trace(trace_path, topology_nodes, profile)
    compute_nodes = {item["node_id"] for item in profile}
    expected_transfers = {}
    for task in tasks:
        expected_transfers[2 * task["task_id"] - 1] = {
            "source_node_id": task["source_node_id"],
            "destination_node_id": task["compute_node_id"],
            "declared_size_bytes": task["input_bytes"],
        }
        expected_transfers[2 * task["task_id"]] = {
            "source_node_id": task["compute_node_id"],
            "destination_node_id": task["result_node_id"],
            "declared_size_bytes": task["output_bytes"],
        }

    output = Path(output)
    run = read_json(output / "run-summary.json")
    diagnostic = read_json(output / "diagnostic-summary.json")
    require(diagnostic.get("run_status") == "INCOMPLETE", "run must be INCOMPLETE")
    require(diagnostic.get("task_count") == len(tasks), "diagnostic task count mismatch")
    require(
        diagnostic.get("completed_task_count", -1)
        + diagnostic.get("incomplete_task_count", -1)
        == len(tasks),
        "diagnostic task completion counts mismatch",
    )
    require(diagnostic["incomplete_task_count"] > 0, "failure run has no incomplete task")
    require(
        isinstance(diagnostic.get("tasks_by_state"), dict)
        and sum(diagnostic["tasks_by_state"].values()) == len(tasks),
        "diagnostic task state counts mismatch",
    )
    require(
        diagnostic.get("transfer_count") == len(expected_transfers),
        "diagnostic transfer count mismatch",
    )
    require(
        diagnostic.get("completed_transfer_count", -1)
        + diagnostic.get("incomplete_transfer_count", -1)
        == len(expected_transfers),
        "diagnostic transfer completion counts mismatch",
    )

    task_rows = read_csv_rows(output, "task-summary.csv", TASK_SUMMARY_FIELDS)
    require(len(task_rows) == len(tasks), "partial task-summary row count mismatch")
    task_rows_by_id = {int_field(row, "task_id"): row for row in task_rows}
    require(
        len(task_rows_by_id) == len(task_rows),
        "partial task-summary contains duplicate task IDs",
    )
    incomplete_task_rows = read_csv_rows(
        output, "incomplete-tasks.csv", INCOMPLETE_TASK_FIELDS
    )
    incomplete_task_ids = {
        int_field(row, "task_id") for row in incomplete_task_rows
    }
    expected_incomplete_task_ids = {
        task_id
        for task_id, row in task_rows_by_id.items()
        if row["final_state"] != "COMPLETED"
    }
    require(
        incomplete_task_ids == expected_incomplete_task_ids,
        "incomplete task coverage mismatch",
    )
    require(
        len(incomplete_task_rows) == diagnostic["incomplete_task_count"],
        "incomplete task diagnostic count mismatch",
    )
    for row in incomplete_task_rows:
        task_id = int_field(row, "task_id")
        require(
            row["state"] == task_rows_by_id[task_id]["final_state"],
            f"task {task_id} partial state mismatch",
        )

    transfer_rows = read_csv_rows(output, "transfer-summary.csv", TRANSFER_SUMMARY_FIELDS)
    transfer_rows_by_id = {
        int_field(row, "transfer_id"): row for row in transfer_rows
    }
    require(
        len(transfer_rows) == len(expected_transfers)
        and len(transfer_rows_by_id) == len(expected_transfers)
        and set(transfer_rows_by_id) == set(expected_transfers),
        "partial transfer-summary coverage mismatch",
    )
    incomplete_transfer_rows = read_csv_rows(
        output, "incomplete-transfers.csv", INCOMPLETE_TRANSFER_FIELDS
    )
    incomplete_transfer_ids = {
        int_field(row, "transfer_id") for row in incomplete_transfer_rows
    }
    require(
        len(incomplete_transfer_ids) == len(incomplete_transfer_rows),
        "incomplete transfer IDs must be unique",
    )
    require(
        len(incomplete_transfer_rows) == diagnostic["incomplete_transfer_count"],
        "incomplete transfer diagnostic count mismatch",
    )
    sender_complete_receiver_incomplete = False
    for row in incomplete_transfer_rows:
        transfer_id = int_field(row, "transfer_id")
        require(transfer_id in expected_transfers, "unknown incomplete transfer ID")
        expected = expected_transfers[transfer_id]
        declared = int_field(row, "declared_size_bytes")
        sent_bytes = int_field(row, "sent_application_bytes")
        sent_packets = int_field(row, "sent_packet_count")
        received_bytes = int_field(row, "received_application_bytes")
        received_packets = int_field(row, "received_packet_count")
        derived_packets = int_field(row, "derived_packet_count")
        require(
            int_field(row, "source_node_id") == expected["source_node_id"]
            and int_field(row, "destination_node_id")
            == expected["destination_node_id"]
            and declared == expected["declared_size_bytes"],
            f"transfer {transfer_id} endpoint/size mismatch",
        )
        require(
            0 <= received_bytes <= sent_bytes <= declared,
            f"transfer {transfer_id} partial byte counters invalid",
        )
        require(
            0 <= received_packets <= sent_packets <= derived_packets,
            f"transfer {transfer_id} partial packet counters invalid",
        )
        require(
            int_field(row, "missing_application_bytes") == declared - received_bytes
            and int_field(row, "missing_packet_count_lower_bound")
            == derived_packets - received_packets,
            f"transfer {transfer_id} missing amount mismatch",
        )
        require(
            row["transfer_state"] in {"REGISTERED", "STARTED"},
            f"transfer {transfer_id} invalid incomplete state",
        )
        if row["transfer_state"] == "REGISTERED":
            require(
                sent_bytes == 0 and sent_packets == 0,
                f"registered transfer {transfer_id} must be unstarted",
            )
        if sent_bytes == declared and received_bytes < declared:
            sender_complete_receiver_incomplete = True
        summary = transfer_rows_by_id[transfer_id]
        require(
            int_field(summary, "sent_application_bytes") == sent_bytes
            and int_field(summary, "received_application_bytes") == received_bytes
            and int_field(summary, "received_packet_count") == received_packets,
            f"transfer {transfer_id} partial summary mismatch",
        )
    require(
        sender_complete_receiver_incomplete,
        "fixture must expose a fully sent but incompletely received transfer",
    )

    flow_rows = read_csv_rows(output, "network-flow-details.csv", FLOW_DETAIL_FIELDS)
    require(
        len(flow_rows) == len(expected_transfers)
        and {int_field(row, "transfer_id") for row in flow_rows}
            == set(expected_transfers),
        "failure FlowMonitor transfer coverage mismatch",
    )
    flow_tx_packets = sum(int_field(row, "tx_packets") for row in flow_rows)
    flow_rx_packets = sum(int_field(row, "rx_packets") for row in flow_rows)
    flow_lost_packets = sum(int_field(row, "lost_packets") for row in flow_rows)
    require(
        diagnostic["flowmonitor_tx_packets"] == flow_tx_packets
        and diagnostic["flowmonitor_rx_packets"] == flow_rx_packets
        and diagnostic["flowmonitor_lost_packets"] == flow_lost_packets,
        "FlowMonitor diagnostic aggregate mismatch",
    )

    drop_rows = read_csv_rows(output, "isl-queue-drops.csv", QUEUE_DROP_FIELDS)
    require(drop_rows, "small failure fixture must produce an ISL queue drop")
    drop_totals = {}
    previous_time = -1
    simulation_duration_ns = round(run["simulation_duration_s"] * 1_000_000_000)
    for row in drop_rows:
        key = directed_link_key(row)
        event_time = int_field(row, "simulation_time_ns")
        packet_bytes = int_field(row, "packet_size_bytes")
        require(previous_time <= event_time <= simulation_duration_ns, "drop time invalid")
        previous_time = event_time
        require(
            tuple(sorted(key[:2])) in topology_links,
            f"queue drop does not map to an ISL: {key}",
        )
        require(packet_bytes > 0, "queue drop packet size must be positive")
        aggregate = drop_totals.setdefault(
            key, {"packets": 0, "bytes": 0, "first": event_time, "last": event_time}
        )
        aggregate["packets"] += 1
        aggregate["bytes"] += packet_bytes
        aggregate["last"] = event_time
        require(
            int_field(row, "cumulative_drop_packets") == aggregate["packets"]
            and int_field(row, "cumulative_drop_bytes") == aggregate["bytes"],
            f"queue drop cumulative total mismatch: {key}",
        )

    drop_summary_rows = read_csv_rows(
        output, "isl-queue-drop-summary.csv", QUEUE_DROP_SUMMARY_FIELDS
    )
    expected_drop_order = sorted(
        drop_totals,
        key=lambda key: (
            -drop_totals[key]["bytes"],
            -drop_totals[key]["packets"],
            key,
        ),
    )
    require(
        [directed_link_key(row) for row in drop_summary_rows] == expected_drop_order,
        "queue drop summary order/coverage mismatch",
    )
    for row in drop_summary_rows:
        key = directed_link_key(row)
        expected = drop_totals[key]
        require(
            int_field(row, "drop_packets") == expected["packets"]
            and int_field(row, "drop_bytes") == expected["bytes"]
            and int_field(row, "first_drop_time_ns") == expected["first"]
            and int_field(row, "last_drop_time_ns") == expected["last"],
            f"queue drop summary mismatch: {key}",
        )
    total_drop_packets = sum(item["packets"] for item in drop_totals.values())
    total_drop_bytes = sum(item["bytes"] for item in drop_totals.values())
    require(
        diagnostic["queue_drop_packets"] == total_drop_packets
        and diagnostic["queue_drop_bytes"] == total_drop_bytes
        and diagnostic["dropped_directed_link_count"] == len(drop_totals),
        "queue drop diagnostic aggregate mismatch",
    )

    concentration_rows = read_csv_rows(
        output, "flow-link-concentration.csv", FLOW_LINK_FIELDS
    )
    concentration_keys = [directed_link_key(row) for row in concentration_rows]
    require(
        len(concentration_keys) == len(set(concentration_keys)),
        "flow-link concentration keys must be unique",
    )
    expected_concentration_order = sorted(
        concentration_rows,
        key=lambda row: (
            -int_field(row, "planned_application_bytes"),
            -int_field(row, "unique_transfer_count"),
            directed_link_key(row),
        ),
    )
    require(
        concentration_keys
        == [directed_link_key(row) for row in expected_concentration_order],
        "flow-link concentration order mismatch",
    )
    concentration_by_key = {
        directed_link_key(row): row for row in concentration_rows
    }
    for key, row in concentration_by_key.items():
        require(
            tuple(sorted(key[:2])) in topology_links,
            f"flow concentration does not map to an ISL: {key}",
        )
        unique_transfers = int_field(row, "unique_transfer_count")
        input_transfers = int_field(row, "input_transfer_count")
        result_transfers = int_field(row, "result_transfer_count")
        large_transfers = int_field(row, "large_transfer_count")
        require(
            input_transfers + result_transfers == unique_transfers
            and 0 <= large_transfers <= unique_transfers,
            f"flow concentration transfer counts invalid: {key}",
        )
        require(
            int_field(row, "adjacent_to_compute_node")
            == int(key[0] in compute_nodes or key[1] in compute_nodes),
            f"flow concentration compute adjacency mismatch: {key}",
        )
        if key in drop_totals:
            require(
                int_field(row, "drop_packets") == drop_totals[key]["packets"]
                and int_field(row, "drop_bytes") == drop_totals[key]["bytes"],
                f"flow concentration drop mismatch: {key}",
            )
    require(
        set(drop_totals) <= set(concentration_by_key),
        "dropped link missing from flow concentration",
    )

    expected_top_drops = drop_summary_rows[:10]
    actual_top_drops = diagnostic.get("top_dropped_links")
    require(
        isinstance(actual_top_drops, list)
        and len(actual_top_drops) == len(expected_top_drops),
        "diagnostic top dropped links count mismatch",
    )
    for actual, expected in zip(actual_top_drops, expected_top_drops):
        require(
            (
                actual["source_node_id"],
                actual["destination_node_id"],
                actual["output_interface"],
                actual["drop_packets"],
                actual["drop_bytes"],
                actual["first_drop_time_ns"],
                actual["last_drop_time_ns"],
            )
            == (
                *directed_link_key(expected),
                int_field(expected, "drop_packets"),
                int_field(expected, "drop_bytes"),
                int_field(expected, "first_drop_time_ns"),
                int_field(expected, "last_drop_time_ns"),
            ),
            "diagnostic top dropped link mismatch",
        )

    planned_rows = [
        row
        for row in concentration_rows
        if int_field(row, "planned_application_bytes") > 0
    ][:10]
    actual_top_planned = diagnostic.get("top_planned_load_links")
    require(
        isinstance(actual_top_planned, list)
        and len(actual_top_planned) == len(planned_rows),
        "diagnostic top planned links count mismatch",
    )
    for actual, expected in zip(actual_top_planned, planned_rows):
        require(
            (
                actual["source_node_id"],
                actual["destination_node_id"],
                actual["output_interface"],
                actual["unique_transfer_count"],
                actual["planned_application_bytes"],
                actual["large_transfer_count"],
                actual["adjacent_to_compute_node"],
                actual["drop_packets"],
                actual["drop_bytes"],
            )
            == (
                *directed_link_key(expected),
                int_field(expected, "unique_transfer_count"),
                int_field(expected, "planned_application_bytes"),
                int_field(expected, "large_transfer_count"),
                bool(int_field(expected, "adjacent_to_compute_node")),
                int_field(expected, "drop_packets"),
                int_field(expected, "drop_bytes"),
            ),
            "diagnostic top planned link mismatch",
        )

    require(
        flow_lost_packets > 0 or total_drop_packets > 0,
        "failure diagnostics contain no direct packet-loss evidence",
    )
    print(
        "PASS: incomplete run preserved strict diagnostics "
        f"({len(incomplete_task_rows)} tasks, "
        f"{len(incomplete_transfer_rows)} transfers, "
        f"{total_drop_packets} directed-queue drops)"
    )


def main():
    argv = sys.argv[1:]
    if argv[:1] == ["failure"]:
        parser = argparse.ArgumentParser(
            description="Validate one incomplete SatCompute task run."
        )
        parser.add_argument("--topology-dir", required=True)
        parser.add_argument("--compute-profile", required=True)
        parser.add_argument("--task-trace", required=True)
        parser.add_argument("--output-dir", required=True)
        args = parser.parse_args(argv[1:])
        validate_failure_diagnostics(
            args.output_dir,
            args.topology_dir,
            args.compute_profile,
            args.task_trace,
        )
        return

    if argv[:1] == ["run"]:
        parser = argparse.ArgumentParser(
            description="Validate one arbitrary SatCompute TaskTrace run."
        )
        parser.add_argument("--topology-dir", required=True)
        parser.add_argument("--compute-profile", required=True)
        parser.add_argument("--task-trace", required=True)
        parser.add_argument("--workload-summary", required=True)
        parser.add_argument("--output-dir", required=True)
        args = parser.parse_args(argv[1:])
        result = validate_scenario(
            args.output_dir,
            args.topology_dir,
            args.compute_profile,
            args.task_trace,
        )
        validate_workload_summary(
            args.workload_summary,
            args.task_trace,
            result["tasks"],
        )
        print(
            "PASS: generic task run "
            f"({len(result['tasks'])} tasks, "
            f"{len(result['plans'])} transfers)"
        )
        return

    parser = argparse.ArgumentParser(
        description="Check deterministic SatCompute task execution evidence."
    )
    parser.add_argument("--topology-dir", required=True)
    parser.add_argument("--single-output", required=True)
    parser.add_argument("--single-profile", required=True)
    parser.add_argument("--single-trace", required=True)
    parser.add_argument("--fcfs-output", required=True)
    parser.add_argument("--fcfs-profile", required=True)
    parser.add_argument("--fcfs-trace", required=True)
    parser.add_argument("--heterogeneous-output", required=True)
    parser.add_argument("--heterogeneous-profile", required=True)
    parser.add_argument("--heterogeneous-trace", required=True)
    parser.add_argument("--task-order-first-output", required=True)
    parser.add_argument("--task-order-second-output", required=True)
    parser.add_argument("--task-order-profile", required=True)
    parser.add_argument("--task-order-first-trace", required=True)
    parser.add_argument("--task-order-second-trace", required=True)
    parser.add_argument("--profile-order-first-output", required=True)
    parser.add_argument("--profile-order-second-output", required=True)
    parser.add_argument("--profile-order-first-profile", required=True)
    parser.add_argument("--profile-order-second-profile", required=True)
    parser.add_argument("--profile-order-trace", required=True)
    args = parser.parse_args(argv)

    single = validate_scenario(
        args.single_output,
        args.topology_dir,
        args.single_profile,
        args.single_trace,
    )
    validate_single(single)
    fcfs = validate_scenario(
        args.fcfs_output,
        args.topology_dir,
        args.fcfs_profile,
        args.fcfs_trace,
    )
    validate_fcfs(fcfs)
    heterogeneous = validate_scenario(
        args.heterogeneous_output,
        args.topology_dir,
        args.heterogeneous_profile,
        args.heterogeneous_trace,
    )
    validate_heterogeneous(heterogeneous)

    task_order_first = validate_scenario(
        args.task_order_first_output,
        args.topology_dir,
        args.task_order_profile,
        args.task_order_first_trace,
    )
    task_order_second = validate_scenario(
        args.task_order_second_output,
        args.topology_dir,
        args.task_order_profile,
        args.task_order_second_trace,
    )
    validate_determinism(
        args.task_order_first_output,
        args.task_order_second_output,
        task_order_first,
        task_order_second,
        "task_trace_path",
        "TaskTrace array reordering",
    )

    profile_order_first = validate_scenario(
        args.profile_order_first_output,
        args.topology_dir,
        args.profile_order_first_profile,
        args.profile_order_trace,
    )
    profile_order_second = validate_scenario(
        args.profile_order_second_output,
        args.topology_dir,
        args.profile_order_second_profile,
        args.profile_order_trace,
    )
    validate_determinism(
        args.profile_order_first_output,
        args.profile_order_second_output,
        profile_order_first,
        profile_order_second,
        "compute_profile_path",
        "ComputeProfile array reordering",
    )


if __name__ == "__main__":
    main()
