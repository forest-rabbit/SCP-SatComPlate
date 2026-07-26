#!/usr/bin/env python3
"""Generate a static bandwidth-limited satellite topology from one snapshot."""

import argparse
import json
from pathlib import Path


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
NODE_FIELDS = {"node_id", "node_type"}
LINK_FIELDS = {
    "node1_id",
    "node2_id",
    "type",
    "delay",
    "link_bandwidth",
}


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


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def require_integer(value, name, minimum, maximum):
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise ValueError(f"{name} must be an integer in [{minimum}, {maximum}]")


def read_nodes(path):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) != {"nodes"}:
        raise ValueError(f"{path} must contain only a nodes array")
    if not isinstance(root["nodes"], list) or not root["nodes"]:
        raise ValueError(f"{path} nodes must be a non-empty array")

    nodes = []
    seen = set()
    for node in root["nodes"]:
        if not isinstance(node, dict) or set(node) != NODE_FIELDS:
            raise ValueError(f"{path} contains an invalid node object")
        node_id = node["node_id"]
        require_integer(node_id, "node_id", 0, UINT32_MAX)
        if node["node_type"] != "sat":
            raise ValueError(f"{path} contains a non-satellite node")
        if node_id in seen:
            raise ValueError(f"{path} contains duplicate node_id={node_id}")
        seen.add(node_id)
        nodes.append({"node_id": node_id, "node_type": "sat"})
    return {"nodes": sorted(nodes, key=lambda item: item["node_id"])}


def read_links(path, node_ids):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) != {"links"}:
        raise ValueError(f"{path} must contain only a links array")
    if not isinstance(root["links"], list) or not root["links"]:
        raise ValueError(f"{path} links must be a non-empty array")

    links = []
    seen = set()
    for link in root["links"]:
        if not isinstance(link, dict) or set(link) != LINK_FIELDS:
            raise ValueError(f"{path} contains an invalid link object")
        node1 = link["node1_id"]
        node2 = link["node2_id"]
        require_integer(node1, "node1_id", 0, UINT32_MAX)
        require_integer(node2, "node2_id", 0, UINT32_MAX)
        require_integer(link["delay"], "delay", 0, UINT64_MAX)
        require_integer(
            link["link_bandwidth"],
            "link_bandwidth",
            1,
            UINT64_MAX // 1000,
        )
        if link["type"] != "sat":
            raise ValueError(f"{path} contains a non-satellite link")
        if node1 == node2 or node1 not in node_ids or node2 not in node_ids:
            raise ValueError(f"{path} contains an invalid link endpoint")
        endpoint = tuple(sorted((node1, node2)))
        if endpoint in seen:
            raise ValueError(f"{path} contains duplicate link {endpoint}")
        seen.add(endpoint)
        links.append(dict(link))
    return {"links": links}


def write_json(path, value):
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate one static satellite-only stress topology."
    )
    parser.add_argument("--source-topology-dir", required=True, type=Path)
    parser.add_argument("--output-topology-dir", required=True, type=Path)
    parser.add_argument("--bandwidth-kbps", required=True, type=positive_int)
    parser.add_argument(
        "--static-from-snapshot",
        default=0,
        type=non_negative_int,
        metavar="SECONDS",
    )
    args = parser.parse_args()

    if args.bandwidth_kbps > UINT64_MAX // 1000:
        raise ValueError("bandwidth-kbps overflows the runtime bps conversion")
    label = f"{args.static_from_snapshot}s"
    source_nodes_path = args.source_topology_dir / f"nodes_{label}.json"
    source_links_path = args.source_topology_dir / f"topology_{label}.json"
    nodes = read_nodes(source_nodes_path)
    node_ids = {item["node_id"] for item in nodes["nodes"]}
    source_links = read_links(source_links_path, node_ids)

    output_links = {
        "links": [
            {**link, "link_bandwidth": args.bandwidth_kbps}
            for link in source_links["links"]
        ]
    }
    args.output_topology_dir.mkdir(parents=True, exist_ok=True)
    allowed_outputs = {"nodes_0s.json", "topology_0s.json"}
    unexpected = {
        path.name
        for path in args.output_topology_dir.iterdir()
        if path.name not in allowed_outputs
    }
    if unexpected:
        raise ValueError(
            "output topology directory contains unexpected entries: "
            + ", ".join(sorted(unexpected))
        )

    output_nodes_path = args.output_topology_dir / "nodes_0s.json"
    output_links_path = args.output_topology_dir / "topology_0s.json"
    write_json(output_nodes_path, nodes)
    write_json(output_links_path, output_links)

    generated_nodes = read_nodes(output_nodes_path)
    generated_ids = {item["node_id"] for item in generated_nodes["nodes"]}
    generated_links = read_links(output_links_path, generated_ids)
    source_shape = {
        (
            *sorted((link["node1_id"], link["node2_id"])),
            link["delay"],
        )
        for link in source_links["links"]
    }
    generated_shape = {
        (
            *sorted((link["node1_id"], link["node2_id"])),
            link["delay"],
        )
        for link in generated_links["links"]
    }
    if generated_ids != node_ids or generated_shape != source_shape:
        raise AssertionError("generated topology changed nodes, endpoints, or delay")
    if any(
        link["link_bandwidth"] != args.bandwidth_kbps
        for link in generated_links["links"]
    ):
        raise AssertionError("generated topology bandwidth validation failed")

    print(
        "PASS: generated static stress topology "
        f"({len(generated_ids)} satellites, "
        f"{len(generated_links['links'])} ISLs, "
        f"{args.bandwidth_kbps} kbps)"
    )


if __name__ == "__main__":
    main()
