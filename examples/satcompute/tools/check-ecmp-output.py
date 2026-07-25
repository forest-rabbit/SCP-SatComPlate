#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path


DETAILS_FILE = "network-flow-details.csv"
EVENTS_FILE = "ecmp-route-events.csv"
AGGREGATE_FILE = "network-flow-metrics.csv"


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


def validate_transfer_rows(directory):
    rows = read_rows(directory, DETAILS_FILE)
    transfer_rows = [row for row in rows if int_field(row, "transfer_id") > 0]
    require(len(transfer_rows) == 4, "expected exactly four transfer flows")
    require(
        {int_field(row, "transfer_id") for row in transfer_rows}
        == {1, 2, 3, 4},
        "transfer IDs must be 1, 2, 3, and 4",
    )
    require(
        len({flow_key(row) for row in transfer_rows}) == 4,
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


def validate_static(first, second):
    transfer_rows = validate_transfer_rows(first)
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

    validate_transfer_rows(second)
    source_events(second)
    for filename in (EVENTS_FILE, DETAILS_FILE, AGGREGATE_FILE):
        first_bytes = (Path(first) / filename).read_bytes()
        second_bytes = (Path(second) / filename).read_bytes()
        require(first_bytes == second_bytes, f"repeat output differs: {filename}")
    print("PASS: static diamond payload, branch coverage, and repeat determinism")


def validate_dynamic(directory):
    transfer_rows = validate_transfer_rows(directory)
    events = source_events(directory)
    events_by_flow = {}
    for row in events:
        events_by_flow.setdefault(flow_key(row), {})[
            int_field(row, "route_epoch")
        ] = row

    require(
        set(events_by_flow) == {flow_key(row) for row in transfer_rows},
        "dynamic route evidence does not match transfer five-tuples",
    )
    require(
        all(set(by_epoch) == {0, 1, 2} for by_epoch in events_by_flow.values()),
        "every dynamic flow must have route decisions in epochs 0, 1, and 2",
    )

    epoch_one_gateways = set()
    changed_to_remaining_path = False
    for key, by_epoch in events_by_flow.items():
        initial = by_epoch[0]
        failed = by_epoch[1]
        restored = by_epoch[2]
        require(
            int_field(initial, "candidate_count_before_dedup") == 2
            and int_field(initial, "candidate_count_after_dedup") == 2
            and int_field(restored, "candidate_count_before_dedup") == 2
            and int_field(restored, "candidate_count_after_dedup") == 2,
            f"flow {key} must see two candidates before and after restoration",
        )
        require(
            int_field(failed, "candidate_count_before_dedup") == 1
            and int_field(failed, "candidate_count_after_dedup") == 1,
            f"flow {key} must see one candidate during branch failure",
        )
        require(
            initial["selection_reason"] == "HASH_PER_FLOW"
            and failed["selection_reason"] == "SINGLE_CANDIDATE"
            and restored["selection_reason"] == "HASH_PER_FLOW",
            f"flow {key} has incorrect dynamic selection reasons",
        )
        require(
            initial["selected_gateway"] == restored["selected_gateway"]
            and int_field(initial, "selected_output_interface")
            == int_field(restored, "selected_output_interface")
            and int_field(initial, "hash_value")
            == int_field(restored, "hash_value"),
            f"flow {key} did not restore its deterministic selection",
        )
        epoch_one_gateways.add(failed["selected_gateway"])
        changed_to_remaining_path |= (
            initial["selected_gateway"] != failed["selected_gateway"]
        )

    require(
        len(epoch_one_gateways) == 1,
        "all epoch-1 traffic must use the single remaining branch",
    )
    require(
        changed_to_remaining_path,
        "no flow demonstrated rerouting away from the failed branch",
    )
    print("PASS: dynamic diamond epochs 2->1->2 and deterministic restoration")


def main():
    parser = argparse.ArgumentParser(
        description="Check deterministic SatCompute ECMP output."
    )
    parser.add_argument("--first", help="first static diamond output directory")
    parser.add_argument("--second", help="second static diamond output directory")
    parser.add_argument("--dynamic", help="dynamic diamond output directory")
    args = parser.parse_args()

    require(
        bool(args.first) == bool(args.second),
        "--first and --second must be provided together",
    )
    require(args.first or args.dynamic, "provide static or dynamic output")
    if args.first:
        validate_static(args.first, args.second)
    if args.dynamic:
        validate_dynamic(args.dynamic)


if __name__ == "__main__":
    main()
