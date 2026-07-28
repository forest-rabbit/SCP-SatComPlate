#!/usr/bin/env python3

import argparse
import csv
import ipaddress
import json
from collections import Counter, defaultdict
from pathlib import Path


ECMP_FILE = "ecmp-route-events.csv"
RESERVATION_FILE = "size-aware-reservation-events.csv"
SIZE_AWARE_SUMMARY_FILE = "size-aware-summary.json"
RUN_FILE = "run-summary.json"
TRANSFER_FILE = "transfer-summary.csv"
FNV1A64_OFFSET_BASIS = 14695981039346656037
FNV1A64_PRIME = 1099511628211
UINT64_MASK = (1 << 64) - 1


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


def read_json(directory, filename):
    path = Path(directory) / filename
    require(path.is_file(), f"missing {path}")
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def int_field(row, name):
    try:
        return int(row[name])
    except (KeyError, ValueError) as error:
        fail(f"invalid integer field {name}: {error}")


def ipv4_int(value):
    return int(ipaddress.IPv4Address(value))


def flow_key(row):
    return (
        row["source_address"],
        row["destination_address"],
        int_field(row, "protocol"),
        int_field(row, "source_port"),
        int_field(row, "destination_port"),
    )


def ecmp_candidate(row):
    return (
        ipv4_int(row["selected_gateway"]),
        int_field(row, "selected_output_interface"),
        ipv4_int(row["destination_address"]),
        0xFFFFFFFF,
    )


def reservation_candidate(row):
    return (
        ipv4_int(row["candidate_gateway"]),
        int_field(row, "candidate_output_interface"),
        ipv4_int(row["candidate_destination"]),
        ipv4_int(row["candidate_destination_mask"]),
    )


def encode_uint(value, width):
    return value.to_bytes(width, "big")


def fnv1a64(data):
    value = FNV1A64_OFFSET_BASIS
    for byte in data:
        value ^= byte
        value = (value * FNV1A64_PRIME) & UINT64_MASK
    return value


def hrw_score(seed, key, candidate):
    source, destination, protocol, source_port, destination_port = key
    gateway, output_interface, route_destination, destination_mask = candidate
    return fnv1a64(
        b"".join(
            (
                encode_uint(seed, 8),
                encode_uint(ipv4_int(source), 4),
                encode_uint(ipv4_int(destination), 4),
                encode_uint(protocol, 1),
                encode_uint(source_port, 2),
                encode_uint(destination_port, 2),
                encode_uint(gateway, 4),
                encode_uint(output_interface, 4),
                encode_uint(route_destination, 4),
                encode_uint(destination_mask, 4),
            )
        )
    )


def hrw_ranking(seed, key, candidates):
    return sorted(candidates, key=lambda candidate: (-hrw_score(seed, key, candidate), candidate))


def compare_files(first, second, filenames):
    for filename in filenames:
        first_path = Path(first) / filename
        second_path = Path(second) / filename
        require(first_path.is_file(), f"missing {first_path}")
        require(second_path.is_file(), f"missing {second_path}")
        require(
            first_path.read_bytes() == second_path.read_bytes(),
            f"repeat output differs for {filename}",
        )


def validate_mode(directory, expected_mode, transfer_count):
    run = read_json(directory, RUN_FILE)
    require(run["routing_mode"] == expected_mode, f"unexpected routing mode in {directory}")
    require(run["ecmp_hash_seed"] == 1, f"unexpected ECMP seed in {directory}")
    require(run["transfer_count"] == transfer_count, f"unexpected transfer count in {directory}")
    return run


def source_ecmp_events(directory):
    rows = read_rows(directory, ECMP_FILE)
    seen = set()
    for row in rows:
        identity = (
            int_field(row, "route_epoch"),
            int_field(row, "node_id"),
            flow_key(row),
        )
        require(identity not in seen, f"duplicate ECMP decision {identity}")
        seen.add(identity)
    return [
        row
        for row in rows
        if int_field(row, "node_id") == 0
        and int_field(row, "protocol") == 17
        and int_field(row, "destination_port") == 9000
    ]


