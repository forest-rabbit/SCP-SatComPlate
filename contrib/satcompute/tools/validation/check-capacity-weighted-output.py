#!/usr/bin/env python3
"""Validate capacity-normalized HRW selection and dynamic path recovery."""

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


def source_assignments(directory, epoch):
    rows = [
        row
        for row in read_rows(directory, "size-aware-reservation-events.csv")
        if row["action"] == "ASSIGN"
        and int_field(row, "node_id") == 0
        and int_field(row, "route_epoch") == epoch
    ]
    result = {
        int_field(row, "transfer_id"): row
        for row in rows
    }
    require(len(result) == len(rows), f"duplicate source assignment in epoch {epoch}")
    return result


def validate_size_baseline(directory):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == "global-size-aware-hrw", "baseline mode mismatch")
    require(run["pacing_mode"] == "first-hop-serialization", "baseline pacing mismatch")

    initial = source_assignments(directory, 0)
    require(set(initial) == {1, 2, 3}, "baseline initial transfer coverage changed")
    require(
        initial[2]["candidate_gateway"] == initial[3]["candidate_gateway"],
        "raw-byte baseline no longer groups transfers 2 and 3",
    )
    require(
        initial[1]["candidate_gateway"] != initial[3]["candidate_gateway"],
        "baseline no longer separates the initially loaded fast candidate",
    )
    require(
        initial[3]["selection_reason"] == "SIZE_AWARE_HRW_PRIMARY",
        "baseline comparison flow no longer keeps its HRW primary",
    )


def validate_complete_transfers(directory):
    transfers = read_rows(directory, "transfer-summary.csv")
    require(len(transfers) == 4, "weighted transfer row count mismatch")
    expected_bytes = {1: 1_280_000, 2: 640_000, 3: 640_000, 4: 640_000}
    expected_packets = {1: 20, 2: 10, 3: 10, 4: 10}
    seen = set()
    for row in transfers:
        transfer_id = int_field(row, "transfer_id")
        require(transfer_id in expected_bytes, f"unexpected transfer {transfer_id}")
        require(transfer_id not in seen, f"duplicate transfer {transfer_id}")
        seen.add(transfer_id)
        require(
            int_field(row, "declared_size_bytes") == expected_bytes[transfer_id],
            f"transfer {transfer_id} declared bytes changed",
        )
        require(
            int_field(row, "sent_application_bytes") == expected_bytes[transfer_id]
            and int_field(row, "received_application_bytes") == expected_bytes[transfer_id],
            f"transfer {transfer_id} did not complete byte-exactly",
        )
        require(
            int_field(row, "derived_packet_count") == expected_packets[transfer_id]
            and int_field(row, "received_packet_count") == expected_packets[transfer_id],
            f"transfer {transfer_id} packet count mismatch",
        )
        require(int_field(row, "completion_time_ns") > 0, f"transfer {transfer_id} incomplete")
        require(row["pacing_mode"] == "first-hop-serialization", "weighted pacing changed")
    require(seen == set(expected_bytes), "weighted transfer IDs changed")


