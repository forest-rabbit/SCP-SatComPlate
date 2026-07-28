#!/usr/bin/env python3

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path


TARGET_IDS = {198, 1287, 2491}
EXPECTED_TARGET_SIZES = {
    198: 448200000,
    1287: 77304185,
    2491: 20468628,
}
EXPECTED_TARGET_PORTS = {
    198: 10003,
    1287: 10014,
    2491: 10036,
}


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def require(condition, message):
    if not condition:
        fail(message)


def int_field(row, name):
    try:
        return int(row[name])
    except (KeyError, ValueError) as error:
        fail(f"invalid integer field {name}: {error}")


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


def flow_key(row):
    return (
        row["source_address"],
        row["destination_address"],
        int_field(row, "protocol"),
        int_field(row, "source_port"),
        int_field(row, "destination_port"),
    )


def selected_candidate(row):
    return (
        row["selected_gateway"],
        int_field(row, "selected_output_interface"),
    )


def compare_repeat(first, second):
    for filename in (
        "ecmp-route-events.csv",
        "diagnostics/failure/flow-drop-reasons.csv",
        "network-flow-details.csv",
        "size-aware-reservation-events.csv",
        "size-aware-summary.json",
        "transfer-summary.csv",
    ):
        first_path = Path(first) / filename
        second_path = Path(second) / filename
        require(first_path.is_file(), f"missing {first_path}")
        require(second_path.is_file(), f"missing {second_path}")
        require(
            first_path.read_bytes() == second_path.read_bytes(),
            f"size-aware replay differs for {filename}",
        )


def drop_reason_packets(run):
    return {
        reason["reason_name"]: reason["dropped_packets"]
        for reason in run["flow_monitor_drop_reasons"]
    }


def validate_run(directory, mode, expected_lost):
    run = read_json(directory, "run-summary.json")
    require(run["routing_mode"] == mode, f"unexpected routing mode for {directory}")
    require(run["ecmp_hash_seed"] == 1, f"unexpected seed for {directory}")
    require(run["transfer_count"] == 41, f"unexpected transfer count for {directory}")
    require(run["flow_monitor_lost_packets"] == expected_lost, f"lost count mismatch for {directory}")
    require(run["flow_monitor_unattributed_lost_packets"] == 0, f"unattributed loss in {directory}")
    reasons = drop_reason_packets(run)
    require(reasons["QUEUE"] == 0, f"device queue drop in {directory}")
    require(reasons["QUEUE_DISC"] == expected_lost, f"QueueDisc drop mismatch in {directory}")
    return run


def validate_hash_drop_owners(directory):
    rows = read_rows(
        directory,
        "diagnostics/failure/flow-drop-reasons.csv",
    )
    observed = defaultdict(int)
    for row in rows:
        if row["reason_name"] == "QUEUE_DISC":
            observed[int_field(row, "transfer_id")] += int_field(row, "dropped_packets")
    require(
        dict(observed) == {198: 11, 1287: 1, 2491: 1},
        "hash replay QueueDisc owners changed",
    )


def transfer_flow_keys(directory):
    rows = read_rows(directory, "network-flow-details.csv")
    keys = {}
    for row in rows:
        transfer_id = int_field(row, "transfer_id")
        if transfer_id in TARGET_IDS:
            require(transfer_id not in keys, f"duplicate target flow {transfer_id}")
            keys[transfer_id] = flow_key(row)
    require(set(keys) == TARGET_IDS, "target flow metadata missing")
    return keys


def hotspot_selections(directory):
    keys = transfer_flow_keys(directory)
    events = read_rows(directory, "ecmp-route-events.csv")
    selections = {}
    for transfer_id, key in keys.items():
        matches = [
            row
            for row in events
            if int_field(row, "node_id") == 36 and flow_key(row) == key
        ]
        require(len(matches) <= 1, f"duplicate node-36 decision for {transfer_id}")
        if matches:
            selections[transfer_id] = selected_candidate(matches[0])
    return selections


