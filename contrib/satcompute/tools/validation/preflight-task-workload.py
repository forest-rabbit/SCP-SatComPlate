#!/usr/bin/env python3
"""Preflight SatCompute task workloads before expensive ns-3 runs."""

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict, deque
from pathlib import Path


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1
BASIS_POINTS = 10000
MAX_SOURCE_PORTS = 55536
MAX_UDP_PAYLOAD = 65507
ONE_GB = 1_000_000_000
FIVE_HUNDRED_MB = 500_000_000
CLASS_ORDER = (
    "image-enhancement",
    "image-detection",
    "dnn-inference",
    "preprocess-compress",
)
TAIL_CLASS_ORDER = ("preprocess-compress", "image-enhancement")
WORK_RANGES = {
    "image-enhancement": (2_000_000, 5_000_000),
    "image-detection": (1_500_000, 4_500_000),
    "dnn-inference": (800_000, 4_000_000),
    "preprocess-compress": (200_000, 2_000_000),
}
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


def positive_finite(value):
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("value must be finite and positive")
    return parsed


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def require_integer(value, name, minimum, maximum):
    require(
        isinstance(value, int)
        and not isinstance(value, bool)
        and minimum <= value <= maximum,
        f"{name} must be an integer in [{minimum}, {maximum}]",
    )


def read_topology(directory):
    nodes_path = directory / "nodes_0s.json"
    links_path = directory / "topology_0s.json"
    nodes_root = read_json(nodes_path)
    require(
        isinstance(nodes_root, dict) and set(nodes_root) == {"nodes"},
        "nodes_0s.json must contain only a nodes array",
    )
    require(
        isinstance(nodes_root["nodes"], list) and nodes_root["nodes"],
        "nodes array must be non-empty",
    )
    node_ids = set()
    for node in nodes_root["nodes"]:
        require(
            isinstance(node, dict)
            and set(node) == {"node_id", "node_type"},
            "invalid topology node object",
        )
        require_integer(node["node_id"], "node_id", 0, UINT32_MAX)
        require(node["node_type"] == "sat", "topology must be satellite-only")
        require(node["node_id"] not in node_ids, "duplicate topology node ID")
        node_ids.add(node["node_id"])

    links_root = read_json(links_path)
    require(
        isinstance(links_root, dict) and set(links_root) == {"links"},
        "topology_0s.json must contain only a links array",
    )
    require(
        isinstance(links_root["links"], list) and links_root["links"],
        "links array must be non-empty",
    )
    expected_fields = {
        "node1_id",
        "node2_id",
        "type",
        "delay",
        "link_bandwidth",
    }
    links = []
    endpoints = set()
    graph = {node_id: set() for node_id in node_ids}
    for link in links_root["links"]:
        require(
            isinstance(link, dict) and set(link) == expected_fields,
            "invalid topology link object",
        )
        node1 = link["node1_id"]
        node2 = link["node2_id"]
        require_integer(node1, "node1_id", 0, UINT32_MAX)
        require_integer(node2, "node2_id", 0, UINT32_MAX)
        require(node1 in node_ids and node2 in node_ids, "unknown link endpoint")
        require(node1 != node2, "self-loop ISL is not supported")
        require(link["type"] == "sat", "topology must contain only ISLs")
        require_integer(link["delay"], "delay", 0, UINT64_MAX)
        require_integer(
            link["link_bandwidth"],
            "link_bandwidth",
            1,
            UINT64_MAX // 1000,
        )
        endpoint = tuple(sorted((node1, node2)))
        require(endpoint not in endpoints, "duplicate undirected ISL")
        endpoints.add(endpoint)
        graph[node1].add(node2)
        graph[node2].add(node1)
        links.append(link)
    require(
        all(graph[node_id] for node_id in node_ids),
        "topology contains an isolated satellite",
    )
    return sorted(node_ids), links, graph


def read_compute_profile(path, topology_ids):
    root = read_json(path)
    require(
        isinstance(root, dict)
        and set(root) == {"schema_version", "compute_nodes"},
        "ComputeProfile root violates its closed-world contract",
    )
    require(root["schema_version"] == "0.1", "ComputeProfile schema must be 0.1")
    require(
        isinstance(root["compute_nodes"], list) and root["compute_nodes"],
        "ComputeProfile compute_nodes must be non-empty",
    )
    topology_ids = set(topology_ids)
    nodes = []
    seen = set()
    for node in root["compute_nodes"]:
        require(
            isinstance(node, dict)
            and set(node)
            == {"node_id", "compute_rate_work_units_per_second"},
            "invalid ComputeProfile node object",
        )
        node_id = node["node_id"]
        rate = node["compute_rate_work_units_per_second"]
        require_integer(node_id, "compute node_id", 0, UINT32_MAX)
        require_integer(rate, "compute rate", 1, UINT64_MAX)
        require(node_id in topology_ids, "unknown ComputeProfile node")
        require(node_id not in seen, "duplicate ComputeProfile node")
        seen.add(node_id)
        nodes.append(node)
    return sorted(nodes, key=lambda item: item["node_id"])


