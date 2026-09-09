#!/usr/bin/env python3
"""Generate deterministic TaskTrace inputs for SatCompute."""

import argparse
import json
import math
import runpy
from collections import defaultdict
from pathlib import Path


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1
BASIS_POINTS = 10000
MIB = 1 << 20
ONE_GB = 1_000_000_000
FIVE_HUNDRED_MB = 500_000_000

CLASS_ORDER = (
    "image-enhancement",
    "image-detection",
    "dnn-inference",
    "preprocess-compress",
)
DEFAULT_CLASS_SHARES = {
    "image-enhancement": 1500,
    "image-detection": 2500,
    "dnn-inference": 3500,
    "preprocess-compress": 2500,
}
DEFAULT_TAIL_SHARES = {
    "preprocess-compress": 7500,
    "image-enhancement": 2500,
}
INPUT_WEIGHT_MULTIPLIERS = {
    "image-enhancement": 3,
    "image-detection": 2,
    "dnn-inference": 1,
    "preprocess-compress": 4,
}
WORK_RANGES = {
    "image-enhancement": (2_000_000, 5_000_000),
    "image-detection": (1_500_000, 4_500_000),
    "dnn-inference": (800_000, 4_000_000),
    "preprocess-compress": (200_000, 2_000_000),
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


def read_satellite_ids(path):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) not in (
        {"nodes"},
        {"simulation_time_ns", "nodes"},
    ):
        raise ValueError("nodes file must be a topology node slice")
    if not isinstance(root["nodes"], list) or len(root["nodes"]) < 3:
        raise ValueError("nodes file must contain at least three satellites")

    satellite_ids = []
    for node in root["nodes"]:
        if not isinstance(node, dict) or set(node) not in (
            {"node_id", "node_type"},
            {"node_id", "node_type", "x", "y", "z"},
        ):
            raise ValueError("invalid satellite node object")
        require_integer(node["node_id"], "node_id", 0, UINT32_MAX)
        if node["node_type"] != "sat":
            raise ValueError("only satellite nodes are supported")
        satellite_ids.append(node["node_id"])
    if len(set(satellite_ids)) != len(satellite_ids):
        raise ValueError("nodes file contains duplicate node IDs")
    return sorted(satellite_ids)


def read_compute_profile(path, satellite_ids):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) != {"compute_nodes"}:
        raise ValueError("ComputeProfile root must contain only compute_nodes")
    if not isinstance(root["compute_nodes"], list) or not root["compute_nodes"]:
        raise ValueError("ComputeProfile compute_nodes must be non-empty")

    valid_ids = set(satellite_ids)
    compute_nodes = []
    seen = set()
    for node in root["compute_nodes"]:
        if not isinstance(node, dict) or set(node) != {
            "node_id",
            "compute_rate_work_units_per_second",
        }:
            raise ValueError("invalid ComputeProfile node object")
        node_id = node["node_id"]
        rate = node["compute_rate_work_units_per_second"]
        require_integer(node_id, "compute node_id", 0, UINT32_MAX)
        require_integer(rate, "compute rate", 1, UINT64_MAX)
        if node_id not in valid_ids or node_id in seen:
            raise ValueError("ComputeProfile contains an unknown or duplicate node")
        seen.add(node_id)
        compute_nodes.append(
            {
                "node_id": node_id,
                "compute_rate_work_units_per_second": rate,
            }
        )
    return sorted(compute_nodes, key=lambda item: item["node_id"])


def deterministic_value(seed, task_id, field_name):
    value = 14695981039346656037
    for byte in f"{seed}\0{task_id}\0{field_name}".encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & UINT64_MAX
    return value


def largest_remainder(total, shares, order, denominator=BASIS_POINTS):
    if total < 0 or denominator <= 0:
        raise ValueError("invalid largest-remainder total or denominator")
    if set(shares) != set(order):
        raise ValueError("largest-remainder shares and order differ")
    if any(
        not isinstance(shares[name], int)
        or isinstance(shares[name], bool)
        or shares[name] < 0
        for name in order
    ):
        raise ValueError("largest-remainder shares must be non-negative integers")
    if sum(shares.values()) != denominator:
        raise ValueError("largest-remainder shares do not sum to denominator")

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


def bounded_weighted_allocation(total, weights, minimum, maximum, tie_keys):
    count = len(weights)
    if count == 0:
        if total != 0:
            raise ValueError("cannot allocate a non-zero budget to zero tasks")
        return []
    if (
        len(tie_keys) != count
        or minimum <= 0
        or maximum < minimum
        or any(weight <= 0 for weight in weights)
    ):
        raise ValueError("invalid bounded allocation inputs")
    if total < count * minimum or total > count * maximum:
        raise ValueError(
            "input budget conflicts with non-tail minimum/maximum constraints"
        )

    values = [minimum] * count
    capacities = [maximum - minimum] * count
    remaining = total - count * minimum
    active = set(range(count))
    while remaining > 0:
        total_weight = sum(weights[index] for index in active)
        saturated = [
            index
            for index in active
            if remaining * weights[index]
            >= capacities[index] * total_weight
        ]
        if saturated:
            for index in saturated:
                values[index] += capacities[index]
                remaining -= capacities[index]
                capacities[index] = 0
                active.remove(index)
            continue

        floors = {
            index: remaining * weights[index] // total_weight
            for index in active
        }
        for index, amount in floors.items():
            values[index] += amount
            capacities[index] -= amount
        leftover = remaining - sum(floors.values())
        ranked = sorted(
            active,
            key=lambda index: (
                -(remaining * weights[index] % total_weight),
                tie_keys[index],
            ),
        )
        for index in ranked[:leftover]:
            values[index] += 1
            capacities[index] -= 1
        remaining = 0
    if sum(values) != total or any(
        not minimum <= value <= maximum for value in values
    ):
        raise AssertionError("bounded allocation failed its exact-budget contract")
    return values


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
    if len(first) != len(second) or len(first) < 2:
        raise ValueError("Spearman correlation needs equal non-trivial samples")
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
    if first_variance == 0 or second_variance == 0:
        return 0.0
    return covariance / math.sqrt(first_variance * second_variance)


def scaled_tail_count(maximum_count, scale_bp):
    shares = {
        "selected": scale_bp,
        "unselected": BASIS_POINTS - scale_bp,
    }
    return largest_remainder(
        maximum_count,
        shares,
        ("selected", "unselected"),
    )["selected"]