def validate_hotspot(hash_directory, hrw_directory, size_directory):
    hash_selections = hotspot_selections(hash_directory)
    require(set(hash_selections) == TARGET_IDS, "hash replay target bypassed node 36")
    hotspot = set(hash_selections.values())
    require(len(hotspot) == 1, "hash replay no longer converges on one node-36 next hop")
    hotspot = next(iter(hotspot))

    hrw_selections = hotspot_selections(hrw_directory)
    size_selections = hotspot_selections(size_directory)
    hrw_hotspot_count = sum(candidate == hotspot for candidate in hrw_selections.values())
    size_hotspot_count = sum(candidate == hotspot for candidate in size_selections.values())
    require(hrw_hotspot_count < 3, "pure HRW still concentrates all targets on 36->37")
    require(size_hotspot_count < 3, "size-aware HRW still concentrates all targets on 36->37")
    return hrw_hotspot_count, size_hotspot_count


def validate_fixture_and_filler_release(directory):
    transfers = read_rows(directory, "transfer-summary.csv")
    require(len(transfers) == 41, "replay transfer summary count mismatch")
    fillers = []
    targets = {}
    for row in transfers:
        transfer_id = int_field(row, "transfer_id")
        declared = int_field(row, "declared_size_bytes")
        if transfer_id in TARGET_IDS:
            targets[transfer_id] = row
        else:
            require(declared == 1, f"filler {transfer_id} is not one byte")
            fillers.append(transfer_id)
    require(len(fillers) == 38, "replay must contain 38 port fillers")
    require(set(targets) == TARGET_IDS, "replay target summary missing")
    for transfer_id, row in targets.items():
        require(
            int_field(row, "declared_size_bytes") == EXPECTED_TARGET_SIZES[transfer_id],
            f"target {transfer_id} size changed",
        )
        require(
            int_field(row, "source_port") == EXPECTED_TARGET_PORTS[transfer_id],
            f"target {transfer_id} source-port ordinal changed",
        )

    events = read_rows(directory, "size-aware-reservation-events.csv")
    filler_events = [
        row for row in events if int_field(row, "transfer_id") not in TARGET_IDS
    ]
    require(len(filler_events) == 76, "each filler must have one assign and one release")
    actions = Counter(row["action"] for row in filler_events)
    require(
        actions == Counter({"ASSIGN": 38, "RELEASE_SENDER_FINISHED": 38}),
        "filler reservation lifecycle changed",
    )
    require(
        all(int_field(row, "simulation_time_ns") == 0 for row in filler_events),
        "filler reservation survived beyond time zero",
    )
    require(
        int_field(filler_events[-1], "total_reserved_after") == 0,
        "filler reservations were not zero before target traffic",
    )
    require(
        min(
            int_field(row, "simulation_time_ns")
            for row in events
            if int_field(row, "transfer_id") in TARGET_IDS
        )
        == 100000000,
        "first target flow no longer starts at 0.1 s",
    )

    summary = read_json(directory, "size-aware-summary.json")
    require(summary["registered_flow_count"] == 41, "replay registry count mismatch")
    require(summary["active_flow_count_at_end"] == 0, "replay active flow remains")
    require(summary["assignment_count_at_end"] == 0, "replay assignment remains")
    require(summary["final_total_reserved_bytes"] == 0, "replay reservation remains")


def main():
    parser = argparse.ArgumentParser(
        description="Validate the N1 local collision replay across ECMP modes."
    )
    parser.add_argument("--hash", required=True)
    parser.add_argument("--hrw", required=True)
    parser.add_argument("--size-first", required=True)
    parser.add_argument("--size-second", required=True)
    arguments = parser.parse_args()

    compare_repeat(arguments.size_first, arguments.size_second)
    validate_run(arguments.hash, "global-hash-per-flow", 13)
    validate_hash_drop_owners(arguments.hash)
    validate_run(arguments.hrw, "global-hrw-per-flow", 0)
    validate_run(arguments.size_first, "global-size-aware-hrw", 0)
    validate_fixture_and_filler_release(arguments.size_first)
    hrw_hotspot_count, size_hotspot_count = validate_hotspot(
        arguments.hash,
        arguments.hrw,
        arguments.size_first,
    )
    print(
        "PASS: N1 replay hash=13 drops, HRW=0, size-aware=0; "
        f"36->37 target counts HRW={hrw_hotspot_count}, "
        f"size-aware={size_hotspot_count}"
    )


if __name__ == "__main__":
    main()
