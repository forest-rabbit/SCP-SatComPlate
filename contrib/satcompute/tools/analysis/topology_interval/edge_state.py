#!/usr/bin/env python3
"""Compare reference and held ISL edge states and connectivity."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from ...generation.topology.common.manifest import read_json, validate_trace


Edge = tuple[int, int]


class EdgeStateError(ValueError):
    """Raised when edge traces cannot be compared safely."""


@dataclass(frozen=True)
class EdgeTrace:
    """Ordered complete edge sets at one or more simulation times."""

    node_count: int
    snapshots: tuple[tuple[int, frozenset[Edge]], ...]

    def __post_init__(self) -> None:
        if (
            not isinstance(self.node_count, int)
            or isinstance(self.node_count, bool)
            or self.node_count <= 0
        ):
            raise EdgeStateError("node_count must be a positive integer")
        if not self.snapshots or self.snapshots[0][0] != 0:
            raise EdgeStateError("edge trace must begin at simulation time 0")
        previous_time = -1
        for time_s, edges in self.snapshots:
            if (
                not isinstance(time_s, int)
                or isinstance(time_s, bool)
                or time_s <= previous_time
            ):
                raise EdgeStateError(
                    "edge trace times must be strictly increasing integers"
                )
            for node1_id, node2_id in edges:
                if not 0 <= node1_id < node2_id < self.node_count:
                    raise EdgeStateError("edge trace endpoint is invalid")
            previous_time = time_s

    @property
    def times(self) -> tuple[int, ...]:
        return tuple(time_s for time_s, _ in self.snapshots)

    @property
    def duration_s(self) -> int:
        return self.snapshots[-1][0]


@dataclass(frozen=True)
class ConnectivityState:
    """Connected-component summary for one undirected edge set."""

    connected_component_count: int
    largest_component_size: int
    unreachable_unordered_pairs: int


def edge_set_sha256(edges: Iterable[Edge]) -> str:
    """Hash a canonical sorted undirected edge list."""
    payload = "".join(
        f"{node1_id},{node2_id}\n"
        for node1_id, node2_id in sorted(edges)
    ).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def connectivity_state(
    node_count: int,
    edges: frozenset[Edge],
) -> ConnectivityState:
    """Compute components with a standard-library iterative DFS."""
    adjacency = [[] for _ in range(node_count)]
    for node1_id, node2_id in edges:
        adjacency[node1_id].append(node2_id)
        adjacency[node2_id].append(node1_id)
    seen = set()
    component_sizes = []
    for start in range(node_count):
        if start in seen:
            continue
        seen.add(start)
        stack = [start]
        size = 0
        while stack:
            node_id = stack.pop()
            size += 1
            for neighbor in adjacency[node_id]:
                if neighbor not in seen:
                    seen.add(neighbor)
                    stack.append(neighbor)
        component_sizes.append(size)
    reachable_pairs = sum(
        size * (size - 1) // 2 for size in component_sizes
    )
    total_pairs = node_count * (node_count - 1) // 2
    return ConnectivityState(
        connected_component_count=len(component_sizes),
        largest_component_size=max(component_sizes),
        unreachable_unordered_pairs=total_pairs - reachable_pairs,
    )


def load_edge_trace(
    topology_trace_dir: Path,
    *,
    validate: bool = True,
) -> EdgeTrace:
    """Load complete edge sets from one v0.3 topology trace."""
    root = Path(topology_trace_dir)
    if validate:
        validate_trace(root)
    return load_topology_edge_trace(root)


def load_topology_edge_trace(topology_dir: Path) -> EdgeTrace:
    """Load manifest-listed snapshots from a canonical topology trace."""
    root = Path(topology_dir)
    manifest = read_json(root / "manifest.json")
    if not isinstance(manifest, dict) or not isinstance(
        manifest.get("slices"), list
    ):
        raise EdgeStateError("topology manifest must list slices")
    initial_node_ids = None
    snapshots = []
    for record in manifest["slices"]:
        time_ns = record["simulation_time_ns"]
        if time_ns % 1_000_000_000 != 0:
            raise EdgeStateError(
                "topology-interval analysis requires integer-second slices"
            )
        time_s = time_ns // 1_000_000_000
        nodes_payload = read_json(root / record["nodes_file"])
        topology_payload = read_json(root / record["topology_file"])
        if not isinstance(nodes_payload, dict) or not isinstance(
            nodes_payload.get("nodes"), list
        ):
            raise EdgeStateError(f"nodes slice at {time_s}s is invalid")
        node_ids = tuple(node.get("node_id") for node in nodes_payload["nodes"])
        if initial_node_ids is None:
            initial_node_ids = node_ids
            if node_ids != tuple(range(len(node_ids))):
                raise EdgeStateError(
                    "interval analysis requires dense node IDs from 0"
                )
        elif node_ids != initial_node_ids:
            raise EdgeStateError("topology node set changes across snapshots")
        if (
            not isinstance(topology_payload, dict)
            or not isinstance(topology_payload.get("links"), list)
        ):
            raise EdgeStateError(
                f"topology_{time_s}s.json root is invalid"
            )
        edges = set()
        for link in topology_payload["links"]:
            if not isinstance(link, dict):
                raise EdgeStateError(f"topology_{time_s}s link is invalid")
            endpoint1 = link.get("node1_id")
            endpoint2 = link.get("node2_id")
            if (
                not isinstance(endpoint1, int)
                or isinstance(endpoint1, bool)
                or not isinstance(endpoint2, int)
                or isinstance(endpoint2, bool)
            ):
                raise EdgeStateError(f"topology_{time_s}s endpoint is invalid")
            edge = tuple(sorted((endpoint1, endpoint2)))
            if endpoint1 == endpoint2 or edge in edges:
                raise EdgeStateError(f"topology_{time_s}s edge is invalid")
            edges.add(edge)
        snapshots.append((time_s, frozenset(edges)))
    if initial_node_ids is None:
        raise EdgeStateError("topology contains no node snapshots")
    return EdgeTrace(len(initial_node_ids), tuple(snapshots))


def expand_held_edges(
    reference: EdgeTrace,
    held: EdgeTrace,
) -> tuple[frozenset[Edge], ...]:
    if reference.node_count != held.node_count:
        raise EdgeStateError("reference and held node counts differ")
    if reference.times != tuple(range(reference.duration_s + 1)):
        raise EdgeStateError(
            "reference edge trace must contain every integer second"
        )
    if held.duration_s != reference.duration_s:
        raise EdgeStateError("reference and held durations differ")
    held_by_time = dict(held.snapshots)
    reference_by_time = dict(reference.snapshots)
    for time_s, edges in held.snapshots:
        if edges != reference_by_time[time_s]:
            raise EdgeStateError(
                "held snapshots must be exact samples of the reference"
            )
    selected_index = 0
    selected_times = held.times
    expanded = []
    for time_s in reference.times:
        while (
            selected_index + 1 < len(selected_times)
            and selected_times[selected_index + 1] <= time_s
        ):
            selected_index += 1
        expanded.append(held_by_time[selected_times[selected_index]])
    return tuple(expanded)


def _nearest_rank(values: list[int], percentile: float) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def _transition_fidelity(
    reference_edges: tuple[frozenset[Edge], ...],
    held: EdgeTrace,
) -> tuple[int, list[int]]:
    """Return invisible events and delay of each endpoint-visible event."""
    missed = 0
    timing_errors = []
    selected_times = held.times
    for start_s, end_s in zip(selected_times, selected_times[1:]):
        relevant_edges = set()
        for time_s in range(start_s + 1, end_s + 1):
            relevant_edges.update(
                reference_edges[time_s - 1] ^ reference_edges[time_s]
            )
        for edge in relevant_edges:
            event_times = [
                time_s
                for time_s in range(start_s + 1, end_s + 1)
                if (
                    (edge in reference_edges[time_s - 1])
                    != (edge in reference_edges[time_s])
                )
            ]
            endpoint_changed = (
                (edge in reference_edges[start_s])
                != (edge in reference_edges[end_s])
            )
            represented = 1 if endpoint_changed else 0
            missed += len(event_times) - represented
            if represented:
                timing_errors.append(end_s - event_times[-1])
    return missed, timing_errors


def _maximum_stale_duration(
    reference_edges: tuple[frozenset[Edge], ...],
    held_edges: tuple[frozenset[Edge], ...],
) -> int:
    maximum = 0
    current = 0
    for reference, held in zip(reference_edges, held_edges):
        if reference != held:
            current += 1
            maximum = max(maximum, current)
        else:
            current = 0
    return maximum


def compare_edge_traces(
    reference: EdgeTrace,
    held: EdgeTrace,
) -> dict[str, Any]:
    """Return edge, transition, and connectivity fidelity metrics."""
    held_edges = expand_held_edges(reference, held)
    reference_edges = tuple(edges for _, edges in reference.snapshots)
    absolute_errors = 0
    union_total = 0
    mismatch_seconds = 0
    missed_active_edges = 0
    spurious_active_edges = 0
    for expected, actual in zip(reference_edges, held_edges):
        absolute_errors += len(expected ^ actual)
        union_total += len(expected | actual)
        mismatch_seconds += expected != actual
        missed_active_edges += len(expected - actual)
        spurious_active_edges += len(actual - expected)

    missed_transitions, timing_errors = _transition_fidelity(
        reference_edges,
        held,
    )
    connectivity_cache: dict[str, ConnectivityState] = {}

    def cached_connectivity(edges: frozenset[Edge]) -> ConnectivityState:
        fingerprint = edge_set_sha256(edges)
        if fingerprint not in connectivity_cache:
            connectivity_cache[fingerprint] = connectivity_state(
                reference.node_count,
                edges,
            )
        return connectivity_cache[fingerprint]

    component_mismatch_seconds = 0
    unreachable_error_sum = 0
    max_unreachable_error = 0
    for expected, actual in zip(reference_edges, held_edges):
        expected_state = cached_connectivity(expected)
        actual_state = cached_connectivity(actual)
        component_mismatch_seconds += (
            expected_state.connected_component_count
            != actual_state.connected_component_count
        )
        unreachable_error = abs(
            expected_state.unreachable_unordered_pairs
            - actual_state.unreachable_unordered_pairs
        )
        unreachable_error_sum += unreachable_error
        max_unreachable_error = max(
            max_unreachable_error,
            unreachable_error,
        )

    reference_fingerprints = {
        edge_set_sha256(edges) for edges in reference_edges
    }
    held_fingerprints = {edge_set_sha256(edges) for edges in held_edges}
    return {
        "reference_snapshot_count": len(reference.snapshots),
        "held_snapshot_count": len(held.snapshots),
        "reference_unique_edge_set_count": len(reference_fingerprints),
        "held_unique_edge_set_count": len(held_fingerprints),
        "reference_edge_set_fingerprint_count":
            len(reference_fingerprints),
        "held_edge_set_fingerprint_count": len(held_fingerprints),
        "absolute_edge_state_errors": absolute_errors,
        "normalized_edge_state_disagreement":
            absolute_errors / union_total if union_total else 0.0,
        "endpoint_mismatch_seconds": mismatch_seconds,
        "missed_active_edges": missed_active_edges,
        "spurious_active_edges": spurious_active_edges,
        "missed_transition_count": missed_transitions,
        "mean_event_timing_error_s":
            sum(timing_errors) / len(timing_errors)
            if timing_errors
            else 0.0,
        "p95_event_timing_error_s":
            _nearest_rank(timing_errors, 0.95),
        "max_event_timing_error_s": max(timing_errors, default=0),
        "maximum_stale_duration_s": _maximum_stale_duration(
            reference_edges,
            held_edges,
        ),
        "component_count_mismatch_seconds":
            component_mismatch_seconds,
        "unreachable_pair_error_sum": unreachable_error_sum,
        "max_unreachable_pair_error": max_unreachable_error,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--held-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        result = compare_edge_traces(
            load_edge_trace(arguments.reference_dir),
            load_edge_trace(arguments.held_dir),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