def assign_classes(task_ids, class_counts, seed):
    ordered_ids = sorted(
        task_ids,
        key=lambda task_id: (
            deterministic_value(seed, task_id, "class-permutation"),
            task_id,
        ),
    )
    assignments = {}
    offset = 0
    for class_name in CLASS_ORDER:
        next_offset = offset + class_counts[class_name]
        for task_id in ordered_ids[offset:next_offset]:
            assignments[task_id] = class_name
        offset = next_offset
    if offset != len(task_ids) or len(assignments) != len(task_ids):
        raise AssertionError("class assignment did not cover every task")
    return assignments


def assign_tail_tasks(
    class_assignments,
    one_gb_count,
    five_hundred_mb_count,
    tail_shares,
    seed,
):
    tail_order = ("preprocess-compress", "image-enhancement")
    counts_by_size = {
        "1gb": largest_remainder(
            one_gb_count,
            tail_shares,
            tail_order,
        ),
        "500mb": largest_remainder(
            five_hundred_mb_count,
            tail_shares,
            tail_order,
        ),
    }
    available = defaultdict(list)
    for task_id, class_name in class_assignments.items():
        available[class_name].append(task_id)

    selected = {}
    for size_name in ("1gb", "500mb"):
        for class_name in tail_order:
            required = counts_by_size[size_name][class_name]
            candidates = sorted(
                available[class_name],
                key=lambda task_id: (
                    deterministic_value(
                        seed,
                        task_id,
                        f"tail-{size_name}-{class_name}",
                    ),
                    task_id,
                ),
            )
            if len(candidates) < required:
                raise ValueError(
                    f"{class_name} class quota cannot hold the requested tail"
                )
            chosen = candidates[:required]
            for task_id in chosen:
                selected[task_id] = size_name
            chosen_set = set(chosen)
            available[class_name] = [
                task_id
                for task_id in available[class_name]
                if task_id not in chosen_set
            ]
    return selected, counts_by_size


def assign_balanced_nodes(
    task_ids,
    node_ids,
    seed,
    field_name,
    forbidden=None,
):
    if not node_ids:
        raise ValueError("balanced node assignment needs candidate nodes")
    forbidden = forbidden or {}
    ordered_tasks = sorted(
        task_ids,
        key=lambda task_id: (
            deterministic_value(seed, task_id, f"{field_name}-permutation"),
            task_id,
        ),
    )
    offset = deterministic_value(seed, 0, f"{field_name}-offset") % len(
        node_ids
    )
    assignments = {}
    for position, task_id in enumerate(ordered_tasks):
        candidate_index = (offset + position) % len(node_ids)
        candidate = node_ids[candidate_index]
        if candidate == forbidden.get(task_id):
            shift = 1 + (
                deterministic_value(seed, task_id, f"{field_name}-shift")
                % (len(node_ids) - 1)
            )
            candidate = node_ids[(candidate_index + shift) % len(node_ids)]
            if candidate == forbidden.get(task_id):
                candidate = node_ids[(candidate_index + shift + 1) % len(node_ids)]
        if candidate == forbidden.get(task_id):
            raise AssertionError(f"{field_name} could not avoid forbidden node")
        assignments[task_id] = candidate
    return assignments