def validate_reservation_ledger(directory, expected_registered):
    rows = read_rows(directory, RESERVATION_FILE)
    summary = read_json(directory, SIZE_AWARE_SUMMARY_FILE)
    require(
        summary["registered_flow_count"] == expected_registered,
        "registered flow count mismatch",
    )

    candidate_loads = defaultdict(int)
    assignments = {}
    total_reserved = 0
    counts = Counter()
    for row in rows:
        action = row["action"]
        counts[action] += 1
        node_id = int_field(row, "node_id")
        transfer_id = int_field(row, "transfer_id")
        declared = int_field(row, "declared_bytes")
        candidate = reservation_candidate(row)
        candidate_key = (node_id, candidate)
        assignment_key = (node_id, transfer_id)
        candidate_before = int_field(row, "candidate_reserved_before")
        candidate_after = int_field(row, "candidate_reserved_after")
        total_before = int_field(row, "total_reserved_before")
        total_after = int_field(row, "total_reserved_after")

        require(candidate_before == candidate_loads[candidate_key], "candidate load-before mismatch")
        require(total_before == total_reserved, "total load-before mismatch")
        if action == "ASSIGN":
            require(assignment_key not in assignments, "duplicate active assignment")
            require(candidate_after == candidate_before + declared, "assign candidate delta mismatch")
            require(total_after == total_before + declared, "assign total delta mismatch")
            assignments[assignment_key] = candidate
        elif action == "STICKY_REUSE":
            require(assignments.get(assignment_key) == candidate, "sticky assignment mismatch")
            require(candidate_after == candidate_before, "sticky candidate load changed")
            require(total_after == total_before, "sticky total load changed")
        elif action in {
            "RELEASE_CANDIDATE_INVALID",
            "RELEASE_SENDER_FINISHED",
        }:
            require(assignments.get(assignment_key) == candidate, "release assignment mismatch")
            require(candidate_before >= declared, "candidate release underflow")
            require(total_before >= declared, "total release underflow")
            require(candidate_after == candidate_before - declared, "release candidate delta mismatch")
            require(total_after == total_before - declared, "release total delta mismatch")
            del assignments[assignment_key]
        else:
            fail(f"unknown reservation action {action}")
        candidate_loads[candidate_key] = candidate_after
        total_reserved = total_after

    require(summary["reservation_event_count"] == len(rows), "reservation event count mismatch")
    require(summary["assign_event_count"] == counts["ASSIGN"], "assign event count mismatch")
    require(
        summary["sticky_reuse_event_count"] == counts["STICKY_REUSE"],
        "sticky event count mismatch",
    )
    require(
        summary["candidate_invalid_release_event_count"]
        == counts["RELEASE_CANDIDATE_INVALID"],
        "invalid release event count mismatch",
    )
    require(
        summary["sender_finished_release_event_count"]
        == counts["RELEASE_SENDER_FINISHED"],
        "sender release event count mismatch",
    )
    require(summary["assignment_count_at_end"] == len(assignments), "final assignment count mismatch")
    require(summary["final_total_reserved_bytes"] == total_reserved, "final reserved bytes mismatch")
    require(sum(candidate_loads.values()) == total_reserved, "candidate totals do not sum to total")
    if summary["active_flow_count_at_end"] == 0:
        require(not assignments and total_reserved == 0, "inactive final state retained reservations")
    return rows, summary


def validate_static_hrw(directory):
    validate_mode(directory, "global-hrw-per-flow", 8)
    events = source_ecmp_events(directory)
    require(len(events) == 8, "static HRW source must expose eight decisions")
    candidates = sorted({ecmp_candidate(row) for row in events})
    require(len(candidates) == 2, "static HRW fixture must expose two candidates")
    selections = {}
    for row in events:
        key = flow_key(row)
        ranking = hrw_ranking(1, key, candidates)
        require(ecmp_candidate(row) == ranking[0], "pure HRW selection mismatch")
        require(row["selection_reason"] == "HRW_PER_FLOW", "pure HRW reason mismatch")
        selections[key] = ranking[0]
    return candidates, selections


def validate_static_size_aware(directory, candidates, pure_selections):
    run = validate_mode(directory, "global-size-aware-hrw", 8)
    require(run["flow_monitor_lost_packets"] == 0, "static size-aware fixture lost packets")
    rows, summary = validate_reservation_ledger(directory, 8)
    require(summary["active_flow_count_at_end"] == 0, "static active flows remain")
    require(summary["final_total_reserved_bytes"] == 0, "static reservations remain")

    source_assignments = [
        row
        for row in rows
        if int_field(row, "node_id") == 0 and row["action"] == "ASSIGN"
    ]
    require(len(source_assignments) == 8, "static source must assign eight flows")
    require(
        sorted({reservation_candidate(row) for row in source_assignments}) == candidates,
        "size-aware and pure HRW candidate sets differ",
    )

    loads = {candidate: 0 for candidate in candidates}
    secondary_count = 0
    changed_count = 0
    for row in source_assignments:
        key = flow_key(row)
        ranking = hrw_ranking(1, key, candidates)
        primary, secondary = ranking[:2]
        expected = primary if loads[primary] <= loads[secondary] else secondary
        selected = reservation_candidate(row)
        expected_reason = (
            "SIZE_AWARE_HRW_PRIMARY"
            if expected == primary
            else "SIZE_AWARE_HRW_SECONDARY"
        )
        require(selected == expected, "size-aware two-choice selection mismatch")
        require(row["selection_reason"] == expected_reason, "size-aware reason mismatch")
        require(
            int_field(row, "candidate_reserved_before") == loads[selected],
            "size-aware selected load mismatch",
        )
        declared = int_field(row, "declared_bytes")
        loads[selected] += declared
        secondary_count += expected == secondary
        changed_count += selected != pure_selections[key]
    require(secondary_count > 0, "static fixture never selected HRW rank two")
    require(changed_count > 0, "size-aware routing never changed a pure HRW collision")

    transfers = read_rows(directory, TRANSFER_FILE)
    require(len(transfers) == 8, "static transfer summary count mismatch")
    for row in transfers:
        require(
            int_field(row, "declared_size_bytes")
            == int_field(row, "sent_application_bytes")
            == int_field(row, "received_application_bytes"),
            f"static transfer {row['transfer_id']} did not complete",
        )