def read_task_trace(path, topology_ids, compute_nodes, simulation_duration_ns):
    root = read_json(path)
    require(
        isinstance(root, dict)
        and set(root) == {"schema_version", "tasks"},
        "TaskTrace root violates its closed-world contract",
    )
    require(root["schema_version"] == "0.1", "TaskTrace schema must be 0.1")
    require(
        isinstance(root["tasks"], list) and root["tasks"],
        "TaskTrace tasks must be non-empty",
    )
    topology_ids = set(topology_ids)
    compute_ids = {node["node_id"] for node in compute_nodes}
    tasks = []
    seen = set()
    for task in root["tasks"]:
        require(
            isinstance(task, dict) and set(task) == TASK_FIELDS,
            "TaskTrace task fields violate schema 0.1",
        )
        for field in TASK_FIELDS:
            require_integer(task[field], field, 0, UINT64_MAX)
        task_id = task["task_id"]
        require(0 < task_id <= UINT64_MAX // 2, "task_id is transfer-ID unsafe")
        require(task_id not in seen, "duplicate task_id")
        seen.add(task_id)
        for field in ("source_node_id", "compute_node_id", "result_node_id"):
            require(task[field] <= UINT32_MAX, f"{field} exceeds uint32")
            require(task[field] in topology_ids, f"unknown {field}")
        require(
            task["compute_node_id"] in compute_ids,
            "compute_node_id is absent from ComputeProfile",
        )
        require(
            task["source_node_id"] != task["compute_node_id"],
            "source_node_id must differ from compute_node_id",
        )
        require(
            task["compute_node_id"] != task["result_node_id"],
            "compute_node_id must differ from result_node_id",
        )
        for field in ("input_bytes", "output_bytes", "compute_work_units"):
            require(task[field] > 0, f"{field} must be positive")
        require(
            task["arrival_time_ns"] <= INT64_MAX,
            "arrival_time_ns exceeds int64",
        )
        require(
            task["arrival_time_ns"] < simulation_duration_ns,
            "task arrival is not before simulation stop",
        )
        tasks.append(task)
    return sorted(tasks, key=lambda item: item["task_id"])


def shortest_hops(graph, source, destination):
    if source == destination:
        return 0
    queue = deque([(source, 0)])
    visited = {source}
    while queue:
        node, hops = queue.popleft()
        for neighbor in graph[node]:
            if neighbor == destination:
                return hops + 1
            if neighbor not in visited:
                visited.add(neighbor)
                queue.append((neighbor, hops + 1))
    raise ValueError(f"no path between satellites {source} and {destination}")


def payload_bytes(chunk_mode, fixed_payload_bytes, size_bytes):
    if chunk_mode == "fixed":
        return fixed_payload_bytes
    if size_bytes <= 1 << 20:
        return 1024
    if size_bytes <= 64 << 20:
        return 8192
    return 64000


def packet_count(chunk_mode, fixed_payload_bytes, size_bytes):
    payload = payload_bytes(chunk_mode, fixed_payload_bytes, size_bytes)
    return (size_bytes + payload - 1) // payload


def largest_remainder(total, shares, order, denominator=BASIS_POINTS):
    require(set(shares) == set(order), "share keys violate their contract")
    for name in order:
        require_integer(shares[name], f"{name} share", 0, denominator)
    require(
        sum(shares.values()) == denominator,
        "basis-point shares must sum to 10000",
    )
    result = {
        name: total * shares[name] // denominator
        for name in order
    }
    remaining = total - sum(result.values())
    ranked = sorted(
        order,
        key=lambda name: (
            -(total * shares[name] % denominator),
            order.index(name),
        ),
    )
    for name in ranked[:remaining]:
        result[name] += 1
    return result


def scaled_tail_count(maximum_count, scale_bp):
    return largest_remainder(
        maximum_count,
        {
            "selected": scale_bp,
            "unselected": BASIS_POINTS - scale_bp,
        },
        ("selected", "unselected"),
    )["selected"]


def average_ranks(values):
    ranks = [0.0] * len(values)
    ordered = sorted(range(len(values)), key=lambda index: (values[index], index))
    start = 0
    while start < len(ordered):
        end = start + 1
        while end < len(ordered) and values[ordered[end]] == values[ordered[start]]:
            end += 1
        average = (start + 1 + end) / 2.0
        for position in range(start, end):
            ranks[ordered[position]] = average
        start = end
    return ranks


def spearman_correlation(first, second):
    require(
        len(first) == len(second) and len(first) >= 2,
        "invalid Spearman samples",
    )
    first_ranks = average_ranks(first)
    second_ranks = average_ranks(second)
    first_mean = sum(first_ranks) / len(first_ranks)
    second_mean = sum(second_ranks) / len(second_ranks)
    covariance = sum(
        (left - first_mean) * (right - second_mean)
        for left, right in zip(first_ranks, second_ranks)
    )
    first_variance = sum((rank - first_mean) ** 2 for rank in first_ranks)
    second_variance = sum((rank - second_mean) ** 2 for rank in second_ranks)
    require(
        first_variance > 0 and second_variance > 0,
        "correlation sample has no rank variance",
    )
    return covariance / math.sqrt(first_variance * second_variance)


def require_summary_integer(summary, field, expected):
    require(field in summary, f"workload summary is missing {field}")
    require_integer(summary[field], f"summary {field}", 0, UINT64_MAX)
    require(summary[field] == expected, f"summary {field} mismatch")


def require_mapping_equal(summary, field, expected):
    require(field in summary, f"workload summary is missing {field}")
    require(summary[field] == expected, f"summary {field} mismatch")


def validate_distribution_summary(summary, label, values):
    minimum_field = f"min_{label}"
    mean_field = f"mean_{label}"
    maximum_field = f"max_{label}"
    require_summary_integer(summary, minimum_field, min(values))
    require_summary_integer(summary, maximum_field, max(values))
    require(
        mean_field in summary
        and isinstance(summary[mean_field], (int, float))
        and not isinstance(summary[mean_field], bool)
        and math.isclose(
            summary[mean_field],
            sum(values) / len(values),
            rel_tol=1e-12,
            abs_tol=1e-12,
        ),
        f"summary {mean_field} mismatch",
    )


def validate_output_and_work_rules(task, class_name):
    input_bytes = task["input_bytes"]
    output_bytes = task["output_bytes"]
    work_units = task["compute_work_units"]
    if class_name == "image-enhancement":
        minimum_output = (input_bytes * 8000 + 9999) // 10000
        maximum_output = input_bytes
    elif class_name == "image-detection":
        minimum_output = (input_bytes * 50 + 9999) // 10000
        maximum_output = (input_bytes * 500 + 9999) // 10000
    elif class_name == "dnn-inference":
        minimum_output = 4096
        maximum_output = 262144
    else:
        minimum_output = (input_bytes * 1000 + 9999) // 10000
        maximum_output = (input_bytes * 4000 + 9999) // 10000
    require(
        minimum_output <= output_bytes <= maximum_output,
        f"task {task['task_id']} output_bytes violates {class_name} rules",
    )
    minimum_work, maximum_work = WORK_RANGES[class_name]
    require(
        minimum_work <= work_units <= maximum_work,
        f"task {task['task_id']} compute_work_units violates class rules",
    )


def validate_workload_summary(path, trace_path, tasks, compute_nodes):
    summary = read_json(path)
    require(isinstance(summary, dict), "workload summary root must be an object")
    for field in (
        "generator_version",
        "rules_version",
        "seed",
        "class_share_basis_points",
        "tail_class_share_basis_points",
        "profile_counts",
        "task_class_assignments",
        "task_trace_sha256",
    ):
        require(field in summary, f"workload summary is missing {field}")
    require(
        isinstance(summary["generator_version"], str)
        and summary["generator_version"],
        "invalid generator_version",
    )
    require(
        isinstance(summary["rules_version"], str) and summary["rules_version"],
        "invalid rules_version",
    )
    require(
        isinstance(summary["seed"], str) and summary["seed"],
        "invalid workload seed",
    )
    require(
        summary.get("deterministic_method")
        == "SHA-256(seed, rules_version, task_id, field_name)",
        "invalid deterministic method declaration",
    )

    expected_hash = hashlib.sha256(trace_path.read_bytes()).hexdigest()
    require(
        summary["task_trace_sha256"] == expected_hash,
        "workload summary TaskTrace SHA-256 mismatch",
    )
    require_summary_integer(summary, "task_count", len(tasks))
    require_summary_integer(
        summary,
        "total_input_bytes",
        sum(task["input_bytes"] for task in tasks),
    )
    require_summary_integer(
        summary,
        "total_output_bytes",
        sum(task["output_bytes"] for task in tasks),
    )
    require_summary_integer(
        summary,
        "total_compute_work_units",
        sum(task["compute_work_units"] for task in tasks),
    )
    require_summary_integer(
        summary,
        "first_arrival_ns",
        min(task["arrival_time_ns"] for task in tasks),
    )
    require_summary_integer(
        summary,
        "last_arrival_ns",
        max(task["arrival_time_ns"] for task in tasks),
    )
    require(
        summary.get("arrival_mode") in {"uniform", "burst"},
        "invalid workload summary arrival_mode",
    )
    require_integer(
        summary.get("arrival_start_ns"),
        "summary arrival_start_ns",
        0,
        INT64_MAX,
    )
    require_integer(
        summary.get("arrival_end_ns"),
        "summary arrival_end_ns",
        0,
        INT64_MAX,
    )
    require(
        summary["arrival_start_ns"] <= summary["first_arrival_ns"]
        <= summary["last_arrival_ns"] <= summary["arrival_end_ns"],
        "actual arrivals fall outside the declared workload window",
    )
    declared_arrival_span = (
        summary["arrival_end_ns"] - summary["arrival_start_ns"] + 1
    )
    arrivals = sorted(task["arrival_time_ns"] for task in tasks)
    if summary["arrival_mode"] == "uniform":
        require(
            declared_arrival_span >= len(tasks),
            "uniform arrival window has fewer nanoseconds than tasks",
        )
        for index, arrival in enumerate(arrivals):
            lower = (
                summary["arrival_start_ns"]
                + declared_arrival_span * index // len(tasks)
            )
            upper = (
                summary["arrival_start_ns"]
                + declared_arrival_span * (index + 1) // len(tasks)
                - 1
            )
            require(
                lower <= arrival <= upper,
                "uniform arrivals violate deterministic stratification",
            )
    else:
        burst_span = max(1, (declared_arrival_span + 9) // 10)
        require(
            all(
                summary["arrival_start_ns"]
                <= arrival
                < summary["arrival_start_ns"] + burst_span
                for arrival in arrivals
            ),
            "burst arrivals fall outside the first 10% of the window",
        )

    class_shares = summary["class_share_basis_points"]
    require(
        isinstance(class_shares, dict),
        "class_share_basis_points must be an object",
    )
    expected_class_counts = largest_remainder(
        len(tasks),
        class_shares,
        CLASS_ORDER,
    )
    require_mapping_equal(
        summary,
        "profile_counts",
        expected_class_counts,
    )

    raw_assignments = summary["task_class_assignments"]
    require(
        isinstance(raw_assignments, dict),
        "task_class_assignments must be an object",
    )
    expected_keys = {str(task["task_id"]) for task in tasks}
    require(
        set(raw_assignments) == expected_keys,
        "task_class_assignments task coverage mismatch",
    )
    assignments = {}
    for task in tasks:
        task_id = task["task_id"]
        class_name = raw_assignments[str(task_id)]
        require(class_name in CLASS_ORDER, "unknown synthetic workload class")
        assignments[task_id] = class_name
        validate_output_and_work_rules(task, class_name)
    actual_class_counts = Counter(assignments.values())
    require(
        {name: actual_class_counts[name] for name in CLASS_ORDER}
        == expected_class_counts,
        "task class assignments violate largest-remainder counts",
    )

    scale_bp = summary.get("scenario_scale_bp")
    maximum_one_gb = summary.get("maximum_large_1gb_count")
    maximum_five_hundred_mb = summary.get("maximum_large_500mb_count")
    require_integer(scale_bp, "scenario_scale_bp", 0, BASIS_POINTS)
    require_integer(maximum_one_gb, "maximum_large_1gb_count", 0, UINT64_MAX)
    require_integer(
        maximum_five_hundred_mb,
        "maximum_large_500mb_count",
        0,
        UINT64_MAX,
    )
    expected_one_gb = scaled_tail_count(maximum_one_gb, scale_bp)
    expected_five_hundred_mb = scaled_tail_count(
        maximum_five_hundred_mb,
        scale_bp,
    )
    require_summary_integer(summary, "large_1gb_count", expected_one_gb)
    require_summary_integer(
        summary,
        "large_500mb_count",
        expected_five_hundred_mb,
    )
    one_gb_tasks = [
        task for task in tasks if task["input_bytes"] == ONE_GB
    ]
    five_hundred_mb_tasks = [
        task for task in tasks if task["input_bytes"] == FIVE_HUNDRED_MB
    ]
    require(
        len(one_gb_tasks) == expected_one_gb,
        "TaskTrace 1 GB tail count mismatch",
    )
    require(
        len(five_hundred_mb_tasks) == expected_five_hundred_mb,
        "TaskTrace 500 MB tail count mismatch",
    )
    require_integer(
        summary.get("non_tail_min_input_bytes"),
        "non_tail_min_input_bytes",
        1,
        UINT64_MAX,
    )
    require_integer(
        summary.get("non_tail_max_input_bytes"),
        "non_tail_max_input_bytes",
        summary["non_tail_min_input_bytes"],
        UINT64_MAX,
    )
    tail_task_ids = {
        task["task_id"] for task in one_gb_tasks + five_hundred_mb_tasks
    }
    require(
        all(
            summary["non_tail_min_input_bytes"]
            <= task["input_bytes"]
            <= summary["non_tail_max_input_bytes"]
            for task in tasks
            if task["task_id"] not in tail_task_ids
        ),
        "a non-tail task violates its declared input bounds",
    )

    tail_shares = summary["tail_class_share_basis_points"]
    require(
        isinstance(tail_shares, dict),
        "tail_class_share_basis_points must be an object",
    )
    expected_tail_counts = {
        "1gb": largest_remainder(
            expected_one_gb,
            tail_shares,
            TAIL_CLASS_ORDER,
        ),
        "500mb": largest_remainder(
            expected_five_hundred_mb,
            tail_shares,
            TAIL_CLASS_ORDER,
        ),
    }
    require_mapping_equal(
        summary,
        "tail_class_counts",
        expected_tail_counts,
    )
    actual_tail_counts = {
        "1gb": Counter(assignments[task["task_id"]] for task in one_gb_tasks),
        "500mb": Counter(
            assignments[task["task_id"]]
            for task in five_hundred_mb_tasks
        ),
    }
    for size_name in ("1gb", "500mb"):
        require(
            {
                name: actual_tail_counts[size_name][name]
                for name in TAIL_CLASS_ORDER
            }
            == expected_tail_counts[size_name],
            f"{size_name} tail class assignment mismatch",
        )

    profile_input = {}
    profile_output = {}
    profile_work = {}
    correlations = {}
    largest_means = {}
    smallest_means = {}
    for class_name in CLASS_ORDER:
        class_tasks = [
            task
            for task in tasks
            if assignments[task["task_id"]] == class_name
        ]
        profile_input[class_name] = sum(
            task["input_bytes"] for task in class_tasks
        )
        profile_output[class_name] = sum(
            task["output_bytes"] for task in class_tasks
        )
        profile_work[class_name] = sum(
            task["compute_work_units"] for task in class_tasks
        )
        if len(class_tasks) < 4:
            correlations[class_name] = None
            largest_means[class_name] = None
            smallest_means[class_name] = None
            continue
        correlation = spearman_correlation(
            [task["input_bytes"] for task in class_tasks],
            [task["compute_work_units"] for task in class_tasks],
        )
        ordered = sorted(
            class_tasks,
            key=lambda task: (task["input_bytes"], task["task_id"]),
        )
        quartile_count = max(1, len(ordered) // 4)
        smallest_mean = (
            sum(
                task["compute_work_units"]
                for task in ordered[:quartile_count]
            )
            / quartile_count
        )
        largest_mean = (
            sum(
                task["compute_work_units"]
                for task in ordered[-quartile_count:]
            )
            / quartile_count
        )
        require(
            correlation > 0 and largest_mean > smallest_mean,
            f"{class_name} input/work correlation contract failed",
        )
        correlations[class_name] = correlation
        largest_means[class_name] = largest_mean
        smallest_means[class_name] = smallest_mean

    require_mapping_equal(summary, "profile_input_bytes", profile_input)
    require_mapping_equal(summary, "profile_output_bytes", profile_output)
    require_mapping_equal(summary, "profile_work_units", profile_work)
    for field, expected in (
        ("per_class_spearman_input_work", correlations),
        ("per_class_largest_quartile_mean_work", largest_means),
        ("per_class_smallest_quartile_mean_work", smallest_means),
    ):
        require(field in summary, f"workload summary is missing {field}")
        require(set(summary[field]) == set(expected), f"summary {field} keys mismatch")
        for class_name in CLASS_ORDER:
            actual = summary[field][class_name]
            target = expected[class_name]
            if target is None:
                require(actual is None, f"summary {field} should be null")
            else:
                require(
                    isinstance(actual, (int, float))
                    and math.isclose(actual, target, rel_tol=1e-12, abs_tol=1e-12),
                    f"summary {field} mismatch for {class_name}",
                )

    compute_node_counts = Counter(
        task["compute_node_id"] for task in tasks
    )
    expected_compute_counts = {
        str(node_id): compute_node_counts[node_id]
        for node_id in sorted(node["node_id"] for node in compute_nodes)
    }
    require_mapping_equal(
        summary,
        "compute_node_task_counts",
        expected_compute_counts,
    )
    validate_distribution_summary(
        summary,
        "input_bytes",
        [task["input_bytes"] for task in tasks],
    )
    validate_distribution_summary(
        summary,
        "output_bytes",
        [task["output_bytes"] for task in tasks],
    )
    validate_distribution_summary(
        summary,
        "work_units",
        [task["compute_work_units"] for task in tasks],
    )
    return summary, assignments, correlations, largest_means, smallest_means


def all_pairs_hops(graph):
    distances = {}
    for source in sorted(graph):
        source_distances = {source: 0}
        queue = deque([source])
        while queue:
            node = queue.popleft()
            for neighbor in graph[node]:
                if neighbor not in source_distances:
                    source_distances[neighbor] = source_distances[node] + 1
                    queue.append(neighbor)
        require(
            len(source_distances) == len(graph),
            f"topology is disconnected from satellite {source}",
        )
        for destination, hops in source_distances.items():
            distances[(source, destination)] = hops
    return distances


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def analyze_workload(
    topology_ids,
    links,
    graph,
    compute_nodes,
    tasks,
    summary,
    correlations,
    largest_means,
    smallest_means,
    args,
):
    total_input = sum(task["input_bytes"] for task in tasks)
    total_output = sum(task["output_bytes"] for task in tasks)
    total_work = sum(task["compute_work_units"] for task in tasks)
    require(
        total_input <= args.max_total_input_bytes,
        "total input exceeds --max-total-input-bytes",
    )
    require(68 <= args.isl_mtu_bytes <= 65535, "invalid ISL MTU")
    require(
        0 < args.isl_queue_bytes <= UINT32_MAX,
        "ISL queue bytes exceeds the runtime uint32 contract",
    )
    if args.chunk_mode == "fixed":
        require(
            args.fixed_payload_bytes is not None,
            "fixed chunk mode requires --fixed-payload-bytes",
        )
        require(
            args.fixed_payload_bytes <= MAX_UDP_PAYLOAD,
            "fixed payload exceeds the IPv4 UDP payload limit",
        )
        require(
            args.fixed_payload_bytes + 28 <= args.isl_mtu_bytes,
            "fixed payload would require IPv4 fragmentation",
        )
    else:
        require(
            args.fixed_payload_bytes is None,
            "size-aware mode must not set --fixed-payload-bytes",
        )
        largest_effective_payload = max(
            payload_bytes("size-aware", None, size)
            for task in tasks
            for size in (task["input_bytes"], task["output_bytes"])
        )
        require(
            largest_effective_payload <= 64000,
            "size-aware payload exceeds the frozen 64000-byte maximum",
        )
        require(
            largest_effective_payload + 28 <= args.isl_mtu_bytes,
            "size-aware payload would require IPv4 fragmentation",
        )

    distances = all_pairs_hops(graph)
    input_packets = 0
    result_packets = 0
    input_packet_hops = 0
    result_packet_hops = 0
    input_byte_hops = 0
    input_hop_counts = Counter()
    result_hop_counts = Counter()
    max_packets_per_transfer = 0
    per_source_transfers = Counter()
    incident_bandwidth_bps = defaultdict(list)
    link_bandwidth_kbps = []
    for link in links:
        bandwidth_kbps = link["link_bandwidth"]
        bandwidth_bps = bandwidth_kbps * 1000
        link_bandwidth_kbps.append(bandwidth_kbps)
        incident_bandwidth_bps[link["node1_id"]].append(bandwidth_bps)
        incident_bandwidth_bps[link["node2_id"]].append(bandwidth_bps)

    maximum_input_serialization_s = 0.0
    maximum_result_serialization_s = 0.0
    maximum_task_lower_bound_s = 0.0
    rates = {
        node["node_id"]: node["compute_rate_work_units_per_second"]
        for node in compute_nodes
    }
    for task in tasks:
        input_count = packet_count(
            args.chunk_mode,
            args.fixed_payload_bytes,
            task["input_bytes"],
        )
        result_count = packet_count(
            args.chunk_mode,
            args.fixed_payload_bytes,
            task["output_bytes"],
        )
        input_hops = distances[
            (task["source_node_id"], task["compute_node_id"])
        ]
        result_hops = distances[
            (task["compute_node_id"], task["result_node_id"])
        ]
        input_packets += input_count
        result_packets += result_count
        input_packet_hops += input_count * input_hops
        result_packet_hops += result_count * result_hops
        input_byte_hops += task["input_bytes"] * input_hops
        input_hop_counts[input_hops] += 1
        result_hop_counts[result_hops] += 1
        max_packets_per_transfer = max(
            max_packets_per_transfer,
            input_count,
            result_count,
        )
        per_source_transfers[task["source_node_id"]] += 1
        per_source_transfers[task["compute_node_id"]] += 1

        input_first_hop_rate = max(
            incident_bandwidth_bps[task["source_node_id"]]
        )
        result_first_hop_rate = max(
            incident_bandwidth_bps[task["compute_node_id"]]
        )
        input_serialization_s = (
            task["input_bytes"] * 8 / input_first_hop_rate
        )
        result_serialization_s = (
            task["output_bytes"] * 8 / result_first_hop_rate
        )
        compute_service_s = (
            task["compute_work_units"] / rates[task["compute_node_id"]]
        )
        maximum_input_serialization_s = max(
            maximum_input_serialization_s,
            input_serialization_s,
        )
        maximum_result_serialization_s = max(
            maximum_result_serialization_s,
            result_serialization_s,
        )
        maximum_task_lower_bound_s = max(
            maximum_task_lower_bound_s,
            input_serialization_s
            + compute_service_s
            + result_serialization_s,
        )

    total_packets = input_packets + result_packets
    estimated_packet_hops = input_packet_hops + result_packet_hops
    if args.abort_above_derived_packets is not None:
        require(
            total_packets <= args.abort_above_derived_packets,
            "derived packets exceed the optional local protection threshold",
        )
    require(
        max(per_source_transfers.values()) <= MAX_SOURCE_PORTS,
        "a source satellite needs more than 55536 UDP source ports",
    )

    earliest_arrival_ns = min(task["arrival_time_ns"] for task in tasks)
    latest_arrival_ns = max(task["arrival_time_ns"] for task in tasks)
    configured_arrival_span_s = max(
        1e-9,
        (summary["arrival_end_ns"] - summary["arrival_start_ns"]) / 1e9,
    )
    active_arrival_span_s = max(
        1e-9,
        (latest_arrival_ns - earliest_arrival_ns) / 1e9,
    )
    global_average_input_bps = total_input * 8 / configured_arrival_span_s

    tasks_by_compute = defaultdict(list)
    for task in tasks:
        tasks_by_compute[task["compute_node_id"]].append(task)
    compute_metrics = {}
    pure_compute_lower_bound_s = 0.0
    for node in compute_nodes:
        node_id = node["node_id"]
        node_tasks = tasks_by_compute[node_id]
        node_work = sum(task["compute_work_units"] for task in node_tasks)
        rate = node["compute_rate_work_units_per_second"]
        service_s = node_work / rate
        pure_compute_lower_bound_s = max(
            pure_compute_lower_bound_s,
            service_s,
        )
        compute_metrics[str(node_id)] = {
            "task_count": len(node_tasks),
            "total_work_units": node_work,
            "compute_rate_work_units_per_second": rate,
            "pure_service_time_s": service_s,
            "full_run_theoretical_utilization": (
                service_s / args.simulation_duration_s
            ),
            "arrival_window_theoretical_utilization": (
                service_s / active_arrival_span_s
            ),
        }

    recommended_duration_s = (
        latest_arrival_ns / 1e9
        + maximum_input_serialization_s
        + pure_compute_lower_bound_s
        + maximum_result_serialization_s
    )
    undirected_link_count = len(links)
    device_queue_count = 2 * undirected_link_count
    aggregate_queue_bytes = device_queue_count * args.isl_queue_bytes
    min_bandwidth_kbps = min(link_bandwidth_kbps)
    max_bandwidth_kbps = max(link_bandwidth_kbps)
    min_bandwidth_bps = min_bandwidth_kbps * 1000
    max_bandwidth_bps = max_bandwidth_kbps * 1000
    directed_aggregate_capacity_bps = 2 * sum(
        bandwidth_kbps * 1000 for bandwidth_kbps in link_bandwidth_kbps
    )
    mean_input_hops = input_byte_hops / total_input
    coarse_effective_capacity_bps = (
        directed_aggregate_capacity_bps / max(1.0, mean_input_hops)
    )

    warnings = []
    if total_packets >= 1_000_000:
        warnings.append(f"derived packet count is large: {total_packets}")
    if estimated_packet_hops >= 10_000_000:
        warnings.append(
            f"estimated packet-hop count is large: {estimated_packet_hops}"
        )
    aggregate_queue_gib = aggregate_queue_bytes / (1 << 30)
    if aggregate_queue_gib >= 8:
        warnings.append(
            "aggregate theoretical queue capacity is large: "
            f"{aggregate_queue_gib:.3f} GiB"
        )
    overloaded_nodes = [
        node_id
        for node_id, metrics in compute_metrics.items()
        if metrics["full_run_theoretical_utilization"] > 1
    ]
    if overloaded_nodes:
        warnings.append(
            "full-run theoretical compute utilization exceeds 1 on nodes: "
            + ",".join(overloaded_nodes)
        )
    if maximum_task_lower_bound_s > args.simulation_duration_s:
        warnings.append(
            "a single task lower bound exceeds simulation duration"
        )
    if latest_arrival_ns / 1e9 > 0.9 * args.simulation_duration_s:
        warnings.append("last task arrival is later than 90% of the run")
    if recommended_duration_s > args.simulation_duration_s:
        warnings.append(
            "recommended lower-bound heuristic exceeds simulation duration"
        )
    if global_average_input_bps >= 0.8 * coarse_effective_capacity_bps:
        warnings.append(
            "average logical input rate approaches the coarse network "
            "capacity reference"
        )

    return {
        "status": "PASS",
        "warnings": warnings,
        "workload": {
            "task_count": len(tasks),
            "total_input_bytes": total_input,
            "total_output_bytes": total_output,
            "total_application_bytes": total_input + total_output,
            "total_compute_work_units": total_work,
            "profile_counts": summary["profile_counts"],
            "large_1gb_count": summary["large_1gb_count"],
            "large_500mb_count": summary["large_500mb_count"],
            "derived_input_packets": input_packets,
            "derived_result_packets": result_packets,
            "derived_total_packets": total_packets,
            "max_packets_per_transfer": max_packets_per_transfer,
            "estimated_input_packet_hops": input_packet_hops,
            "estimated_result_packet_hops": result_packet_hops,
            "estimated_total_packet_hops": estimated_packet_hops,
            "earliest_arrival_ns": earliest_arrival_ns,
            "latest_arrival_ns": latest_arrival_ns,
            "global_average_logical_input_bps": global_average_input_bps,
            "minimum_pure_compute_completion_lower_bound_s": (
                pure_compute_lower_bound_s
            ),
            "maximum_input_first_hop_serialization_lower_bound_s": (
                maximum_input_serialization_s
            ),
            "maximum_result_first_hop_serialization_lower_bound_s": (
                maximum_result_serialization_s
            ),
            "maximum_single_task_pipeline_lower_bound_s": (
                maximum_task_lower_bound_s
            ),
            "recommended_minimum_simulation_duration_s": (
                recommended_duration_s
            ),
            "per_class_spearman_input_work": correlations,
            "per_class_largest_quartile_mean_work": largest_means,
            "per_class_smallest_quartile_mean_work": smallest_means,
        },
        "ports": {
            "per_source_node_transfer_count": {
                str(node_id): per_source_transfers[node_id]
                for node_id in sorted(per_source_transfers)
            },
            "per_source_node_udp_source_port_usage": {
                str(node_id): per_source_transfers[node_id]
                for node_id in sorted(per_source_transfers)
            },
            "maximum_ports_used_by_one_source": max(
                per_source_transfers.values()
            ),
            "available_ports_per_source": MAX_SOURCE_PORTS,
        },
        "compute": {
            "node_count": len(compute_nodes),
            "per_node": compute_metrics,
        },
        "network": {
            "satellite_count": len(topology_ids),
            "undirected_isl_count": undirected_link_count,
            "link_bandwidth_min_kbps": min_bandwidth_kbps,
            "link_bandwidth_max_kbps": max_bandwidth_kbps,
            "link_bandwidth_min_bps": min_bandwidth_bps,
            "link_bandwidth_max_bps": max_bandwidth_bps,
            "directed_aggregate_capacity_bps": (
                directed_aggregate_capacity_bps
            ),
            "coarse_effective_application_capacity_bps": (
                coarse_effective_capacity_bps
            ),
            "mean_input_shortest_hops_weighted_by_bytes": mean_input_hops,
            "input_shortest_hop_distribution": {
                str(hops): count
                for hops, count in sorted(input_hop_counts.items())
            },
            "result_shortest_hop_distribution": {
                str(hops): count
                for hops, count in sorted(result_hop_counts.items())
            },
        },
        "queue": {
            "point_to_point_device_queue_count": device_queue_count,
            "queue_bytes_per_device": args.isl_queue_bytes,
            "aggregate_theoretical_queue_bytes": aggregate_queue_bytes,
            "aggregate_theoretical_queue_gib": aggregate_queue_gib,
            "queue_drain_time_at_min_link_rate_ms": (
                args.isl_queue_bytes * 8 * 1000 / min_bandwidth_bps
            ),
            "queue_drain_time_at_max_link_rate_ms": (
                args.isl_queue_bytes * 8 * 1000 / max_bandwidth_bps
            ),
            "capacity_is_preallocated": False,
        },
        "run_contract": {
            "simulation_duration_s": args.simulation_duration_s,
            "chunk_mode": args.chunk_mode,
            "fixed_payload_bytes": args.fixed_payload_bytes,
            "isl_mtu_bytes": args.isl_mtu_bytes,
            "isl_queue_bytes": args.isl_queue_bytes,
            "max_total_input_bytes": args.max_total_input_bytes,
            "abort_above_derived_packets": (
                args.abort_above_derived_packets
            ),
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="Preflight one SatCompute TaskTrace workload."
    )
    parser.add_argument("--topology-dir", required=True, type=Path)
    parser.add_argument("--compute-profile", required=True, type=Path)
    parser.add_argument("--task-trace", required=True, type=Path)
    parser.add_argument("--workload-summary", required=True, type=Path)
    parser.add_argument(
        "--simulation-duration-s",
        required=True,
        type=positive_finite,
    )
    parser.add_argument(
        "--chunk-mode",
        required=True,
        choices=("fixed", "size-aware"),
    )
    parser.add_argument("--fixed-payload-bytes", type=positive_int)
    parser.add_argument("--isl-mtu-bytes", required=True, type=positive_int)
    parser.add_argument("--isl-queue-bytes", required=True, type=positive_int)
    parser.add_argument(
        "--max-total-input-bytes",
        type=positive_int,
        default=109_000_000_000,
    )
    parser.add_argument(
        "--abort-above-derived-packets",
        type=positive_int,
        help="optional local-machine protection; disabled by default",
    )
    parser.add_argument("--output-report", type=Path)
    args = parser.parse_args()

    simulation_duration_ns = round(args.simulation_duration_s * 1e9)
    require(
        0 < simulation_duration_ns <= INT64_MAX,
        "simulation duration cannot be represented by int64 nanoseconds",
    )
    topology_ids, links, graph = read_topology(args.topology_dir)
    compute_nodes = read_compute_profile(
        args.compute_profile,
        topology_ids,
    )
    tasks = read_task_trace(
        args.task_trace,
        topology_ids,
        compute_nodes,
        simulation_duration_ns,
    )
    (
        summary,
        _assignments,
        correlations,
        largest_means,
        smallest_means,
    ) = validate_workload_summary(
        args.workload_summary,
        args.task_trace,
        tasks,
        compute_nodes,
    )
    report = analyze_workload(
        topology_ids,
        links,
        graph,
        compute_nodes,
        tasks,
        summary,
        correlations,
        largest_means,
        smallest_means,
        args,
    )
    if args.output_report is not None:
        write_json(args.output_report, report)

    workload = report["workload"]
    network = report["network"]
    queue = report["queue"]
    print("PASS: workload preflight")
    print(f"  tasks                 : {workload['task_count']}")
    print(f"  input bytes           : {workload['total_input_bytes']}")
    print(f"  output bytes          : {workload['total_output_bytes']}")
    print(
        "  derived packets       : "
        f"{workload['derived_total_packets']} "
        f"(INPUT={workload['derived_input_packets']}, "
        f"RESULT={workload['derived_result_packets']})"
    )
    print(
        "  estimated packet hops : "
        f"{workload['estimated_total_packet_hops']}"
    )
    print(
        "  recommended duration  : "
        f"{workload['recommended_minimum_simulation_duration_s']:.3f} s "
        "(lower-bound heuristic)"
    )
    print(
        "  topology              : "
        f"{network['satellite_count']} satellites, "
        f"{network['undirected_isl_count']} ISLs, "
        f"{network['link_bandwidth_min_kbps']}.."
        f"{network['link_bandwidth_max_kbps']} kbps"
    )
    print(
        "  device queues         : "
        f"{queue['point_to_point_device_queue_count']} x "
        f"{queue['queue_bytes_per_device']} bytes"
    )
    print(
        "  theoretical capacity  : "
        f"{queue['aggregate_theoretical_queue_gib']:.3f} GiB "
        "(not preallocated)"
    )
    if args.output_report is not None:
        print(f"  report                : {args.output_report}")
    for warning in report["warnings"]:
        print(f"WARNING: {warning}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