def generate_arrivals(
    task_ids,
    start_ns,
    end_ns,
    mode,
    seed,
):
    if end_ns < start_ns:
        raise ValueError("arrival end must not precede arrival start")
    span = end_ns - start_ns + 1
    if mode == "uniform":
        if span < len(task_ids):
            raise ValueError("arrival window is too small for stratified uniform mode")
        ordered_tasks = sorted(
            task_ids,
            key=lambda task_id: (
                deterministic_value(
                    seed,
                    task_id,
                    "arrival-permutation",
                ),
                task_id,
            ),
        )
        arrivals = {}
        for stratum, task_id in enumerate(ordered_tasks):
            lower = start_ns + span * stratum // len(task_ids)
            upper = start_ns + span * (stratum + 1) // len(task_ids) - 1
            jitter = (
                deterministic_value(seed, task_id, "arrival-jitter")
                % (upper - lower + 1)
            )
            arrivals[task_id] = lower + jitter
        return arrivals

    burst_span = max(1, (span + 9) // 10)
    return {
        task_id: start_ns
        + deterministic_value(seed, task_id, "arrival-burst")
        % burst_span
        for task_id in task_ids
    }


def allocate_input_bytes(
    task_ids,
    class_assignments,
    tail_assignments,
    total_input_bytes,
    minimum,
    maximum,
    seed,
):
    sizes = {}
    for task_id, size_name in tail_assignments.items():
        sizes[task_id] = (
            ONE_GB if size_name == "1gb" else FIVE_HUNDRED_MB
        )
    tail_budget = sum(sizes.values())
    non_tail_ids = [
        task_id for task_id in task_ids if task_id not in tail_assignments
    ]
    remaining_budget = total_input_bytes - tail_budget
    if remaining_budget < 0:
        raise ValueError("fixed large tail exceeds total input budget")

    weights = [
        INPUT_WEIGHT_MULTIPLIERS[class_assignments[task_id]]
        * (
            1
            + deterministic_value(seed, task_id, "input-weight")
            % 1_000_000
        )
        for task_id in non_tail_ids
    ]
    tie_keys = [
        (
            deterministic_value(seed, task_id, "input-remainder"),
            task_id,
        )
        for task_id in non_tail_ids
    ]
    allocated = bounded_weighted_allocation(
        remaining_budget,
        weights,
        minimum,
        maximum,
        tie_keys,
    )
    sizes.update(dict(zip(non_tail_ids, allocated)))
    if len(sizes) != len(task_ids) or sum(sizes.values()) != total_input_bytes:
        raise AssertionError("input size allocation violated the exact budget")
    return sizes


def inclusive_hash_range(
    minimum,
    maximum,
    seed,
    task_id,
    field_name,
):
    return minimum + deterministic_value(
        seed,
        task_id,
        field_name,
    ) % (maximum - minimum + 1)


def generate_output_bytes(
    task_ids,
    class_assignments,
    input_sizes,
    seed,
):
    outputs = {}
    for task_id in task_ids:
        class_name = class_assignments[task_id]
        if class_name == "image-enhancement":
            ratio_bp = inclusive_hash_range(
                8000,
                10000,
                seed,
                task_id,
                "output-ratio-bp",
            )
            value = (input_sizes[task_id] * ratio_bp + 9999) // 10000
        elif class_name == "image-detection":
            ratio_bp = inclusive_hash_range(
                50,
                500,
                seed,
                task_id,
                "output-ratio-bp",
            )
            value = (input_sizes[task_id] * ratio_bp + 9999) // 10000
        elif class_name == "dnn-inference":
            value = inclusive_hash_range(
                4096,
                262144,
                seed,
                task_id,
                "output-bytes",
            )
        else:
            ratio_bp = inclusive_hash_range(
                1000,
                4000,
                seed,
                task_id,
                "output-ratio-bp",
            )
            value = (input_sizes[task_id] * ratio_bp + 9999) // 10000
        if not 1 <= value <= UINT64_MAX:
            raise ValueError("generated output_bytes exceeds uint64")
        outputs[task_id] = value
    return outputs


def generate_work_units(
    task_ids,
    class_assignments,
    input_sizes,
    seed,
):
    work = {}
    for class_name in CLASS_ORDER:
        class_tasks = sorted(
            (
                task_id
                for task_id in task_ids
                if class_assignments[task_id] == class_name
            ),
            key=lambda task_id: (
                input_sizes[task_id],
                deterministic_value(seed, task_id, "work-tie"),
                task_id,
            ),
        )
        minimum, maximum = WORK_RANGES[class_name]
        span = maximum - minimum
        if len(class_tasks) == 1:
            work[class_tasks[0]] = (minimum + maximum) // 2
            continue
        jitter_limit = max(1, span // (4 * (len(class_tasks) - 1)))
        for rank, task_id in enumerate(class_tasks):
            base = minimum + span * rank // (len(class_tasks) - 1)
            jitter = inclusive_hash_range(
                -jitter_limit,
                jitter_limit,
                seed,
                task_id,
                "work-jitter",
            )
            work[task_id] = min(maximum, max(minimum, base + jitter))
    if len(work) != len(task_ids):
        raise AssertionError("work generation did not cover every task")
    return work


def class_correlation_metrics(
    task_ids,
    class_assignments,
    input_sizes,
    work_units,
):
    correlations = {}
    largest_means = {}
    smallest_means = {}
    for class_name in CLASS_ORDER:
        class_tasks = [
            task_id
            for task_id in task_ids
            if class_assignments[task_id] == class_name
        ]
        inputs = [input_sizes[task_id] for task_id in class_tasks]
        work = [work_units[task_id] for task_id in class_tasks]
        if len(class_tasks) < 4:
            correlations[class_name] = None
            largest_means[class_name] = None
            smallest_means[class_name] = None
            continue
        correlation = spearman_correlation(inputs, work)
        ordered = sorted(
            class_tasks,
            key=lambda task_id: (input_sizes[task_id], task_id),
        )
        quartile_count = max(1, len(ordered) // 4)
        smallest_mean = (
            sum(work_units[task_id] for task_id in ordered[:quartile_count])
            / quartile_count
        )
        largest_mean = (
            sum(work_units[task_id] for task_id in ordered[-quartile_count:])
            / quartile_count
        )
        if correlation <= 0 or largest_mean <= smallest_mean:
            raise AssertionError(
                f"{class_name} input/work correlation contract failed"
            )
        correlations[class_name] = correlation
        largest_means[class_name] = largest_mean
        smallest_means[class_name] = smallest_mean
    return correlations, largest_means, smallest_means


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def distribution(values):
    return {
        "min": min(values),
        "mean": sum(values) / len(values),
        "max": max(values),
    }


def build_f1_validation_workload(satellite_ids, compute_nodes, seed):
    """Build the fixed 66-star, 20-task N4B F1 validation workload."""
    if len(satellite_ids) != 66:
        raise ValueError("f1-validation profile requires exactly 66 satellites")
    if len(compute_nodes) < 6:
        raise ValueError("f1-validation profile requires at least six compute nodes")

    role_nodes = [compute_nodes[index * len(compute_nodes) // 6] for index in range(6)]
    hotspot_nodes = role_nodes[:3]
    risk_only_node = role_nodes[3]
    control_nodes = role_nodes[4:]
    tasks = []
    hotspot_task_ids = {}
    post_recovery_task_ids = []
    expected_critical_failure_task_ids = []

    def add_task(compute_node, duration_seconds, arrival_time_ns):
        task_id = len(tasks) + 1
        compute_node_id = compute_node["node_id"]
        source_index = deterministic_value(seed, task_id, "f1-source") % 66
        while satellite_ids[source_index] == compute_node_id:
            source_index = (source_index + 1) % 66
        result_index = deterministic_value(seed, task_id, "f1-result") % 66
        while satellite_ids[result_index] in (
            compute_node_id,
            satellite_ids[source_index],
        ):
            result_index = (result_index + 1) % 66
        compute_work_units = (
            compute_node["compute_rate_work_units_per_second"] * duration_seconds
        )
        if compute_work_units > UINT64_MAX:
            raise ValueError("f1-validation compute work exceeds uint64")
        tasks.append(
            {
                "task_id": task_id,
                "source_node_id": satellite_ids[source_index],
                "compute_node_id": compute_node_id,
                "result_node_id": satellite_ids[result_index],
                "input_bytes": 4096,
                "output_bytes": 2048,
                "compute_work_units": compute_work_units,
                "arrival_time_ns": arrival_time_ns,
            }
        )
        return task_id

    for hotspot_index, node in enumerate(hotspot_nodes):
        start_time_ns = 100_000_000 + hotspot_index * 10_000_000_000
        task_ids = [
            add_task(node, 15, start_time_ns + task_index * 1_000_000_000)
            for task_index in range(4)
        ]
        hotspot_task_ids[str(node["node_id"])] = task_ids
        expected_critical_failure_task_ids.append(task_ids[-1])
        post_recovery_task_ids.append(
            add_task(node, 2, 67_000_000_000 + hotspot_index * 10_000_000_000)
        )

    risk_only_task_ids = [
        add_task(risk_only_node, 15, 100_000_000 + task_index * 1_000_000_000)
        for task_index in range(3)
    ]
    control_task_ids = [
        add_task(node, 2, 1_000_000_000 + index * 1_000_000_000)
        for index, node in enumerate(control_nodes)
    ]
    if len(tasks) != 20:
        raise AssertionError("f1-validation profile must contain exactly 20 tasks")

    summary = {
        "profile": "f1-validation",
        "seed": seed,
        "task_count": len(tasks),
        "hotspot_compute_node_ids": [node["node_id"] for node in hotspot_nodes],
        "hotspot_task_ids": hotspot_task_ids,
        "expected_critical_failure_task_ids": expected_critical_failure_task_ids,
        "post_recovery_task_ids": post_recovery_task_ids,
        "risk_only_compute_node_id": risk_only_node["node_id"],
        "risk_only_task_ids": risk_only_task_ids,
        "control_compute_node_ids": [node["node_id"] for node in control_nodes],
        "control_task_ids": control_task_ids,
        "hotspot_task_duration_s": 15,
        "risk_only_task_duration_s": 15,
        "post_recovery_task_duration_s": 2,
        "control_task_duration_s": 2,
    }
    return {"tasks": tasks}, summary


def build_f2_validation_workload(satellite_ids, compute_nodes, seed):
    """Build the fixed 66-star, 8-task N4B F2 validation workload."""
    if len(satellite_ids) != 66:
        raise ValueError("f2-validation profile requires exactly 66 satellites")
    compute_by_id = {node["node_id"]: node for node in compute_nodes}
    required_compute_ids = (0, 11, 16, 17, 27, 28)
    if any(node_id not in compute_by_id for node_id in required_compute_ids):
        raise ValueError(
            "f2-validation profile requires compute nodes 0, 11, 16, 17, 27, and 28"
        )

    tasks = []

    def add_task(compute_node_id, duration_seconds, arrival_time_ns, role):
        task_id = len(tasks) + 1
        source_index = deterministic_value(seed, task_id, "f2-source") % 66
        while satellite_ids[source_index] == compute_node_id:
            source_index = (source_index + 1) % 66
        result_index = deterministic_value(seed, task_id, "f2-result") % 66
        while satellite_ids[result_index] in (
            compute_node_id,
            satellite_ids[source_index],
        ):
            result_index = (result_index + 1) % 66
        compute_work_units = (
            compute_by_id[compute_node_id]["compute_rate_work_units_per_second"]
            * duration_seconds
        )
        if compute_work_units > UINT64_MAX:
            raise ValueError("f2-validation compute work exceeds uint64")
        tasks.append(
            {
                "task_id": task_id,
                "source_node_id": satellite_ids[source_index],
                "compute_node_id": compute_node_id,
                "result_node_id": satellite_ids[result_index],
                "input_bytes": 4096,
                "output_bytes": 2048,
                "compute_work_units": compute_work_units,
                "arrival_time_ns": arrival_time_ns,
            }
        )
        return {"task_id": task_id, "role": role}

    roles = [
        add_task(17, 60, 200_100_000_000, "long-window-a"),
        add_task(17, 10, 245_100_000_000, "follow-up-a"),
        add_task(16, 60, 820_100_000_000, "long-window-b"),
        add_task(16, 10, 859_100_000_000, "follow-up-b"),
        add_task(28, 20, 200_100_000_000, "medium-window-a"),
        add_task(27, 20, 700_100_000_000, "medium-window-b"),
        add_task(0, 5, 100_100_000_000, "short-control"),
        add_task(11, 5, 500_100_000_000, "short-control"),
    ]
    summary = {
        "profile": "f2-validation",
        "seed": seed,
        "task_count": len(tasks),
        "orbit_start_offset_s": 302,
        "random_seed": 1,
        "random_run": 16,
        "roles": roles,
        "long_task_compute_node_ids": [17, 16],
        "long_task_ids": [1, 3],
        "follow_up_task_ids": [2, 4],
        "medium_task_ids": [5, 6],
        "control_task_ids": [7, 8],
    }
    return {"tasks": tasks}, summary


def build_n4b_joint_validation_workload(satellite_ids, compute_nodes, seed):
    """Build the fixed 66-star, 100-task N4B joint acceptance workload."""
    if len(satellite_ids) != 66:
        raise ValueError(
            "n4b-joint-validation profile requires exactly 66 satellites"
        )
    compute_by_id = {node["node_id"]: node for node in compute_nodes}
    if set(compute_by_id) != set(satellite_ids):
        raise ValueError(
            "n4b-joint-validation profile requires all 66 satellites to compute"
        )

    strong_hotspot_ids = (0, 11, 22)
    boundary_hotspot_id = 33
    warm_control_id = 44
    fault_window_node_ids = (17, 28, 16, 4, 5)
    reserved_node_ids = set(strong_hotspot_ids) | {
        boundary_hotspot_id,
        warm_control_id,
        *fault_window_node_ids,
    }
    distributed_node_ids = [
        node_id for node_id in satellite_ids if node_id not in reserved_node_ids
    ]
    if len(distributed_node_ids) != 56:
        raise AssertionError("joint validation distributed-node set differs")

    tasks = []

    def add_task(compute_node_id, duration_seconds, arrival_time_ns):
        task_id = len(tasks) + 1
        source_index = deterministic_value(seed, task_id, "joint-source") % 66
        while satellite_ids[source_index] == compute_node_id:
            source_index = (source_index + 1) % 66
        result_index = deterministic_value(seed, task_id, "joint-result") % 66
        while satellite_ids[result_index] in (
            compute_node_id,
            satellite_ids[source_index],
        ):
            result_index = (result_index + 1) % 66
        compute_work_units = (
            compute_by_id[compute_node_id]["compute_rate_work_units_per_second"]
            * duration_seconds
        )
        if compute_work_units > UINT64_MAX:
            raise ValueError("joint validation compute work exceeds uint64")
        tasks.append(
            {
                "task_id": task_id,
                "source_node_id": satellite_ids[source_index],
                "compute_node_id": compute_node_id,
                "result_node_id": satellite_ids[result_index],
                "input_bytes": 4096,
                "output_bytes": 2048,
                "compute_work_units": compute_work_units,
                "arrival_time_ns": arrival_time_ns,
            }
        )
        return task_id

    strong_hotspot_task_ids = {}
    expected_critical_failure_task_ids = []
    post_recovery_task_ids = []
    for hotspot_index, node_id in enumerate(strong_hotspot_ids):
        start_time_ns = 100_000_000 + hotspot_index * 10_000_000_000
        continuous_task_ids = [
            add_task(node_id, 10, start_time_ns + task_index * 1_000_000_000)
            for task_index in range(6)
        ]
        expected_critical_failure_task_ids.append(continuous_task_ids[-1])
        post_recovery_task_id = add_task(
            node_id,
            2,
            67_100_000_000 + hotspot_index * 10_000_000_000,
        )
        post_recovery_task_ids.append(post_recovery_task_id)
        strong_hotspot_task_ids[str(node_id)] = {
            "continuous": continuous_task_ids,
            "post_recovery": post_recovery_task_id,
        }

    boundary_hotspot_task_ids = [
        add_task(
            boundary_hotspot_id,
            10,
            100_100_000_000 + task_index * 1_000_000_000,
        )
        for task_index in range(5)
    ]
    warm_control_task_ids = [
        add_task(
            warm_control_id,
            8,
            200_100_000_000 + task_index * 1_000_000_000,
        )
        for task_index in range(4)
    ]

    fault_window_specs = (
        (17, 20, 228_100_000_000, "f2-first-fault-active"),
        (17, 5, 245_100_000_000, "f2-first-post-recovery"),
        (28, 10, 200_100_000_000, "f2-risk-active"),
        (16, 20, 842_100_000_000, "f2-second-fault-active"),
        (16, 5, 859_100_000_000, "f2-second-post-recovery"),
        (4, 10, 824_100_000_000, "f3-fault-active"),
        (4, 3, 840_100_000_000, "f3-post-fault"),
        (5, 5, 825_100_000_000, "f3-neighbor-control"),
    )
    fault_window_roles = []
    for node_id, duration_seconds, arrival_time_ns, role in fault_window_specs:
        task_id = add_task(node_id, duration_seconds, arrival_time_ns)
        fault_window_roles.append(
            {
                "task_id": task_id,
                "compute_node_id": node_id,
                "duration_s": duration_seconds,
                "role": role,
            }
        )

    distributed_control_task_ids = []
    for control_index in range(62):
        node_id = distributed_node_ids[control_index % len(distributed_node_ids)]
        duration_seconds = 2 + (
            deterministic_value(seed, control_index + 1, "joint-control-duration")
            % 4
        )
        distributed_control_task_ids.append(
            add_task(
                node_id,
                duration_seconds,
                100_100_000_000 + control_index * 13_000_000_000,
            )
        )

    if len(tasks) != 100:
        raise AssertionError(
            "n4b-joint-validation profile must contain exactly 100 tasks"
        )

    summary = {
        "profile": "n4b-joint-validation",
        "seed": seed,
        "task_count": len(tasks),
        "simulation_duration_s": 1000,
        "orbit_start_offset_s": 302,
        "random_seed": 1,
        "random_run": 16,
        "strong_hotspot_compute_node_ids": list(strong_hotspot_ids),
        "strong_hotspot_task_ids": strong_hotspot_task_ids,
        "expected_critical_failure_task_ids": expected_critical_failure_task_ids,
        "post_recovery_task_ids": post_recovery_task_ids,
        "boundary_hotspot_compute_node_id": boundary_hotspot_id,
        "boundary_hotspot_task_ids": boundary_hotspot_task_ids,
        "warm_control_compute_node_id": warm_control_id,
        "warm_control_task_ids": warm_control_task_ids,
        "fault_window_roles": fault_window_roles,
        "distributed_control_compute_node_ids": distributed_node_ids,
        "distributed_control_task_ids": distributed_control_task_ids,
        "strong_hotspot_task_duration_s": 10,
        "boundary_hotspot_task_duration_s": 10,
        "warm_control_task_duration_s": 8,
        "post_recovery_task_duration_s": 2,
        "expected_f2_faults": [
            {"node_id": 17, "start_time_ns": 236_000_000_000},
            {"node_id": 16, "start_time_ns": 850_000_000_000},
        ],
        "expected_f3_fault": {
            "node_id": 4,
            "start_time_ns": 829_256_867_404,
        },
    }
    return {"tasks": tasks}, summary


def build_n4c_c800_workload(satellite_ids, compute_nodes, seed, candidate="C800"):
    """Package frozen G1 mappings or the G3 intensity variant with balanced endpoints."""
    if sorted(satellite_ids) != list(range(66)):
        raise ValueError("n4c-c800 requires satellite IDs 0..65")
    if (sorted(node["node_id"] for node in compute_nodes) != list(range(66)) or
            any(node["compute_rate_work_units_per_second"] != 100_000
                for node in compute_nodes)):
        raise ValueError("n4c-c800 requires all 66 nodes at 100000 WU/s")
    # Reuse the frozen G1 allocator and model; do not copy its byte/WU formulas.
    preview = runpy.run_path(str(Path(__file__).with_name("preview-n4c-workload.py")))
    if candidate not in ("C800", "C800-109G", "C800-TruncNormal", "C800-TruncNormal-v3"):
        raise ValueError("unsupported formal C800 candidate")
    horizon = 1050 if candidate == "C800-TruncNormal-v3" else 900 if candidate == "C800-TruncNormal" else 600
    duration = 1300 if candidate == "C800-TruncNormal-v3" else 1200 if candidate == "C800-TruncNormal" else 1000
    attributes = preview["preview_attributes"](seed, candidate)
    summary, budgets = preview["summarize_attributes"](attributes, simulation_seconds=duration)
    ids = [task["task_id"] for task in attributes]
    nodes = sorted(satellite_ids)
    compute = assign_balanced_nodes(ids, nodes, seed, "n4c-compute")
    source = assign_balanced_nodes(ids, nodes, seed, "n4c-source", compute)
    result = assign_balanced_nodes(ids, nodes, seed, "n4c-result", compute)
    arrivals = generate_arrivals(ids, 1_000_000_000, horizon * 10**9, "uniform", seed)
    tasks = [{**{key: row[key] for key in ("task_id", "task_profile", "input_bytes",
                                          "output_bytes", "compute_work_units")},
              "source_node_id": source[row["task_id"]],
              "compute_node_id": compute[row["task_id"]],
              "result_node_id": result[row["task_id"]],
              "arrival_time_ns": arrivals[row["task_id"]]} for row in budgets]
    summary.update(profile="n4c-c800", seed=seed, simulation_duration_s=duration,
                   arrival_window_s=[1, horizon], endpoint_assignment="non-geographic-balanced",
                   state_metadata="G1 model/preview only; not runtime checkpoint objects")
    if candidate == "C800-TruncNormal":
        preview["describe_truncnormal"](summary, attributes, budgets)
        summary.update(profile="n4c-c800-truncnormal", workload_candidate=candidate)
    if candidate == "C800-TruncNormal-v3":
        preview["describe_truncnormal_v3"](summary, attributes, budgets)
        summary.update(profile="n4c-c800-truncnormal-v3", workload_candidate=candidate)
    if candidate == "C800-109G":
        frozen = preview["preview_attributes"](seed, "C800")
        for before, after in zip(frozen, attributes):
            if {k: v for k, v in before.items() if k != "input_bytes"} != {
                    k: v for k, v in after.items() if k != "input_bytes"}:
                raise AssertionError("109 GB changed frozen task attributes")
            if before["task_profile"] == "llm" or before["input_bytes"] >= 500_000_000:
                if before != after:
                    raise AssertionError("109 GB changed a frozen LLM or tail task")
        frozen_llm_bytes = sum(t["input_bytes"] for t in frozen if t["task_profile"] == "llm")
        if frozen_llm_bytes != sum(t["input_bytes"] for t in attributes if t["task_profile"] == "llm"):
            raise AssertionError("LLM input budget changed")
        summary.update(profile="n4c-c800-109g", workload_candidate=candidate,
                       purpose="g3-stress-variant-derived-from-frozen-g1-model",
                       frozen_llm_input_bytes=frozen_llm_bytes,
                       ordinary_image_input_bytes=sum(t["input_bytes"] for t in attributes
                           if t["task_profile"] != "llm" and t["input_bytes"] < 500_000_000))
    return {"tasks": tasks}, summary


def main():
    parser = argparse.ArgumentParser(description="Generate one deterministic SatCompute TaskTrace.")
    parser.add_argument(
        "--profile",
        choices=(
            "stress",
            "f1-validation",
            "f2-validation",
            "n4b-joint-validation",
            "n4c-c800",
            "n4c-c800-109g",
            "n4c-c800-truncnormal",
            "n4c-c800-truncnormal-v3",
            "n4c-hotspot",
        ),
        default="stress",
    )
    parser.add_argument("--nodes-file", required=True, type=Path)
    parser.add_argument("--compute-profile", required=True, type=Path)
    parser.add_argument("--task-count", type=positive_int)
    parser.add_argument("--total-input-bytes", type=positive_int)
    parser.add_argument("--seed", required=True)
    parser.add_argument("--base-task-trace", type=Path,
                        help="Frozen G2 C800 trace; hotspot changes placement only")
    parser.add_argument("--base-workload-summary", type=Path,
                        help="v3 hotspot: generator summary carrying explicit fixed-tail IDs")
    parser.add_argument("--position-slices", type=Path, help="Native topology-only output directory")
    parser.add_argument("--hotspot-weight", type=positive_int, default=4)
    parser.add_argument("--workload-candidate", choices=("C800", "C800-109G", "C800-TruncNormal", "C800-TruncNormal-v3"), default="C800",
                        help="Explicit business baseline for n4c-hotspot; defaults to historical C800")
    parser.add_argument("--regional-candidate-limit", type=non_negative_int, default=0)
    parser.add_argument("--f3-from-none", type=Path,
                        help="n4c-hotspot: select a large controlled victim from actual none business timing")
    parser.add_argument("--arrival-start-ns", type=non_negative_int)
    parser.add_argument("--arrival-end-ns", type=non_negative_int)
    parser.add_argument("--arrival-mode", choices=("uniform", "burst"))
    parser.add_argument(
        "--enhancement-share-bp",
        type=non_negative_int,
        default=DEFAULT_CLASS_SHARES["image-enhancement"],
    )
    parser.add_argument(
        "--detection-share-bp",
        type=non_negative_int,
        default=DEFAULT_CLASS_SHARES["image-detection"],
    )
    parser.add_argument(
        "--dnn-share-bp",
        type=non_negative_int,
        default=DEFAULT_CLASS_SHARES["dnn-inference"],
    )
    parser.add_argument(
        "--preprocess-share-bp",
        type=non_negative_int,
        default=DEFAULT_CLASS_SHARES["preprocess-compress"],
    )
    parser.add_argument(
        "--large-1gb-count",
        type=non_negative_int,
        default=0,
        help="maximum-scenario 1 GB tail count before scenario scaling",
    )
    parser.add_argument(
        "--large-500mb-count",
        type=non_negative_int,
        default=0,
        help="maximum-scenario 500 MB tail count before scenario scaling",
    )
    parser.add_argument(
        "--scenario-scale-bp",
        type=non_negative_int,
        default=BASIS_POINTS,
        help="largest-remainder scale applied to both maximum tail counts",
    )
    parser.add_argument(
        "--tail-preprocess-share-bp",
        type=non_negative_int,
        default=DEFAULT_TAIL_SHARES["preprocess-compress"],
    )
    parser.add_argument(
        "--tail-enhancement-share-bp",
        type=non_negative_int,
        default=DEFAULT_TAIL_SHARES["image-enhancement"],
    )
    parser.add_argument(
        "--non-tail-min-input-bytes",
        type=positive_int,
        default=MIB,
    )
    parser.add_argument(
        "--non-tail-max-input-bytes",
        type=positive_int,
        default=300_000_000,
    )
    parser.add_argument("--output-task-trace", required=True, type=Path)
    parser.add_argument(
        "--output-workload-summary",
        required=True,
        type=Path,
    )
    args = parser.parse_args()

    if args.f3_from_none is not None and args.profile != "n4c-hotspot":
        parser.error("f3-from-none is only supported by n4c-hotspot")
    if "\0" in args.seed or not args.seed:
        raise ValueError("seed must be a non-empty string without NUL")
    satellite_ids = read_satellite_ids(args.nodes_file)
    compute_nodes = read_compute_profile(args.compute_profile, satellite_ids)
    if args.profile == "n4c-hotspot":
        if args.base_task_trace is None or args.position_slices is None:
            parser.error("n4c-hotspot requires base-task-trace and position-slices")
        if len(compute_nodes) != 66 or any(n["compute_rate_work_units_per_second"] != 100000
                                          for n in compute_nodes):
            parser.error("n4c-hotspot requires all 66 workers at 100000 WU/s")
        hotspot = runpy.run_path(str(Path(__file__).with_name("n4c_hotspot.py")))
        none_tasks = hotspot["read_none_tasks"](args.f3_from_none) if args.f3_from_none else None
        frozen_placement = args.workload_candidate in ("C800-TruncNormal", "C800-TruncNormal-v3")
        fixed_tail_ids = None
        if args.workload_candidate == "C800-TruncNormal-v3":
            if args.base_workload_summary is None:
                parser.error("v3 hotspot requires base-workload-summary for fixed-tail IDs")
            base_summary = read_json(args.base_workload_summary)
            if base_summary.get("workload_candidate") != args.workload_candidate:
                parser.error("base-workload-summary candidate differs")
            fixed_tail_ids = base_summary["truncated_normal"]["fixed_tail_task_ids"]
        f3_plan = (hotspot["select_f3_from_none"](none_tasks, args.seed)
                   if none_tasks is not None and not frozen_placement else None)
        if f3_plan is not None:
            f3_plan["none_evidence_directory"] = str(args.f3_from_none)
        trace, summary = hotspot["build_hotspot"](
            read_json(args.base_task_trace), hotspot["read_positions"](args.position_slices),
            args.seed, args.hotspot_weight, args.regional_candidate_limit,
            f3_plan=f3_plan, none_tasks=None if frozen_placement else none_tasks,
            workload_candidate=args.workload_candidate, fixed_tail_task_ids=fixed_tail_ids)
        if frozen_placement and none_tasks is not None:
            observed = {int(t["task_id"]): t for t in none_tasks}
            for task in trace["tasks"]:
                if any((observed[task["task_id"]][k] if k == "task_profile" else
                        int(observed[task["task_id"]][k])) != v for k, v in task.items()):
                    raise ValueError("isolated F3 requires exactly the same none task placement/business")
            import csv
            with (args.f3_from_none / "transfer-summary.csv").open() as stream:
                transfers = list(csv.DictReader(stream))
            summary["f3"] = hotspot["select_isolated_f3_from_none"](
                none_tasks, transfers, args.seed, fixed_tail_task_ids=fixed_tail_ids)
            summary["f3"]["none_evidence_directory"] = str(args.f3_from_none)
        write_json(args.output_task_trace, trace)
        write_json(args.output_workload_summary, summary)
        print(json.dumps({k: v for k, v in summary.items() if k != "placements"}, indent=2))
        return
    if args.profile in ("n4c-c800", "n4c-c800-109g", "n4c-c800-truncnormal", "n4c-c800-truncnormal-v3"):
        candidate = {"n4c-c800": "C800", "n4c-c800-109g": "C800-109G",
                     "n4c-c800-truncnormal": "C800-TruncNormal",
                     "n4c-c800-truncnormal-v3": "C800-TruncNormal-v3"}[args.profile]
        total = None if candidate in ("C800-TruncNormal", "C800-TruncNormal-v3") else (
            109_000_000_000 if candidate == "C800-109G" else 81_750_000_000)
        horizon = 1050 if candidate == "C800-TruncNormal-v3" else 900 if candidate == "C800-TruncNormal" else 600
        for name, expected in (("task_count", 800), ("total_input_bytes", total),
                               ("arrival_start_ns", 1_000_000_000),
                               ("arrival_end_ns", horizon * 10**9), ("arrival_mode", "uniform")):
            if getattr(args, name) not in (None, expected):
                raise ValueError(f"{args.profile} freezes {name}={expected}")
        trace, summary = build_n4c_c800_workload(satellite_ids, compute_nodes, args.seed, candidate)
        write_json(args.output_task_trace, trace)
        write_json(args.output_workload_summary, summary)
        print(f"PASS: generated {candidate} TaskTrace (800 tasks)")
        return
    if args.profile == "f1-validation":
        trace, summary = build_f1_validation_workload(
            satellite_ids,
            compute_nodes,
            args.seed,
        )
        write_json(args.output_task_trace, trace)
        write_json(args.output_workload_summary, summary)
        print("PASS: generated deterministic F1 validation TaskTrace (20 tasks)")
        return
    if args.profile == "f2-validation":
        trace, summary = build_f2_validation_workload(
            satellite_ids,
            compute_nodes,
            args.seed,
        )
        write_json(args.output_task_trace, trace)
        write_json(args.output_workload_summary, summary)
        print("PASS: generated deterministic F2 validation TaskTrace (8 tasks)")
        return
    if args.profile == "n4b-joint-validation":
        trace, summary = build_n4b_joint_validation_workload(
            satellite_ids,
            compute_nodes,
            args.seed,
        )
        write_json(args.output_task_trace, trace)
        write_json(args.output_workload_summary, summary)
        print("PASS: generated deterministic N4B joint TaskTrace (100 tasks)")
        return

    required_stress_arguments = {
        "task-count": args.task_count,
        "total-input-bytes": args.total_input_bytes,
        "arrival-start-ns": args.arrival_start_ns,
        "arrival-end-ns": args.arrival_end_ns,
        "arrival-mode": args.arrival_mode,
    }
    missing = [name for name, value in required_stress_arguments.items() if value is None]
    if missing:
        raise ValueError(
            "stress profile requires " + ", ".join(f"--{name}" for name in missing)
        )
    if args.task_count > UINT64_MAX // 2:
        raise ValueError("task-count cannot produce safe INPUT/RESULT transfer IDs")
    if args.total_input_bytes > UINT64_MAX:
        raise ValueError("total-input-bytes exceeds uint64")
    if args.arrival_end_ns > INT64_MAX:
        raise ValueError("arrival-end-ns exceeds int64")
    if args.scenario_scale_bp > BASIS_POINTS:
        raise ValueError("scenario-scale-bp must be in [0, 10000]")
    if args.non_tail_min_input_bytes > args.non_tail_max_input_bytes:
        raise ValueError("non-tail minimum exceeds non-tail maximum")

    class_shares = {
        "image-enhancement": args.enhancement_share_bp,
        "image-detection": args.detection_share_bp,
        "dnn-inference": args.dnn_share_bp,
        "preprocess-compress": args.preprocess_share_bp,
    }
    if sum(class_shares.values()) != BASIS_POINTS:
        raise ValueError("class share basis points must sum to 10000")
    tail_shares = {
        "preprocess-compress": args.tail_preprocess_share_bp,
        "image-enhancement": args.tail_enhancement_share_bp,
    }
    if sum(tail_shares.values()) != BASIS_POINTS:
        raise ValueError("tail class share basis points must sum to 10000")

    compute_ids = [item["node_id"] for item in compute_nodes]
    task_ids = list(range(1, args.task_count + 1))
    class_counts = largest_remainder(
        args.task_count,
        class_shares,
        CLASS_ORDER,
    )
    class_assignments = assign_classes(
        task_ids,
        class_counts,
        args.seed,
    )
    one_gb_count = scaled_tail_count(
        args.large_1gb_count,
        args.scenario_scale_bp,
    )
    five_hundred_mb_count = scaled_tail_count(
        args.large_500mb_count,
        args.scenario_scale_bp,
    )
    if one_gb_count + five_hundred_mb_count > args.task_count:
        raise ValueError("scaled large tail contains more tasks than task-count")
    tail_assignments, tail_class_counts = assign_tail_tasks(
        class_assignments,
        one_gb_count,
        five_hundred_mb_count,
        tail_shares,
        args.seed,
    )
    input_sizes = allocate_input_bytes(
        task_ids,
        class_assignments,
        tail_assignments,
        args.total_input_bytes,
        args.non_tail_min_input_bytes,
        args.non_tail_max_input_bytes,
        args.seed,
    )
    output_sizes = generate_output_bytes(
        task_ids,
        class_assignments,
        input_sizes,
        args.seed,
    )
    work_units = generate_work_units(
        task_ids,
        class_assignments,
        input_sizes,
        args.seed,
    )

    compute_assignments = assign_balanced_nodes(
        task_ids,
        compute_ids,
        args.seed,
        "compute-node",
    )
    source_assignments = assign_balanced_nodes(
        task_ids,
        satellite_ids,
        args.seed,
        "source-node",
        compute_assignments,
    )
    result_assignments = assign_balanced_nodes(
        task_ids,
        satellite_ids,
        args.seed,
        "result-node",
        compute_assignments,
    )
    arrivals = generate_arrivals(
        task_ids,
        args.arrival_start_ns,
        args.arrival_end_ns,
        args.arrival_mode,
        args.seed,
    )

    tasks = [
        {
            "task_id": task_id,
            "source_node_id": source_assignments[task_id],
            "compute_node_id": compute_assignments[task_id],
            "result_node_id": result_assignments[task_id],
            "input_bytes": input_sizes[task_id],
            "output_bytes": output_sizes[task_id],
            "compute_work_units": work_units[task_id],
            "arrival_time_ns": arrivals[task_id],
        }
        for task_id in task_ids
    ]
    trace = {"tasks": tasks}
    write_json(args.output_task_trace, trace)

    correlations, largest_means, smallest_means = class_correlation_metrics(
        task_ids,
        class_assignments,
        input_sizes,
        work_units,
    )
    profile_input = {
        class_name: sum(
            input_sizes[task_id]
            for task_id in task_ids
            if class_assignments[task_id] == class_name
        )
        for class_name in CLASS_ORDER
    }
    profile_output = {
        class_name: sum(
            output_sizes[task_id]
            for task_id in task_ids
            if class_assignments[task_id] == class_name
        )
        for class_name in CLASS_ORDER
    }
    profile_work = {
        class_name: sum(
            work_units[task_id]
            for task_id in task_ids
            if class_assignments[task_id] == class_name
        )
        for class_name in CLASS_ORDER
    }
    compute_node_task_counts = {
        str(node_id): sum(
            1
            for task_id in task_ids
            if compute_assignments[task_id] == node_id
        )
        for node_id in compute_ids
    }
    input_stats = distribution(list(input_sizes.values()))
    output_stats = distribution(list(output_sizes.values()))
    work_stats = distribution(list(work_units.values()))
    summary = {
        "seed": args.seed,
        "task_count": args.task_count,
        "total_input_bytes": sum(input_sizes.values()),
        "total_output_bytes": sum(output_sizes.values()),
        "total_compute_work_units": sum(work_units.values()),
        "arrival_mode": args.arrival_mode,
        "arrival_start_ns": args.arrival_start_ns,
        "arrival_end_ns": args.arrival_end_ns,
        "class_share_basis_points": class_shares,
        "profile_counts": class_counts,
        "profile_input_bytes": profile_input,
        "profile_output_bytes": profile_output,
        "profile_work_units": profile_work,
        "maximum_large_1gb_count": args.large_1gb_count,
        "maximum_large_500mb_count": args.large_500mb_count,
        "scenario_scale_bp": args.scenario_scale_bp,
        "large_1gb_count": one_gb_count,
        "large_500mb_count": five_hundred_mb_count,
        "tail_class_share_basis_points": tail_shares,
        "tail_class_counts": tail_class_counts,
        "non_tail_min_input_bytes": args.non_tail_min_input_bytes,
        "non_tail_max_input_bytes": args.non_tail_max_input_bytes,
        "min_input_bytes": input_stats["min"],
        "mean_input_bytes": input_stats["mean"],
        "max_input_bytes": input_stats["max"],
        "min_output_bytes": output_stats["min"],
        "mean_output_bytes": output_stats["mean"],
        "max_output_bytes": output_stats["max"],
        "min_work_units": work_stats["min"],
        "mean_work_units": work_stats["mean"],
        "max_work_units": work_stats["max"],
        "first_arrival_ns": min(arrivals.values()),
        "last_arrival_ns": max(arrivals.values()),
        "compute_node_task_counts": compute_node_task_counts,
        "task_class_assignments": {
            str(task_id): class_assignments[task_id]
            for task_id in task_ids
        },
        "per_class_spearman_input_work": correlations,
        "per_class_largest_quartile_mean_work": largest_means,
        "per_class_smallest_quartile_mean_work": smallest_means,
    }
    write_json(args.output_workload_summary, summary)
    print(
        "PASS: generated deterministic TaskTrace "
        f"({args.task_count} tasks, {sum(input_sizes.values())} input bytes, "
        f"{one_gb_count} x 1 GB, {five_hundred_mb_count} x 500 MB)"
    )


if __name__ == "__main__":
    main()