def actions_by_epoch(rows, transfer_id, epoch):
    return [
        row
        for row in rows
        if int_field(row, "node_id") == 0
        and int_field(row, "transfer_id") == transfer_id
        and int_field(row, "route_epoch") == epoch
        and row["action"] != "RELEASE_SENDER_FINISHED"
    ]


def validate_dynamic_size_aware(directory):
    validate_mode(directory, "global-size-aware-hrw", 5)
    rows, summary = validate_reservation_ledger(directory, 5)
    require(summary["active_flow_count_at_end"] == 0, "dynamic active flows remain")
    require(summary["final_total_reserved_bytes"] == 0, "dynamic reservations remain")

    initial = {}
    for transfer_id in range(1, 5):
        epoch_zero = actions_by_epoch(rows, transfer_id, 0)
        require(
            [row["action"] for row in epoch_zero] == ["ASSIGN"],
            f"transfer {transfer_id} initial assignment mismatch",
        )
        initial[transfer_id] = reservation_candidate(epoch_zero[0])

        epoch_one = actions_by_epoch(rows, transfer_id, 1)
        require(
            [row["action"] for row in epoch_one] == ["STICKY_REUSE"],
            f"transfer {transfer_id} changed on candidate-order-only epoch",
        )
        require(
            reservation_candidate(epoch_one[0]) == initial[transfer_id],
            f"transfer {transfer_id} did not keep its initial candidate",
        )
    require(len(set(initial.values())) == 2, "dynamic initial flows did not cover two candidates")

    epoch_two_selected = {}
    migrated = 0
    unchanged = 0
    for transfer_id in range(1, 5):
        epoch_two = actions_by_epoch(rows, transfer_id, 2)
        actions = [row["action"] for row in epoch_two]
        if actions == ["STICKY_REUSE"]:
            selected = reservation_candidate(epoch_two[0])
            require(selected == initial[transfer_id], "unchanged candidate moved")
            unchanged += 1
        else:
            require(
                actions == ["RELEASE_CANDIDATE_INVALID", "ASSIGN"],
                f"transfer {transfer_id} invalid-candidate sequence mismatch",
            )
            require(
                reservation_candidate(epoch_two[0]) == initial[transfer_id],
                "invalid release did not target the previous candidate",
            )
            selected = reservation_candidate(epoch_two[1])
            require(selected != initial[transfer_id], "invalid candidate was reselected")
            migrated += 1
        epoch_two_selected[transfer_id] = selected
    require(migrated > 0 and unchanged > 0, "dynamic fixture missed a migration class")
    remaining = set(epoch_two_selected.values())
    require(len(remaining) == 1, "reduced epoch must have one remaining candidate")
    remaining_candidate = next(iter(remaining))

    for transfer_id in range(1, 5):
        epoch_three = actions_by_epoch(rows, transfer_id, 3)
        require(
            [row["action"] for row in epoch_three] == ["STICKY_REUSE"],
            f"transfer {transfer_id} migrated when a candidate returned",
        )
        require(
            reservation_candidate(epoch_three[0]) == remaining_candidate,
            f"transfer {transfer_id} did not retain the epoch-two candidate",
        )

    new_flow = actions_by_epoch(rows, 5, 3)
    require([row["action"] for row in new_flow] == ["ASSIGN"], "new flow assignment mismatch")
    require(
        reservation_candidate(new_flow[0]) != remaining_candidate,
        "new flow did not use the restored lighter candidate",
    )
    require(
        new_flow[0]["selection_reason"] == "SIZE_AWARE_HRW_SECONDARY",
        "new flow must select the lighter HRW rank-two candidate",
    )


def main():
    parser = argparse.ArgumentParser(
        description="Validate deterministic size-aware HRW ECMP fixtures."
    )
    parser.add_argument("--static-hrw", required=True)
    parser.add_argument("--static-first", required=True)
    parser.add_argument("--static-second", required=True)
    parser.add_argument("--dynamic-first", required=True)
    parser.add_argument("--dynamic-second", required=True)
    arguments = parser.parse_args()

    compare_files(
        arguments.static_first,
        arguments.static_second,
        [ECMP_FILE, RESERVATION_FILE, SIZE_AWARE_SUMMARY_FILE, TRANSFER_FILE],
    )
    compare_files(
        arguments.dynamic_first,
        arguments.dynamic_second,
        [ECMP_FILE, RESERVATION_FILE, SIZE_AWARE_SUMMARY_FILE, TRANSFER_FILE],
    )
    candidates, pure_selections = validate_static_hrw(arguments.static_hrw)
    validate_static_size_aware(arguments.static_first, candidates, pure_selections)
    validate_dynamic_size_aware(arguments.dynamic_first)
    print("PASS: deterministic size-aware HRW static and dynamic contracts")


if __name__ == "__main__":
    main()