def validate_weighted(directory):
    run = read_json(directory, "run-summary.json")
    require(run["mode"] == "network-transfer", "weighted run mode mismatch")
    require(run["run_status"] == "COMPLETE", "weighted run status mismatch")
    require(
        run["routing_mode"] == "global-capacity-weighted-hrw",
        "weighted routing mode mismatch",
    )
    require(run["pacing_mode"] == "first-hop-serialization", "weighted pacing mismatch")
    require(run["transfer_count"] == 4, "weighted transfer count mismatch")
    require(run["declared_application_bytes"] == 3_200_000, "declared bytes changed")
    require(run["sent_application_bytes"] == 3_200_000, "sent bytes mismatch")
    require(run["received_application_bytes"] == 3_200_000, "received bytes mismatch")
    require(run["derived_udp_packets"] == 50, "derived packet count changed")
    require(run["flow_monitor_tx_packets"] == 50, "FlowMonitor tx count changed")
    require(run["flow_monitor_rx_packets"] == 50, "FlowMonitor rx count changed")
    require(run["flow_monitor_lost_packets"] == 0, "weighted run lost packets")
    require(run["flow_monitor_reported_drop_packets"] == 0, "weighted run reports drops")
    require(
        run["flow_monitor_unattributed_lost_packets"] == 0,
        "weighted run has unattributed loss",
    )
    require(run["udp_socket_drop_packets"] == 0, "weighted run has UDP socket drops")
    require(
        all(item["dropped_packets"] == 0 for item in run["flow_monitor_drop_reasons"]),
        "weighted run has a nonzero FlowMonitor drop reason",
    )
    validate_complete_transfers(directory)
    require(
        not read_rows(directory, "diagnostics/failure/flow-drop-reasons.csv"),
        "weighted run produced failure drop rows",
    )

    events = read_rows(directory, "size-aware-reservation-events.csv")
    initial = source_assignments(directory, 0)
    require(set(initial) == {1, 2, 3}, "weighted initial transfer coverage changed")
    failed_gateway = initial[1]["candidate_gateway"]
    require(
        initial[3]["candidate_gateway"] == failed_gateway
        and initial[3]["selection_reason"] == "CAPACITY_WEIGHTED_HRW_SECONDARY",
        "capacity normalization no longer selects the loaded high-rate HRW secondary",
    )

    invalid_source_rows = [
        row
        for row in events
        if row["action"] == "RELEASE_CANDIDATE_INVALID"
        and int_field(row, "node_id") == 0
        and int_field(row, "route_epoch") == 1
    ]
    invalid_source = {
        int_field(row, "transfer_id"): row
        for row in invalid_source_rows
    }
    require(len(invalid_source) == len(invalid_source_rows), "duplicate source invalidation")
    require(set(invalid_source) == {1, 3}, "source invalidation coverage changed")
    require(
        {row["candidate_gateway"] for row in invalid_source.values()} == {failed_gateway},
        "source did not release the failed next hop",
    )
    migrated = source_assignments(directory, 1)
    require(set(migrated) == {1, 3}, "source migration assignment coverage changed")
    require(
        all(row["candidate_gateway"] != failed_gateway for row in migrated.values()),
        "source migration reused the unavailable next hop",
    )

    intermediate_invalid = [
        row
        for row in events
        if row["action"] == "RELEASE_CANDIDATE_INVALID"
        and int_field(row, "node_id") == 1
        and int_field(row, "route_epoch") == 1
    ]
    require(len(intermediate_invalid) == 1, "intermediate path invalidation changed")
    intermediate_reassign = [
        row
        for row in events
        if row["action"] == "ASSIGN"
        and int_field(row, "node_id") == 1
        and int_field(row, "route_epoch") == 1
        and row["transfer_id"] == intermediate_invalid[0]["transfer_id"]
    ]
    require(len(intermediate_reassign) == 1, "intermediate packet was not rerouted")
    require(
        intermediate_reassign[0]["candidate_gateway"]
        != intermediate_invalid[0]["candidate_gateway"],
        "intermediate reroute reused the failed downstream link",
    )

    recovered = source_assignments(directory, 2)
    require(set(recovered) == {4}, "recovered-path new-flow assignment changed")
    require(
        recovered[4]["candidate_gateway"] == failed_gateway
        and recovered[4]["selection_reason"] == "CAPACITY_WEIGHTED_HRW_SECONDARY",
        "new flow did not reuse the restored high-rate path",
    )
    epoch_two_sticky = {
        int_field(row, "transfer_id"): row["candidate_gateway"]
        for row in events
        if row["action"] == "STICKY_REUSE"
        and int_field(row, "node_id") == 0
        and int_field(row, "route_epoch") == 2
    }
    require(set(epoch_two_sticky) == {1, 2, 3}, "existing flow sticky coverage changed")
    require(
        all(epoch_two_sticky[transfer_id] != failed_gateway for transfer_id in (1, 3)),
        "migrated flow moved back when the failed path recovered",
    )

    route_events = read_rows(directory, "ecmp-route-events.csv")
    source_candidate_counts = {}
    for row in route_events:
        if int_field(row, "node_id") != 0:
            continue
        epoch = int_field(row, "route_epoch")
        source_candidate_counts.setdefault(epoch, set()).add(
            int_field(row, "candidate_count_after_dedup")
        )
    require(
        source_candidate_counts == {0: {3}, 1: {2}, 2: {3}},
        "dynamic ECMP candidates changed",
    )

    actions = Counter(row["action"] for row in events)
    summary = read_json(directory, "size-aware-summary.json")
    require(actions["RELEASE_CANDIDATE_INVALID"] == 3, "invalidation count changed")
    require(
        summary["candidate_invalid_release_event_count"] == 3,
        "invalidation summary mismatch",
    )
    require(summary["active_flow_count_at_end"] == 0, "active flow remains")
    require(summary["assignment_count_at_end"] == 0, "assignment remains")
    require(summary["final_total_reserved_bytes"] == 0, "reserved bytes remain")


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
        require(left == right, f"weighted replay differs for {filename}")
    first_run = read_json(first, "run-summary.json")
    second_run = read_json(second, "run-summary.json")
    first_run.pop("wall_clock_s")
    second_run.pop("wall_clock_s")
    require(first_run == second_run, "weighted run summary replay differs")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--size-baseline", required=True)
    parser.add_argument("--weighted-first", required=True)
    parser.add_argument("--weighted-second", required=True)
    arguments = parser.parse_args()

    validate_size_baseline(arguments.size_baseline)
    validate_weighted(arguments.weighted_first)
    validate_weighted(arguments.weighted_second)
    validate_replay(arguments.weighted_first, arguments.weighted_second)
    print(
        "PASS: capacity-normalized HRW changes the intended heterogeneous-link "
        "choice; active source and intermediate assignments release/reselect "
        "across 3->2->3 candidates; all 4 transfers and 50 packets complete "
        "twice with deterministic zero loss"
    )


if __name__ == "__main__":
    main()
