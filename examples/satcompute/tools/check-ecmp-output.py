#!/usr/bin/env python3

import argparse
import csv
import json
import re
from pathlib import Path


DETAILS_FILE = "network-flow-details.csv"
EVENTS_FILE = "ecmp-route-events.csv"
AGGREGATE_FILE = "network-flow-metrics.csv"
TRANSFER_FILE = "transfer-summary.csv"
RUN_FILE = "run-summary.json"
TRANSFER_FIELDS = {
    "transfer_id",
    "source_node_id",
    "destination_node_id",
    "size_bytes",
    "arrival_time_ns",
}


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def require(condition, message):
    if not condition:
        fail(message)


def read_rows(directory, filename):
    path = Path(directory) / filename
    require(path.is_file(), f"missing {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def read_json(path):
    path = Path(path)
    require(path.is_file(), f"missing {path}")
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def read_run_summary(directory):
    return read_json(Path(directory) / RUN_FILE)


def int_field(row, name):
    try:
        return int(row[name])
    except (KeyError, ValueError) as error:
        fail(f"invalid integer field {name}: {error}")


def flow_key(row):
    return (
        row["source_address"],
        row["destination_address"],
        int_field(row, "protocol"),
        int_field(row, "source_port"),
        int_field(row, "destination_port"),
    )


def validate_transfer_rows(directory, expected_ids):
    rows = read_rows(directory, DETAILS_FILE)
    transfer_rows = [row for row in rows if int_field(row, "transfer_id") > 0]
    require(
        {int_field(row, "transfer_id") for row in transfer_rows}
        == set(expected_ids),
        "transfer IDs do not match the expected fixture IDs",
    )
    require(
        len({flow_key(row) for row in transfer_rows}) == len(transfer_rows),
        "transfer five-tuples must be unique",
    )
    for row in transfer_rows:
        transfer_id = int_field(row, "transfer_id")
        require(
            int_field(row, "received_application_payload_bytes")
            == int_field(row, "planned_application_payload_bytes"),
            f"transfer {transfer_id} application payload mismatch",
        )
        require(
            int_field(row, "tx_packets") == int_field(row, "rx_packets")
            and int_field(row, "tx_packets") > 0,
            f"transfer {transfer_id} packet delivery mismatch",
        )
        require(
            int_field(row, "lost_packets") == 0,
            f"transfer {transfer_id} reports lost packets",
        )
    return transfer_rows


def validate_unique_route_events(rows):
    seen = set()
    for row in rows:
        key = (
            int_field(row, "route_epoch"),
            int_field(row, "node_id"),
            flow_key(row),
        )
        require(key not in seen, f"duplicate route decision for {key}")
        seen.add(key)


def source_events(directory):
    rows = read_rows(directory, EVENTS_FILE)
    validate_unique_route_events(rows)
    return [
        row
        for row in rows
        if int_field(row, "node_id") == 0
        and int_field(row, "protocol") == 17
        and int_field(row, "destination_port") == 9000
    ]


def validate_no_fragmentation_fallback(directory):
    rows = read_rows(directory, EVENTS_FILE)
    invalid = [
        row
        for row in rows
        if row["selection_reason"] == "BASE_FALLBACK_NO_FIVE_TUPLE"
    ]
    require(
        not invalid,
        "global-hash-per-flow emitted BASE_FALLBACK_NO_FIVE_TUPLE",
    )


def validate_static(first, second):
    expected_ids = range(1, 5)
    transfer_rows = validate_transfer_rows(first, expected_ids)
    events = source_events(first)
    require(len(events) == 4, "static source must emit one event per transfer")
    require(
        {flow_key(row) for row in events}
        == {flow_key(row) for row in transfer_rows},
        "static route evidence does not match transfer five-tuples",
    )

    selected_gateways = set()
    selected_interfaces = set()
    for row in events:
        require(int_field(row, "route_epoch") == 0, "static epoch must be zero")
        require(
            int_field(row, "candidate_count_before_dedup") == 2
            and int_field(row, "candidate_count_after_dedup") == 2,
            "static source must expose two deduplicated candidates",
        )
        require(
            row["selection_reason"] == "HASH_PER_FLOW",
            "static source decision must use HASH_PER_FLOW",
        )
        selected_gateways.add(row["selected_gateway"])
        selected_interfaces.add(int_field(row, "selected_output_interface"))
    require(
        len(selected_gateways) == 2 and len(selected_interfaces) == 2,
        "static transfers must cover both equal-cost branches",
    )

    validate_transfer_rows(second, expected_ids)
    source_events(second)
    for filename in (EVENTS_FILE, DETAILS_FILE, AGGREGATE_FILE, TRANSFER_FILE):
        first_bytes = (Path(first) / filename).read_bytes()
        second_bytes = (Path(second) / filename).read_bytes()
        require(first_bytes == second_bytes, f"repeat output differs: {filename}")
    validate_no_fragmentation_fallback(first)
    validate_no_fragmentation_fallback(second)
    print("PASS: static diamond payload, branch coverage, and repeat determinism")


def validate_dynamic(directory):
    expected_ids = range(1, 13)
    transfer_rows = validate_transfer_rows(directory, expected_ids)
    events = source_events(directory)
    require(len(events) == 12, "dynamic source must emit one event per transfer")
    require(
        {flow_key(row) for row in events}
        == {flow_key(row) for row in transfer_rows},
        "dynamic route evidence does not match transfer five-tuples",
    )

    events_by_epoch = {}
    for row in events:
        events_by_epoch.setdefault(int_field(row, "route_epoch"), []).append(row)

    require(
        set(events_by_epoch) == {0, 1, 2}
        and all(len(rows) == 4 for rows in events_by_epoch.values()),
        "dynamic fixture must emit four transfers in each route epoch",
    )

    for epoch in (0, 2):
        selected_gateways = set()
        selected_interfaces = set()
        for row in events_by_epoch[epoch]:
            require(
                int_field(row, "candidate_count_before_dedup") == 2
                and int_field(row, "candidate_count_after_dedup") == 2,
                f"epoch {epoch} transfer must see two candidates",
            )
            require(
                row["selection_reason"] == "HASH_PER_FLOW",
                f"epoch {epoch} transfer must use HASH_PER_FLOW",
            )
            selected_gateways.add(row["selected_gateway"])
            selected_interfaces.add(int_field(row, "selected_output_interface"))
        require(
            len(selected_gateways) == 2 and len(selected_interfaces) == 2,
            f"epoch {epoch} transfers must cover both restored branches",
        )

    epoch_one_gateways = set()
    for row in events_by_epoch[1]:
        require(
            int_field(row, "candidate_count_before_dedup") == 1
            and int_field(row, "candidate_count_after_dedup") == 1,
            "epoch 1 transfer must see one candidate",
        )
        require(
            row["selection_reason"] == "SINGLE_CANDIDATE",
            "epoch 1 transfer must use SINGLE_CANDIDATE",
        )
        epoch_one_gateways.add(row["selected_gateway"])

    require(
        len(epoch_one_gateways) == 1,
        "all epoch-1 traffic must use the single remaining branch",
    )
    validate_no_fragmentation_fallback(directory)
    print("PASS: dynamic diamond epochs expose deterministic 2->1->2 candidates")


def read_transfer_input(path):
    root = read_json(path)
    require(
        isinstance(root, dict) and set(root) == {"schema_version", "transfers"},
        "transfer JSON root must contain only schema_version and transfers",
    )
    require(root["schema_version"] == "0.1", "transfer schema must be 0.1")
    require(isinstance(root["transfers"], list), "transfers must be an array")

    transfers = []
    for item in root["transfers"]:
        require(
            isinstance(item, dict) and set(item) == TRANSFER_FIELDS,
            "each transfer must contain exactly the five logical fields",
        )
        for field in TRANSFER_FIELDS:
            require(
                isinstance(item[field], int) and not isinstance(item[field], bool),
                f"{field} must be an integer",
            )
        require(item["transfer_id"] > 0, "transfer_id must be positive")
        require(item["size_bytes"] > 0, "size_bytes must be positive")
        require(item["arrival_time_ns"] >= 0, "arrival_time_ns must be non-negative")
        require(
            item["source_node_id"] != item["destination_node_id"],
            "source and destination must differ",
        )
        transfers.append(item)

    transfers.sort(key=lambda item: item["transfer_id"])
    require(
        len({item["transfer_id"] for item in transfers}) == len(transfers),
        "transfer IDs must be unique",
    )
    return transfers


def validate_transfer_contract(directory, input_path):
    transfers = read_transfer_input(input_path)
    run = read_run_summary(directory)
    require(
        isinstance(run["isl_queue_bytes"], int)
        and run["isl_queue_bytes"] > 0,
        "run ISL queue byte capacity must be positive",
    )
    chunk_mode = run["transfer_chunk_mode"]
    require(
        chunk_mode in {"fixed", "size-aware"},
        "run transfer chunk mode must be fixed or size-aware",
    )
    fixed_payload = run["fixed_payload_bytes"]
    if chunk_mode == "fixed":
        require(
            isinstance(fixed_payload, int) and fixed_payload > 0,
            "fixed mode requires a positive fixed payload",
        )
    else:
        require(
            fixed_payload is None,
            "size-aware mode must report null fixed payload",
        )

    summary_rows = read_rows(directory, TRANSFER_FILE)
    detail_rows = [
        row
        for row in read_rows(directory, DETAILS_FILE)
        if int_field(row, "transfer_id") > 0
    ]
    require(
        len(summary_rows) == len(transfers),
        "transfer-summary row count does not match JSON",
    )
    require(
        len(detail_rows) == len(transfers),
        "network-flow-details row count does not match JSON",
    )

    summaries = {
        int_field(row, "transfer_id"): row for row in summary_rows
    }
    details = {int_field(row, "transfer_id"): row for row in detail_rows}
    require(len(summaries) == len(summary_rows), "duplicate transfer summary ID")
    require(len(details) == len(detail_rows), "duplicate transfer detail ID")
    require(
        len({flow_key(row) for row in detail_rows}) == len(detail_rows),
        "transfer five-tuples must be unique",
    )

    total_bytes = 0
    total_packets = 0
    packet_counts = []
    for transfer in transfers:
        transfer_id = transfer["transfer_id"]
        require(transfer_id in summaries, f"missing transfer summary {transfer_id}")
        require(transfer_id in details, f"missing transfer detail {transfer_id}")
        summary = summaries[transfer_id]
        detail = details[transfer_id]
        payload = int_field(summary, "effective_payload_bytes")
        require(payload > 0, f"transfer {transfer_id} has invalid payload cap")
        declared = transfer["size_bytes"]
        expected_payload = fixed_payload
        if chunk_mode == "size-aware":
            if declared <= 1 << 20:
                expected_payload = 1024
            elif declared <= 64 << 20:
                expected_payload = 8192
            else:
                expected_payload = 64000
        require(
            payload == expected_payload,
            f"transfer {transfer_id} effective payload mismatch",
        )
        expected_packets = (declared + payload - 1) // payload
        expected_final = declared % payload or payload

        require(
            int_field(summary, "source_node_id") == transfer["source_node_id"]
            and int_field(summary, "destination_node_id")
            == transfer["destination_node_id"],
            f"transfer {transfer_id} endpoint mismatch",
        )
        require(
            int_field(summary, "declared_size_bytes") == declared,
            f"transfer {transfer_id} declared size mismatch",
        )
        require(
            int_field(summary, "arrival_time_ns") == transfer["arrival_time_ns"],
            f"transfer {transfer_id} arrival mismatch",
        )
        require(
            summary["source_address"] == detail["source_address"]
            and summary["destination_address"] == detail["destination_address"]
            and int_field(summary, "source_port")
            == int_field(detail, "source_port")
            and int_field(summary, "destination_port")
            == int_field(detail, "destination_port"),
            f"transfer {transfer_id} five-tuple metadata mismatch",
        )
        require(
            int_field(summary, "derived_packet_count") == expected_packets,
            f"transfer {transfer_id} derived packet count mismatch",
        )
        require(
            int_field(summary, "final_packet_payload_bytes") == expected_final,
            f"transfer {transfer_id} final payload mismatch",
        )
        require(
            summary["pacing_mode"] == "first-hop-serialization",
            f"transfer {transfer_id} pacing mode mismatch",
        )
        last_send_time = int_field(summary, "last_send_time_ns")
        require(
            last_send_time >= transfer["arrival_time_ns"],
            f"transfer {transfer_id} last send precedes arrival",
        )
        if expected_packets == 1:
            require(
                last_send_time == transfer["arrival_time_ns"],
                f"single-packet transfer {transfer_id} must send at arrival",
            )
        require(
            int_field(summary, "sent_application_bytes") == declared
            and int_field(summary, "received_application_bytes") == declared,
            f"transfer {transfer_id} application byte mismatch",
        )
        require(
            int_field(summary, "received_packet_count") == expected_packets,
            f"transfer {transfer_id} received packet count mismatch",
        )
        completion_time = int_field(summary, "completion_time_ns")
        require(completion_time >= 0, f"transfer {transfer_id} did not complete")
        require(
            completion_time >= last_send_time,
            f"transfer {transfer_id} completed before its last send",
        )
        require(
            int_field(summary, "completion_delay_ns")
            == completion_time - transfer["arrival_time_ns"],
            f"transfer {transfer_id} completion delay mismatch",
        )
        require(
            int_field(detail, "planned_application_payload_bytes") == declared
            and int_field(detail, "received_application_payload_bytes") == declared,
            f"transfer {transfer_id} flow payload mismatch",
        )
        require(
            int_field(detail, "tx_packets") == expected_packets
            and int_field(detail, "rx_packets") == expected_packets
            and int_field(detail, "lost_packets") == 0,
            f"transfer {transfer_id} FlowMonitor packet mismatch",
        )
        total_bytes += declared
        total_packets += expected_packets
        packet_counts.append(expected_packets)

    require(
        run["pacing_mode"] == "first-hop-serialization",
        "run pacing mode mismatch",
    )
    require(run["transfer_count"] == len(transfers), "run transfer count mismatch")
    require(
        run["declared_application_bytes"] == total_bytes
        and run["sent_application_bytes"] == total_bytes
        and run["received_application_bytes"] == total_bytes,
        "run application byte totals mismatch",
    )
    require(
        run["derived_udp_packets"] == total_packets,
        "run derived packet total mismatch",
    )
    require(
        run["flow_monitor_tx_packets"] == total_packets
        and run["flow_monitor_rx_packets"] == total_packets
        and run["flow_monitor_lost_packets"] == 0,
        "run FlowMonitor packet totals mismatch",
    )

    aggregate_rows = read_rows(directory, AGGREGATE_FILE)
    require(len(aggregate_rows) == 1, "aggregate CSV must contain one row")
    aggregate = aggregate_rows[0]
    require(
        int_field(aggregate, "tx_packets") == total_packets
        and int_field(aggregate, "rx_packets") == total_packets
        and int_field(aggregate, "lost_packets") == 0,
        "aggregate packet totals mismatch",
    )
    validate_no_fragmentation_fallback(directory)
    return transfers, summary_rows, detail_rows, packet_counts


def normalized_run_summary(directory):
    summary = read_run_summary(directory)
    summary.pop("wall_clock_s", None)
    return summary


def validate_repeat_outputs(first, second):
    for filename in (EVENTS_FILE, DETAILS_FILE, AGGREGATE_FILE, TRANSFER_FILE):
        require(
            (Path(first) / filename).read_bytes()
            == (Path(second) / filename).read_bytes(),
            f"repeat output differs: {filename}",
        )
    require(
        normalized_run_summary(first) == normalized_run_summary(second),
        "repeat run-summary differs outside wall_clock_s",
    )


def validate_canonical(first, second):
    validate_repeat_outputs(first, second)
    validate_no_fragmentation_fallback(first)
    validate_no_fragmentation_fallback(second)
    print("PASS: canonical input variants preserve deterministic output")


def validate_remainder(directory):
    rows = read_rows(directory, TRANSFER_FILE)
    matches = [row for row in rows if int_field(row, "declared_size_bytes") == 2050]
    require(len(matches) == 1, "expected exactly one 2050-byte transfer")
    row = matches[0]
    require(
        int_field(row, "effective_payload_bytes") == 1024
        and int_field(row, "derived_packet_count") == 3
        and int_field(row, "final_packet_payload_bytes") == 2,
        "2050-byte remainder must be 1024 + 1024 + 2",
    )
    require(
        int_field(row, "sent_application_bytes") == 2050
        and int_field(row, "received_application_bytes") == 2050,
        "2050-byte transfer payload mismatch",
    )
    validate_no_fragmentation_fallback(directory)
    print("PASS: 2050-byte remainder is 1024 + 1024 + 2")


def validate_scale(directory, input_path, log_path, second=None):
    transfers, _, details, packet_counts = validate_transfer_contract(
        directory, input_path
    )
    require(len(transfers) == 5000, "scale input must contain 5000 transfers")
    require(
        [item["transfer_id"] for item in transfers] == list(range(1, 5001)),
        "scale transfer IDs must be exactly 1..5000",
    )
    require(
        len({item["size_bytes"] for item in transfers}) == 5000,
        "all 5000 scale transfer sizes must differ",
    )
    require(
        set(packet_counts) == set(range(1, 21)),
        "scale workload must cover 1..20 derived packets",
    )
    require(
        len({flow_key(row) for row in details}) == 5000,
        "scale workload must expose 5000 unique five-tuples",
    )
    log_text = Path(log_path).read_text(encoding="utf-8")
    require(
        re.search(r"^\[TRANSFER:\d+\]", log_text, re.MULTILINE) is None,
        "summary log contains per-transfer detail blocks",
    )
    if second:
        validate_transfer_contract(second, input_path)
        validate_repeat_outputs(directory, second)
    print("PASS: 5000 distinct-size transfers, 1..20 packets, zero loss")


def validate_large(directory, input_path):
    transfers, _, _, packet_counts = validate_transfer_contract(
        directory, input_path
    )
    require(len(transfers) > 1, "varied multi-packet input needs multiple flows")
    require(
        len({item["size_bytes"] for item in transfers}) == len(transfers),
        "varied multi-packet sizes must differ",
    )
    require(max(packet_counts) >= 20, "expected at least one 20-packet transfer")
    require(min(packet_counts) > 1, "every varied-large transfer must be multi-packet")
    print("PASS: varied-size multi-packet transfers complete without fragmentation")


def main():
    parser = argparse.ArgumentParser(
        description="Check deterministic SatCompute ECMP output."
    )
    parser.add_argument("--first", help="first static diamond output directory")
    parser.add_argument("--second", help="second static diamond output directory")
    parser.add_argument("--dynamic", help="dynamic diamond output directory")
    parser.add_argument("--canonical-first")
    parser.add_argument("--canonical-second")
    parser.add_argument("--remainder", help="remainder output directory")
    parser.add_argument("--scale", help="5000-transfer output directory")
    parser.add_argument("--scale-second", help="repeat scale output directory")
    parser.add_argument("--scale-input", help="5000-transfer JSON input")
    parser.add_argument("--scale-log", help="5000-transfer stdout log")
    parser.add_argument("--large", help="varied multi-packet output directory")
    parser.add_argument("--large-input", help="varied multi-packet JSON input")
    args = parser.parse_args()

    require(
        bool(args.first) == bool(args.second),
        "--first and --second must be provided together",
    )
    require(
        bool(args.canonical_first) == bool(args.canonical_second),
        "--canonical-first and --canonical-second must be provided together",
    )
    require(
        not args.scale or (args.scale_input and args.scale_log),
        "--scale requires --scale-input and --scale-log",
    )
    require(
        not args.scale_second or args.scale,
        "--scale-second requires --scale",
    )
    require(
        not args.large or args.large_input,
        "--large requires --large-input",
    )
    require(
        any(
            (
                args.first,
                args.dynamic,
                args.canonical_first,
                args.remainder,
                args.scale,
                args.large,
            )
        ),
        "provide at least one output mode",
    )
    if args.first:
        validate_static(args.first, args.second)
    if args.dynamic:
        validate_dynamic(args.dynamic)
    if args.canonical_first:
        validate_canonical(args.canonical_first, args.canonical_second)
    if args.remainder:
        validate_remainder(args.remainder)
    if args.scale:
        validate_scale(
            args.scale,
            args.scale_input,
            args.scale_log,
            args.scale_second,
        )
    if args.large:
        validate_large(args.large, args.large_input)


if __name__ == "__main__":
    main()
