#!/usr/bin/env python3
"""Validate SatCompute's diagnostic FlowMonitor DropReason output."""

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


DROP_FIELDS = [
    "flow_monitor_id",
    "transfer_id",
    "source_address",
    "destination_address",
    "protocol",
    "source_port",
    "destination_port",
    "reason_code",
    "reason_name",
    "dropped_packets",
    "dropped_bytes",
    "flow_lost_packets",
    "flow_reported_drop_packets",
    "flow_unattributed_lost_packets",
]
REASON_NAMES = {
    -1: "UNATTRIBUTED_TIMEOUT",
    0: "NO_ROUTE",
    1: "TTL_EXPIRE",
    2: "BAD_CHECKSUM",
    3: "QUEUE",
    4: "QUEUE_DISC",
    5: "INTERFACE_DOWN",
    6: "ROUTE_ERROR",
    7: "FRAGMENT_TIMEOUT",
    8: "INVALID_REASON",
}


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def require(condition, message):
    if not condition:
        fail(message)


def int_field(row, field):
    try:
        return int(row[field])
    except (KeyError, ValueError) as error:
        fail(f"invalid integer field {field}: {error}")


def read_rows(path, expected_fields=None):
    require(path.is_file(), f"missing output file: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if expected_fields is not None:
            require(reader.fieldnames == expected_fields, f"{path.name} header mismatch")
        return list(reader)


def read_json(path):
    require(path.is_file(), f"missing output file: {path}")
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def flow_tuple(row):
    return (
        row["source_address"],
        row["destination_address"],
        int_field(row, "protocol"),
        int_field(row, "source_port"),
        int_field(row, "destination_port"),
    )


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument(
        "--require-reason",
        action="append",
        default=[],
        choices=sorted(REASON_NAMES.values()),
    )
    parser.add_argument(
        "--forbid-reason",
        action="append",
        default=[],
        choices=sorted(REASON_NAMES.values()),
    )
    parser.add_argument("--require-zero-unattributed", action="store_true")
    parser.add_argument("--minimum-explicit-drop-packets", type=int, default=1)
    parser.add_argument("--expected-explicit-drop-packets", type=int)
    parser.add_argument(
        "--expect-transfer-drop",
        action="append",
        default=[],
        metavar="TRANSFER_ID:REASON_NAME:PACKETS",
    )
    return parser.parse_args()


def parse_expected_transfer_drop(value):
    fields = value.split(":")
    require(
        len(fields) == 3,
        f"invalid expected transfer drop {value!r}",
    )
    try:
        transfer_id = int(fields[0])
        packets = int(fields[2])
    except ValueError as error:
        fail(f"invalid expected transfer drop {value!r}: {error}")
    require(transfer_id > 0, "expected transfer ID must be positive")
    require(
        fields[1] in REASON_NAMES.values(),
        f"unknown expected reason name {fields[1]!r}",
    )
    require(packets > 0, "expected transfer drop packets must be positive")
    return transfer_id, fields[1], packets


def main():
    args = parse_arguments()
    require(
        args.minimum_explicit_drop_packets >= 0,
        "minimum explicit drop packets must be non-negative",
    )
    if args.expected_explicit_drop_packets is not None:
        require(
            args.expected_explicit_drop_packets >= 0,
            "expected explicit drop packets must be non-negative",
        )
    expected_transfer_drops = [
        parse_expected_transfer_drop(value)
        for value in args.expect_transfer_drop
    ]
    output_dir = Path(args.output_dir)
    detail_rows = read_rows(output_dir / "network-flow-details.csv")
    positive_detail_rows = [
        row
        for row in detail_rows
        if int_field(row, "flow_monitor_id") > 0
    ]
    details_by_flow = {
        int_field(row, "flow_monitor_id"): row
        for row in positive_detail_rows
    }
    require(
        len(details_by_flow) == len(positive_detail_rows),
        "duplicate positive flow ID",
    )

    drop_rows = read_rows(output_dir / "flow-drop-reasons.csv", DROP_FIELDS)
    rows_by_flow = defaultdict(list)
    observed_reasons = set()
    seen_flow_reasons = set()
    explicit_drop_packets = 0
    unattributed_packets = 0
    transfer_reason_packets = defaultdict(int)
    reason_drop_bytes = defaultdict(int)

    for row in drop_rows:
        flow_id = int_field(row, "flow_monitor_id")
        require(flow_id in details_by_flow, f"unknown flow ID {flow_id}")
        detail = details_by_flow[flow_id]
        reason_code = int_field(row, "reason_code")
        require(reason_code in REASON_NAMES, f"unknown reason code {reason_code}")
        require(
            row["reason_name"] == REASON_NAMES[reason_code],
            f"reason name mismatch for code {reason_code}",
        )
        require(
            (flow_id, reason_code) not in seen_flow_reasons,
            f"duplicate reason {reason_code} for flow {flow_id}",
        )
        seen_flow_reasons.add((flow_id, reason_code))
        require(
            int_field(row, "transfer_id") == int_field(detail, "transfer_id"),
            f"transfer ID mismatch for flow {flow_id}",
        )
        require(
            flow_tuple(row) == flow_tuple(detail),
            f"five-tuple mismatch for flow {flow_id}",
        )

        dropped_packets = int_field(row, "dropped_packets")
        dropped_bytes = int_field(row, "dropped_bytes")
        require(dropped_packets > 0, f"flow {flow_id} has an empty drop row")
        require(dropped_bytes >= 0, f"flow {flow_id} has negative dropped bytes")
        if reason_code >= 0:
            require(dropped_bytes > 0, f"flow {flow_id} explicit drop has no bytes")
            explicit_drop_packets += dropped_packets
            transfer_reason_packets[
                (int_field(row, "transfer_id"), row["reason_name"])
            ] += dropped_packets
            reason_drop_bytes[row["reason_name"]] += dropped_bytes
        else:
            require(dropped_bytes == 0, "unattributed drop bytes must remain unknown")
            unattributed_packets += dropped_packets
        observed_reasons.add(row["reason_name"])
        rows_by_flow[flow_id].append(row)

    for flow_id, detail in details_by_flow.items():
        rows = rows_by_flow[flow_id]
        lost_packets = int_field(detail, "lost_packets")
        explicit = sum(
            int_field(row, "dropped_packets")
            for row in rows
            if int_field(row, "reason_code") >= 0
        )
        unattributed = max(0, lost_packets - explicit)
        unattributed_rows = [
            row for row in rows if int_field(row, "reason_code") == -1
        ]
        require(
            len(unattributed_rows) == (1 if unattributed > 0 else 0),
            f"flow {flow_id} unattributed row mismatch",
        )
        for row in rows:
            require(
                int_field(row, "flow_lost_packets") == lost_packets,
                f"flow {flow_id} lost packet context mismatch",
            )
            require(
                int_field(row, "flow_reported_drop_packets") == explicit,
                f"flow {flow_id} reported drop context mismatch",
            )
            require(
                int_field(row, "flow_unattributed_lost_packets") == unattributed,
                f"flow {flow_id} unattributed context mismatch",
            )
        if unattributed_rows:
            require(
                int_field(unattributed_rows[0], "dropped_packets") == unattributed,
                f"flow {flow_id} unattributed packet count mismatch",
            )

    run = read_json(output_dir / "run-summary.json")
    total_lost_packets = sum(
        int_field(row, "lost_packets") for row in details_by_flow.values()
    )
    require(
        run.get("flow_monitor_lost_packets") == total_lost_packets,
        "run summary FlowMonitor lost count mismatch",
    )
    require(
        run.get("flow_monitor_reported_drop_packets") == explicit_drop_packets,
        "run summary reported drop count mismatch",
    )
    require(
        run.get("flow_monitor_unattributed_lost_packets")
        == unattributed_packets,
        "run summary unattributed loss count mismatch",
    )
    run_drop_reasons = run.get("flow_monitor_drop_reasons")
    require(
        isinstance(run_drop_reasons, list)
        and len(run_drop_reasons) == len(REASON_NAMES) - 1,
        "run summary DropReason list mismatch",
    )
    run_reason_packets = {}
    for reason in run_drop_reasons:
        require(
            isinstance(reason, dict)
            and set(reason)
            == {
                "reason_code",
                "reason_name",
                "dropped_packets",
                "dropped_bytes",
            },
            "run summary DropReason object mismatch",
        )
        reason_code = reason["reason_code"]
        require(
            reason_code in REASON_NAMES and reason_code >= 0,
            f"run summary contains unknown reason code {reason_code}",
        )
        require(
            reason["reason_name"] == REASON_NAMES[reason_code],
            f"run summary reason name mismatch for code {reason_code}",
        )
        require(
            isinstance(reason["dropped_packets"], int)
            and reason["dropped_packets"] >= 0
            and isinstance(reason["dropped_bytes"], int)
            and reason["dropped_bytes"] >= 0,
            f"run summary reason counts invalid for code {reason_code}",
        )
        require(
            reason_code not in run_reason_packets,
            f"run summary duplicate reason code {reason_code}",
        )
        run_reason_packets[reason_code] = reason["dropped_packets"]
    for reason_code, reason_name in REASON_NAMES.items():
        if reason_code < 0:
            continue
        require(
            run_reason_packets[reason_code]
            == transfer_reason_packets.get((0, reason_name), 0)
            + sum(
                packets
                for (transfer_id, observed_reason), packets
                in transfer_reason_packets.items()
                if transfer_id > 0 and observed_reason == reason_name
            ),
            f"run summary aggregate mismatch for {reason_name}",
        )
        require(
            next(
                reason["dropped_bytes"]
                for reason in run_drop_reasons
                if reason["reason_code"] == reason_code
            )
            == reason_drop_bytes[reason_name],
            f"run summary byte aggregate mismatch for {reason_name}",
        )

    require(
        explicit_drop_packets >= args.minimum_explicit_drop_packets,
        "explicit drop count is below the requested minimum",
    )
    if args.expected_explicit_drop_packets is not None:
        require(
            explicit_drop_packets == args.expected_explicit_drop_packets,
            "explicit drop count does not match the expected value",
        )
    for transfer_id, reason_name, packets in expected_transfer_drops:
        require(
            transfer_reason_packets[(transfer_id, reason_name)] == packets,
            f"transfer {transfer_id} {reason_name} drop count mismatch",
        )
    for reason in args.require_reason:
        require(reason in observed_reasons, f"required reason not observed: {reason}")
    for reason in args.forbid_reason:
        require(reason not in observed_reasons, f"forbidden reason observed: {reason}")
    if args.require_zero_unattributed:
        require(unattributed_packets == 0, "unattributed losses were observed")

    print(
        "PASS: FlowMonitor DropReason evidence is internally consistent "
        f"({explicit_drop_packets} explicit drops, "
        f"{unattributed_packets} unattributed losses)"
    )


if __name__ == "__main__":
    main()
