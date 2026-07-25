#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


MAX_UINT64 = (1 << 64) - 1
MAX_INT64 = (1 << 63) - 1


def positive_int(value):
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def non_negative_int(value):
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("value must be non-negative")
    return parsed


def read_satellite_ids(filename):
    with filename.open(encoding="utf-8") as stream:
        root = json.load(stream)
    if set(root) != {"nodes"} or not isinstance(root["nodes"], list):
        raise ValueError("nodes file must contain only a nodes array")

    satellite_ids = []
    for node in root["nodes"]:
        if set(node) != {"node_id", "node_type"}:
            raise ValueError("each node must contain node_id and node_type")
        if node["node_type"] != "sat":
            raise ValueError("only node_type=sat is supported")
        node_id = node["node_id"]
        if not isinstance(node_id, int) or isinstance(node_id, bool) or node_id < 0:
            raise ValueError("node_id must be a non-negative integer")
        satellite_ids.append(node_id)

    satellite_ids = sorted(satellite_ids)
    if len(satellite_ids) < 2:
        raise ValueError("at least two satellites are required")
    if len(set(satellite_ids)) != len(satellite_ids):
        raise ValueError("nodes file contains duplicate node_id")
    return satellite_ids


def transfer_sizes(args):
    if args.size_bytes is not None:
        if args.min_size_bytes is not None or args.max_size_bytes is not None:
            raise ValueError(
                "--size-bytes cannot be combined with min/max size options"
            )
        return [args.size_bytes] * args.count

    if args.min_size_bytes is None or args.max_size_bytes is None:
        raise ValueError(
            "provide --size-bytes or both --min-size-bytes and --max-size-bytes"
        )
    if args.min_size_bytes > args.max_size_bytes:
        raise ValueError("--min-size-bytes cannot exceed --max-size-bytes")
    span = args.max_size_bytes - args.min_size_bytes
    if args.count > 1 and span < args.count - 1:
        raise ValueError("size range is too small to keep every transfer unique")
    if args.count == 1:
        return [args.min_size_bytes]
    return [
        args.min_size_bytes + span * index // (args.count - 1)
        for index in range(args.count)
    ]


def main():
    parser = argparse.ArgumentParser(
        description="Generate deterministic SatCompute NetworkTransfer JSON."
    )
    parser.add_argument("--nodes-file", required=True, type=Path)
    parser.add_argument("--count", required=True, type=positive_int)
    parser.add_argument("--size-bytes", type=positive_int)
    parser.add_argument("--min-size-bytes", type=positive_int)
    parser.add_argument("--max-size-bytes", type=positive_int)
    parser.add_argument(
        "--arrival-start-ns", required=True, type=non_negative_int
    )
    parser.add_argument(
        "--arrival-step-ns", required=True, type=non_negative_int
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    satellite_ids = read_satellite_ids(args.nodes_file)
    sizes = transfer_sizes(args)
    if max(sizes) > MAX_UINT64:
        raise ValueError("size_bytes exceeds uint64")
    last_arrival = (
        args.arrival_start_ns + (args.count - 1) * args.arrival_step_ns
    )
    if last_arrival > MAX_INT64:
        raise ValueError("last arrival_time_ns exceeds int64")

    transfers = []
    node_count = len(satellite_ids)
    for index in range(args.count):
        source_index = index % node_count
        offset = 1 + (index // node_count) % (node_count - 1)
        destination_index = (source_index + offset) % node_count
        transfers.append(
            {
                "transfer_id": index + 1,
                "source_node_id": satellite_ids[source_index],
                "destination_node_id": satellite_ids[destination_index],
                "size_bytes": sizes[index],
                "arrival_time_ns": (
                    args.arrival_start_ns + index * args.arrival_step_ns
                ),
            }
        )

    output = {
        "schema_version": "0.1",
        "transfers": transfers,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(output, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
